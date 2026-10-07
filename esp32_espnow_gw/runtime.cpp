#include "runtime.h"
#include "state.h"
#include "config.h"
#include "wifi_mgr.h"
#include "mqtt_client.h"
#include "bootstrap.h"
#include "relay_ctrl.h"
#include "nvs_store.h"
#include "espnow_link.h"
#include "proxy.h"
#include "gw_state.h"
#include <ArduinoJson.h>

extern NvsStore nvs;
extern WiFiMgr wifi;
extern MqttClient mqtt;
extern RelayCtrl relays;
extern ProxyMgr proxies;

Runtime runtime;

// Buffers are file-scope (not start() locals): the bootstrap path
// re-reads and re-fills them from tick() long after start() returned.
static char cmdTopicBuf[160];
static char stateTopicBuf[160];
static char clientIdBuf[80];
static char ssidBuf[64], wifiPassBuf[64], hostBuf[64], custBuf[64], devBuf[64];
static char mqttUserBuf[64], mqttPassBuf[96];
static char enrollBuf[96];

const char* runtimeMqttHost() { return hostBuf; }
uint16_t runtimeMqttPort() {
  int32_t p = nvs.getInt(NVS_KEY_MQTT_PORT, DEFAULT_MQTT_PORT);
  if (p <= 0 || p > 65535) p = DEFAULT_MQTT_PORT;
  return (uint16_t)p;
}

void Runtime::start() {
  if (_started) return;

  Serial.println("runtime: starting");

  relays.begin();

  bool haveSsid = nvs.getCStr(NVS_KEY_WIFI_SSID, ssidBuf, sizeof(ssidBuf)) && strlen(ssidBuf) > 0;
  bool haveWifiPass = nvs.getCStr(NVS_KEY_WIFI_PASS, wifiPassBuf, sizeof(wifiPassBuf)) && strlen(wifiPassBuf) > 0;
  bool haveDev = nvs.getCStr(NVS_KEY_ID_DEVICE, devBuf, sizeof(devBuf)) && strlen(devBuf) > 0;
  bool haveEnroll = nvs.getCStr(NVS_KEY_ENROLL_SECRET, enrollBuf, sizeof(enrollBuf)) && strlen(enrollBuf) > 0;

  // WiFi + device identity are the irreducible bench-provisioned set;
  // everything MQTT-side can come from bootstrap (migration 039).
  if (!haveSsid || !haveWifiPass || !haveDev) {
    Serial.println("runtime: missing required NVS keys:");
    if (!haveSsid) Serial.println("  - wifi.ssid");
    if (!haveWifiPass) Serial.println("  - wifi.pass");
    if (!haveDev)  Serial.println("  - id.device");
    Serial.println("runtime: not starting; configure via CLI (set wifi ssid ..., set id device ..., etc)");
    return;
  }
  if (!haveEnroll) {
    Serial.println("runtime: NOTE no enroll.secret - bootstrap impossible; set via CLI if credentials are ever lost");
  }

  g_state.failSafe = nvs.getInt(NVS_KEY_FAIL_SAFE, 1) != 0;

  Serial.print("runtime: ssid=");
  Serial.println(ssidBuf);
  Serial.print("runtime: device=");
  Serial.println(devBuf);
  Serial.print("runtime: fail_safe=");
  Serial.println(g_state.failSafe ? "on" : "off");

  wifi.begin(ssidBuf, wifiPassBuf);

  // ESP-NOW rides the same radio; its channel is the AP's once
  // associated (spec §3.1). Peer slots load from NVS after the MAC
  // is queryable.
  espnow.begin();
  proxies.begin();

  _mqttConfigured = startMqtt();
  if (!_mqttConfigured) {
    Serial.println("runtime: mqtt config incomplete; will bootstrap once WiFi is up");
  }

  g_state.bootMs = millis();
  g_state.lastHeartbeatMs = millis();
  g_state.relaysChanged = true;

  _started = true;
  Serial.println("runtime: started");
}

// Load the MQTT-side NVS keys and bring the client up. Returns false
// (with a diagnostic list) when anything is missing — the caller then
// either bootstraps or waits.
bool Runtime::startMqtt() {
  bool haveHost = nvs.getCStr(NVS_KEY_MQTT_HOST, hostBuf, sizeof(hostBuf)) && strlen(hostBuf) > 0;
  bool haveMqttUser = nvs.getCStr(NVS_KEY_MQTT_USER, mqttUserBuf, sizeof(mqttUserBuf)) && strlen(mqttUserBuf) > 0;
  bool haveMqttPass = nvs.getCStr(NVS_KEY_MQTT_PASS, mqttPassBuf, sizeof(mqttPassBuf)) && strlen(mqttPassBuf) > 0;
  bool haveCust = nvs.getCStr(NVS_KEY_ID_CUSTOMER, custBuf, sizeof(custBuf)) && strlen(custBuf) > 0;

  if (!haveHost || !haveMqttUser || !haveMqttPass || !haveCust) {
    Serial.println("runtime: mqtt config missing:");
    if (!haveHost) Serial.println("  - mqtt.host");
    if (!haveMqttUser) Serial.println("  - mqtt.user");
    if (!haveMqttPass) Serial.println("  - mqtt.pass");
    if (!haveCust) Serial.println("  - id.customer");
    return false;
  }

  int32_t port = nvs.getInt(NVS_KEY_MQTT_PORT, DEFAULT_MQTT_PORT);

  snprintf(cmdTopicBuf, sizeof(cmdTopicBuf), TOPIC_CMD_FMT, custBuf, devBuf);
  snprintf(stateTopicBuf, sizeof(stateTopicBuf), TOPIC_STATE_FMT, custBuf, devBuf);
  // MQTT client_id is exactly the device_uid (UUID v4, 36 chars).
  // Do NOT prepend 'device_' or 'dev_' — broker logs, the bridge
  // worker's regex, and the broker ACL patterns (keyed on %c) match
  // the bare UID. The proxy connections below follow the same rule
  // with each REMOTE's uid.
  snprintf(clientIdBuf, sizeof(clientIdBuf), "%s", devBuf);

  mqtt.begin(hostBuf, (uint16_t)port, clientIdBuf, mqttUserBuf, mqttPassBuf, cmdTopicBuf, stateTopicBuf);
  g_state.mqttAuthFailed = false;
  return true;
}

// One bootstrap attempt. On success the returned config is written to
// NVS (bootstrap is also how credentials get rotated later) and the
// MQTT client is started.
void Runtime::runBootstrap() {
  char bh[64];
  if (!(nvs.getCStr(NVS_KEY_BOOTSTRAP_HOST, bh, sizeof(bh)) && strlen(bh) > 0)) {
    snprintf(bh, sizeof(bh), "%s", DEFAULT_BOOTSTRAP_HOST);
  }
  // getInt returns the default only when the key was never WRITTEN; a
  // blank-struct zero (fresh layout) or a stale/garbage value must also
  // fall back — 0.4.6 asked for 10.0.4.1:0 on first boot.
  int32_t bpRaw = nvs.getInt(NVS_KEY_BOOTSTRAP_PORT, DEFAULT_BOOTSTRAP_PORT);
  if (bpRaw <= 0 || bpRaw > 65535) bpRaw = DEFAULT_BOOTSTRAP_PORT;
  uint16_t bp = (uint16_t)bpRaw;

  Serial.print("bootstrap: requesting config from ");
  Serial.print(bh);
  Serial.print(":");
  Serial.println(bp);

  uint16_t respPort = 0;
  Bootstrapper b;
  int code = b.fetch(bh, bp, devBuf, enrollBuf,
                     mqttUserBuf, sizeof(mqttUserBuf),
                     mqttPassBuf, sizeof(mqttPassBuf),
                     custBuf, sizeof(custBuf),
                     hostBuf, sizeof(hostBuf), &respPort);
  if (code == 200) {
    nvs.putCStr(NVS_KEY_MQTT_USER, mqttUserBuf);
    nvs.putCStr(NVS_KEY_MQTT_PASS, mqttPassBuf);
    nvs.putCStr(NVS_KEY_ID_CUSTOMER, custBuf);
    nvs.putCStr(NVS_KEY_MQTT_HOST, hostBuf);
    nvs.putInt(NVS_KEY_MQTT_PORT, respPort);
    if (startMqtt()) {
      _mqttConfigured = true;
      Serial.println("bootstrap: configured; mqtt starting");
    }
  } else if (code == 401) {
    // Unknown, already claimed, or locked. If we got here WITH stored
    // MQTT credentials, "locked" means we are already claimed and
    // the stored credential is the right one (possibly not yet in
    // the broker's password file) — go back to trying it instead of
    // parking in bootstrap mode forever.
    Serial.println("bootstrap: rejected; backing off 10 min");
    _bootstrapDelayMs = BOOTSTRAP_BACKOFF_401_MS;
    if (startMqtt()) {
      _mqttConfigured = true;
      Serial.println("bootstrap: already claimed; retrying stored credential");
    }
  } else {
    Serial.println("bootstrap: failed; will retry");
    _bootstrapDelayMs = BOOTSTRAP_RETRY_MS;
  }
}

void Runtime::drainEspNow() {
  EspNowRxItem item;
  while (espnow.drain(&item)) {
    proxies.onFrame(item);
  }
}

void Runtime::tick() {
  if (!_started) return;

  wifi.tick();

  unsigned long now = millis();

  // Bootstrap mode: no usable MQTT config yet — wait for WiFi, then
  // fetch the config (retry cadence kept under the backend's
  // per-client bootstrap rate limit).
  if (!_mqttConfigured) {
    if (!wifi.isConnected()) return;
    if (now - _lastBootstrapMs < _bootstrapDelayMs) return;
    _lastBootstrapMs = now;
    runBootstrap();
    return;
  }

  // Self-healing rotation: the broker rejected our credential
  // (rc=4/5). That is USUALLY transient — a freshly claimed or
  // rotated credential takes up to the cron interval (2 min) to
  // reach the broker's password file — so keep the normal 5-second
  // retry loop connecting through a grace period first. Only if the
  // rejection persists does re-bootstrapping start (0.4.5: the
  // 0.4.3/0.4.4 guard fired instantly because its clock started at
  // zero, flipping the device into bootstrap mode on the first
  // post-claim rejection and stranding it there).
  if (!g_state.mqttAuthFailed) {
    _lastReBootstrapMs = 0;  // working credential: reset the clock
  } else {
    if (_lastReBootstrapMs == 0) _lastReBootstrapMs = now;
    if (now - _lastReBootstrapMs > BOOTSTRAP_REBOOTSTRAP_MIN_MS) {
      _lastReBootstrapMs = now;
      Serial.println("runtime: broker still rejects credentials; re-bootstrapping");
      g_state.mqttConnected = false;
      g_state.mqttAuthFailed = false;
      _mqttConfigured = false;
      _bootstrapDelayMs = BOOTSTRAP_RETRY_MS;
      return;
    }
  }

  mqtt.tick();

  // ESP-NOW: receive → dispatch, proxy keepalive + forwarder, and
  // the 60 s heartbeat that feeds every remote's fail-safe clock.
  drainEspNow();
  proxies.tick();
  gwStateTick();

  if (now - g_state.lastHeartbeatMs > HEARTBEAT_PERIOD_MS) {
    g_state.relaysChanged = true;
    g_state.lastHeartbeatMs = now;
  }

  if (g_state.failSafe && relays.anyOn()) {
    unsigned long lastGood = g_state.wifiLastGoodMs;
    if (g_state.mqttLastGoodMs < lastGood) lastGood = g_state.mqttLastGoodMs;
    if ((!g_state.wifiConnected || !g_state.mqttConnected) &&
        now - lastGood > FAIL_SAFE_TIMEOUT_MS) {
      Serial.println("fail-safe: disconnect timeout, all relays OFF");
      relays.setAll(false);
      g_state.failSafeTriggered = true;
      g_state.relaysChanged = true;
    }
  }

  if (g_state.relaysChanged && mqtt.isConnected() &&
      now - g_state.lastStatePublishMs > STATE_PUBLISH_DEBOUNCE_MS) {
    publishState();
    g_state.relaysChanged = false;
    g_state.lastStatePublishMs = now;
  }
}

void Runtime::publishState() {
  // Larger buffer than esp32_agroiot's: the `gw` block adds ~60 B
  // per provisioned peer.
  DynamicJsonDocument doc(1536);

  if (g_state.lastCommandLogId > 0) {
    doc["command_log_id"] = g_state.lastCommandLogId;
  }

  JsonObject relaysObj = doc.createNestedObject("relays_state");
  for (int i = 0; i < RELAY_COUNT; i++) {
    char key[16];
    snprintf(key, sizeof(key), "relay_%d", i);
    relaysObj[key] = g_state.relays[i] ? "on" : "off";
  }

  doc["rssi"] = wifi.rssi();
  doc["uptime_s"] = (uint32_t)((millis() - g_state.bootMs) / 1000);
  doc["wifi_status"] = g_state.wifiConnected ? "connected" : "disconnected";
  doc["mqtt_status"] = g_state.mqttConnected ? "connected" : "disconnected";
  doc["free_heap"] = (uint32_t)ESP.getFreeHeap();

  appendGwBlock(doc);

  char buf[1400];
  size_t len = serializeJson(doc, buf, sizeof(buf));
  if (len == 0 || len >= sizeof(buf)) {
    Serial.println("state json serialize failed");
    return;
  }

  if (mqtt.publishState(buf)) {
    Serial.print("mqtt out: ");
    Serial.println(buf);
  }
}
