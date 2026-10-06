#include "runtime.h"
#include "state.h"
#include "config.h"
#include "nvs_store.h"
#include "bootstrap.h"
#include "relay_ctrl.h"
#include "espnow_link.h"
#include "sensors.h"
#include <WiFi.h>
#include <esp_wifi.h>
#include <ArduinoJson.h>

extern NvsStore nvs;
extern RelayCtrl relays;

Runtime runtime;

// File-scope identity buffers: filled in start(), read on every
// frame build afterwards.
static char devBuf[64];
static char enrollBuf[96];
static char custBuf[40];
static char mqttUserBuf[64];
static char mqttPassBuf[96];

void Runtime::start() {
  if (_started) return;

  bool haveDev = nvs.getCStr(NVS_KEY_ID_DEVICE, devBuf, sizeof(devBuf)) && strlen(devBuf) > 0;
  bool haveEnroll = nvs.getCStr(NVS_KEY_ENROLL_SECRET, enrollBuf, sizeof(enrollBuf)) && strlen(enrollBuf) > 0;

  if (!haveDev) {
    Serial.println("runtime: missing id.device; configure via CLI");
    return;
  }
  if (!haveEnroll) {
    Serial.println("runtime: NOTE no enroll.secret — bootstrap and ESP-NOW keying both impossible until set");
  }

  powerLoad(_policy);
  g_rs.profileAlwaysOn = powerIsAlwaysOn(_policy);
  g_rs.readingPeriodMs = (uint32_t)_policy.wakeIntervalS * 1000UL;

  Serial.printf("runtime: device=%s profile=%s source=%s wake=%lds\n",
                devBuf, _policy.profile, _policy.source,
                (long)_policy.wakeIntervalS);

  if (powerAllowsRelays(_policy)) {
    relays.begin();
    g_rs.failSafe = nvs.getInt(NVS_KEY_FAIL_SAFE, 1) != 0;
  } else {
    Serial.println("runtime: sleep_tx — relays DISABLED (spec §2)");
  }

  if (haveMqttCreds()) {
    completeInit();
  } else {
    // Field remotes carry their credentials from the bench. A node
    // without them can only be at the bench: associate, claim once,
    // disconnect, then operate radio-only.
    _benchHold = true;
    Serial.println("runtime: no mqtt credentials — bench bootstrap needed (set wifi, then wait; or 'wake' to retry)");
  }
}

bool Runtime::haveMqttCreds() const {
  return nvs.getCStr(NVS_KEY_MQTT_USER, mqttUserBuf, sizeof(mqttUserBuf)) && mqttUserBuf[0] &&
         nvs.getCStr(NVS_KEY_MQTT_PASS, mqttPassBuf, sizeof(mqttPassBuf)) && mqttPassBuf[0] &&
         nvs.getCStr(NVS_KEY_ID_CUSTOMER, custBuf, sizeof(custBuf)) && custBuf[0];
}

// One bench bootstrap: associate just long enough to claim, then get
// off the air — the remote must not sit on WiFi in the field (§3.1).
bool Runtime::benchBootstrap() {
  char ssid[64], pass[64];
  if (!(nvs.getCStr(NVS_KEY_WIFI_SSID, ssid, sizeof(ssid)) && ssid[0])) {
    Serial.println("bootstrap: no wifi.ssid — cannot reach the backend");
    return false;
  }
  nvs.getCStr(NVS_KEY_WIFI_PASS, pass, sizeof(pass));

  Serial.print("bootstrap: wifi ");
  Serial.println(ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);
  unsigned long deadline = millis() + WIFI_CONNECT_TIMEOUT_MS;
  while (WiFi.status() != WL_CONNECTED && millis() < deadline) {
    delay(100);
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("bootstrap: wifi failed");
    WiFi.mode(WIFI_OFF);
    return false;
  }
  Serial.print("bootstrap: associated, IP ");
  Serial.println(WiFi.localIP());

  char bh[64];
  if (!(nvs.getCStr(NVS_KEY_BOOTSTRAP_HOST, bh, sizeof(bh)) && bh[0])) {
    snprintf(bh, sizeof(bh), "%s", DEFAULT_BOOTSTRAP_HOST);
  }
  int32_t bpRaw = nvs.getInt(NVS_KEY_BOOTSTRAP_PORT, DEFAULT_BOOTSTRAP_PORT);
  if (bpRaw <= 0 || bpRaw > 65535) bpRaw = DEFAULT_BOOTSTRAP_PORT;

  char host[64];
  char user[64], pw[96], cust[40];
  uint16_t port = 0;
  Bootstrapper b;
  int code = b.fetch(bh, (uint16_t)bpRaw, devBuf, enrollBuf,
                     user, sizeof(user), pw, sizeof(pw),
                     cust, sizeof(cust), host, sizeof(host), &port);
  WiFi.mode(WIFI_OFF);  // radio free for ESP-NOW regardless of outcome

  if (code != 200) {
    Serial.printf("bootstrap: failed (HTTP %d); will retry\n", code);
    return false;
  }
  nvs.putCStr(NVS_KEY_MQTT_USER, user);
  nvs.putCStr(NVS_KEY_MQTT_PASS, pw);
  nvs.putCStr(NVS_KEY_ID_CUSTOMER, cust);
  nvs.putCStr(NVS_KEY_MQTT_HOST, host);
  nvs.putInt(NVS_KEY_MQTT_PORT, port);
  Serial.println("bootstrap: claimed; credentials stored for handover");
  return true;
}

void Runtime::completeInit() {
  // The radio is free (bench path turned WiFi off, field path never
  // associated) — pin the site channel now.
  espnow.begin(true);
  int gws = espnow.loadGateways(enrollBuf);
  if (gws == 0) {
    Serial.println("runtime: no gateways paired; set gw.0 mac via CLI");
  }

  g_rs.bootMs = millis();
  g_rs.lastGwFrameMs = millis();
  g_rs.relaysChanged = true;
  _lastReadingMs = millis();
  _started = true;

  if (!powerIsAlwaysOn(_policy)) {
    // sleep_tx: the wake cycle IS the boot. It ends in deep sleep.
    runWakeCycle();
  } else {
    // always_on: boot hello (§6.1 "first frame after power-up") —
    // announces the ack cursor and gives the gateway our uid even
    // before the first reading.
    sendHello();
  }
  Serial.println("runtime: started");
}

bool Runtime::sendHello() {
  StaticJsonDocument<256> hello;
  hello["v"] = ESPNOW_FRAME_VERSION;
  hello["seq"] = ++g_rs.seq;
  hello["uid"] = devBuf;
  hello["type"] = "hello";
  hello["last_cmd_id"] = g_rs.highestCmdIdSeen;
  char buf[192];
  size_t len = serializeJson(hello, buf, sizeof(buf));
  int gwIdx = -1;
  bool ok = (len > 0) && espnow.sendWalk(buf, &gwIdx);
  if (ok) g_rs.gwSeen = true;
  return ok;
}

void Runtime::runWakeCycle() {
  g_rs.gwSeen = false;

  // Hello first (§6.1): announces the boot/wake, carries the ack
  // cursor so the gateway can reconcile a missed command.
  sendHello();

  // Bounded RX window (§7.6): gateway replies (hb/cfg/cmd) land here.
  // TX-done already resolved inside sendWalk; this window is purely
  // for the reply, never a fixed radio-on timer.
  unsigned long deadline = millis() + RX_WINDOW_MS;
  while (millis() < deadline) {
    drainEspNow();
    delay(2);
  }

  sendReading(false);

  if (g_rs.gwSeen) {
    g_rs.offlineStreak = 0;
  } else if (g_rs.offlineStreak < 250) {
    g_rs.offlineStreak++;
  }

  SensorData e;
  sensorsRead(e, _policy.source);
  uint32_t eff = powerEffectiveIntervalS(_policy, e, g_rs.offlineStreak,
                                         g_rs.gwSeen);
  powerSleepNow(eff);
  // never returns
}

void Runtime::sendReading(bool includeRelays) {
  StaticJsonDocument<384> frame;
  frame["v"] = ESPNOW_FRAME_VERSION;
  frame["seq"] = ++g_rs.seq;
  frame["uid"] = devBuf;
  frame["type"] = "reading";

  SensorData e;
  sensorsRead(e, _policy.source);
  appendSensors(frame.as<JsonObject>(), e);

  if (includeRelays && powerAllowsRelays(_policy)) {
    JsonObject relaysObj = frame.createNestedObject("relays");
    for (int i = 0; i < RELAY_COUNT; i++) {
      char key[16];
      snprintf(key, sizeof(key), "relay_%d", i);
      relaysObj[key] = g_rs.relays[i] ? "on" : "off";
    }
    if (g_rs.highestCmdIdSeen > 0) {
      frame["cmd_id"] = g_rs.highestCmdIdSeen;
    }
  }
  if (g_rs.haveHbRssi) {
    frame["rssi"] = g_rs.lastHbRssi;
  }
  frame["up"] = (uint32_t)((millis() - g_rs.bootMs) / 1000);
  frame["gw_seen"] = g_rs.gwSeen ? 1 : 0;

  char buf[ESPNOW_FRAME_MAX + 1];
  size_t len = serializeJson(frame, buf, sizeof(buf));
  if (len == 0 || len >= sizeof(buf)) {
    Serial.println("reading frame too large — dropping");
    return;
  }
  int gwIdx = -1;
  if (espnow.sendWalk(buf, &gwIdx)) {
    g_rs.gwSeen = true;
  }
}

void Runtime::sendHandover(const uint8_t mac[6]) {
  // §6.2: the handover is what lets the gateway open the proxy AS us.
  // Sent to the gateway that advertised ho:0 (it lost/never got our
  // credentials — e.g. a standby gateway after failover).
  StaticJsonDocument<384> ho;
  ho["v"] = ESPNOW_FRAME_VERSION;
  ho["seq"] = ++g_rs.seq;
  ho["uid"] = devBuf;
  ho["type"] = "handover";
  ho["cust"] = custBuf;
  ho["mqtt_user"] = mqttUserBuf;
  ho["mqtt_pass"] = mqttPassBuf;
  char buf[ESPNOW_FRAME_MAX + 1];
  size_t len = serializeJson(ho, buf, sizeof(buf));
  if (len == 0 || len >= sizeof(buf)) {
    Serial.println("handover frame too large — creds may be at limit");
    return;
  }
  Serial.print("handover: sending credentials to gateway ... ");
  Serial.println(espnow.sendTo(mac, buf) ? "ok" : "FAIL");
}

void Runtime::applyCommand(uint32_t cmdId, const char* action, int relay) {
  // Replay guard AND multi-gateway failover dedup in one check (§7.4,
  // §8.4): anchored to the backend's own monotonic command_log_id.
  if (cmdId <= g_rs.highestCmdIdSeen) {
    Serial.printf("cmd %u: duplicate/old — dropped\n", (unsigned)cmdId);
    return;
  }

  if (!powerAllowsRelays(_policy)) {
    // sleep_tx refuses relay work (§2). Record the id so a retried
    // frame stays deduplicated, but do not ack it — the backend keeps
    // the command pending, which is the visible "refused" state.
    g_rs.highestCmdIdSeen = cmdId;
    Serial.printf("cmd %u: refused (sleep_tx carries no relays)\n", (unsigned)cmdId);
    return;
  }

  bool badRelay = false;
  if (strcmp(action, "switch_on") == 0) {
    if (relay >= 1 && relay <= RELAY_COUNT) relays.setChannel(relay - 1, true);
    else if (relay == 0) relays.setAll(true);
    else badRelay = true;
  } else if (strcmp(action, "switch_off") == 0) {
    if (relay >= 1 && relay <= RELAY_COUNT) relays.setChannel(relay - 1, false);
    else if (relay == 0) relays.setAll(false);
    else badRelay = true;
  } else if (strcmp(action, "switch_all_on") == 0) {
    relays.setAll(true);
  } else if (strcmp(action, "switch_all_off") == 0) {
    relays.setAll(false);
  } else {
    Serial.printf("cmd %u: unknown action '%s'\n", (unsigned)cmdId, action);
    g_rs.highestCmdIdSeen = cmdId;
    return;
  }

  if (badRelay) {
    Serial.printf("cmd %u: relay out of range %d\n", (unsigned)cmdId, relay);
    return;
  }

  g_rs.highestCmdIdSeen = cmdId;
  g_rs.relaysChanged = true;
  Serial.printf("cmd %u (%s relay %d) applied; acking with reading\n",
                (unsigned)cmdId, action, relay);
  sendReading(true);
}

void Runtime::processFrame(const EspNowRxItem& item) {
  StaticJsonDocument<384> doc;
  if (deserializeJson(doc, item.json)) {
    Serial.println("frame: json parse error (wrong LMK?)");
    return;
  }
  const char* type = doc["type"] | "";

  // §7.1 tolerance: a frame with a cmd_id but NO type is still a
  // command (gateway builds before the type field existed sent
  // exactly that, and they decrypt+parse fine — dropping them for
  // the missing field silently killed the whole downlink on the
  // bench 2026-10-06).
  if (type[0] == 0) {
    uint32_t cmdId = doc["cmd_id"] | 0;
    if (cmdId > 0) {
      applyCommand(cmdId, doc["action"] | "", doc["relay"] | 0);
    } else {
      Serial.println("frame: unknown type ''");
    }
    return;
  }

  if (strcmp(type, "hb") == 0) {
    g_rs.lastGwFrameMs = millis();
    g_rs.haveGwFrameEver = true;
    g_rs.lastHbRssi = item.rssi;
    g_rs.haveHbRssi = true;

    // Which gateway answered? For hysteresis and the handover reply.
    int idx = -1;
    for (int i = 0; i < espnow.gwCount(); i++) {
      if (memcmp(espnow.gwMac(i), item.mac, 6) == 0) { idx = i; break; }
    }

    // ho:0 → that gateway holds no credentials of ours: hand them
    // over (§6.2, and §8.6 rotation recovery).
    if ((doc["ho"] | 0) == 0 && haveMqttCreds()) {
      sendHandover(item.mac);
    }

    if (!g_rs.profileAlwaysOn || idx < 0) return;

    if (idx == g_rs.activeGw) {
      g_rs.hbMissActive = 0;
    } else if (idx < g_rs.activeGw) {
      // Better-ranked gateway is alive: come home after M good
      // beats (§8.2 hysteresis — no flapping on a marginal link).
      g_rs.hbConsecBetter++;
      if (g_rs.hbConsecBetter >= HB_RETURN_COUNT) {
        Serial.printf("gateway failover: returning to gw.%d\n", idx);
        g_rs.activeGw = (uint8_t)idx;
        g_rs.hbConsecBetter = 0;
        g_rs.hbMissActive = 0;
      }
    }
  } else if (strcmp(type, "cmd") == 0) {
    uint32_t cmdId = doc["cmd_id"] | 0;
    if (cmdId == 0) return;
    applyCommand(cmdId, doc["action"] | "", doc["relay"] | 0);
  } else if (strcmp(type, "cfg") == 0) {
    // Config-on-wake (§7.6): stored, so the next wake follows it —
    // and for always_on the reading cadence follows immediately.
    int32_t interval = doc["interval_s"] | 0;
    if (interval >= 10 && interval <= 86400) {
      nvs.putInt(NVS_KEY_WAKE_INTERVAL, interval);
      _policy.wakeIntervalS = interval;
      g_rs.readingPeriodMs = (uint32_t)interval * 1000UL;
      Serial.printf("cfg: wake interval -> %lds\n", (long)interval);
    }
    if (doc["adapt"].is<int>()) {
      int32_t adapt = doc["adapt"] | 0;
      nvs.putInt(NVS_KEY_ADAPT, adapt ? 1 : 0);
      _policy.adapt = adapt ? 1 : 0;
    }
    if (doc["adapt_low_v"].is<float>()) {
      float lv = doc["adapt_low_v"] | 0.0f;
      if (lv > 1.0f && lv < 20.0f) {
        nvs.putInt(NVS_KEY_ADAPT_LOWV, (int32_t)(lv * 1000.0f));
        _policy.adaptLowV = lv;
        Serial.printf("cfg: adapt_low_v -> %.2f V\n", lv);
      }
    }
  } else {
    Serial.printf("frame: unknown type '%s'\n", type);
  }
}

void Runtime::drainEspNow() {
  EspNowRxItem item;
  while (espnow.drain(&item)) {
    processFrame(item);
  }
}

void Runtime::failSafeCheck() {
  if (!g_rs.failSafe || !powerAllowsRelays(_policy)) return;
  if (!relays.anyOn()) return;
  // Same semantics as MQTT loss on a direct device — but keyed on
  // the RADIO path: an internet outage does NOT trip it, because the
  // gateway keeps heartbeating through the tunnel's absence (§7.5).
  if (millis() - g_rs.lastGwFrameMs > FAIL_SAFE_TIMEOUT_MS) {
    Serial.printf("fail-safe: no gateway frame for %lus, all relays OFF\n",
                  (unsigned long)(FAIL_SAFE_TIMEOUT_MS / 1000));
    relays.setAll(false);
    g_rs.failSafeTriggered = true;
    g_rs.relaysChanged = true;
  }
}

void Runtime::tick() {
  if (!_started) {
    // Bench hold: retry the bootstrap quietly until it succeeds.
    if (_benchHold && millis() - _lastBootstrapAttemptMs > BOOTSTRAP_RETRY_MS) {
      _lastBootstrapAttemptMs = millis();
      if (benchBootstrap()) {
        _benchHold = false;
        completeInit();  // may deep-sleep immediately (sleep_tx)
      }
    }
    return;
  }

  drainEspNow();

  if (g_rs.forceCycle) {
    g_rs.forceCycle = false;
    if (g_rs.profileAlwaysOn) {
      sendReading(powerAllowsRelays(_policy));
      g_rs.relaysChanged = false;
    } else {
      Serial.println("wake: bench cycle (no sleep — bench hold)");
      // One dry cycle minus the sleep: hello, window, reading.
      sendHello();
      unsigned long deadline = millis() + RX_WINDOW_MS;
      while (millis() < deadline) { drainEspNow(); delay(2); }
      sendReading(false);
    }
  }

  if (!g_rs.profileAlwaysOn) {
    return;  // sleep_tx only reaches here from the bench-hold path
  }

  unsigned long now = millis();
  if (now - _lastReadingMs >= g_rs.readingPeriodMs) {
    _lastReadingMs = now;
    sendReading(powerAllowsRelays(_policy));
    g_rs.relaysChanged = false;

    // Heartbeat miss accounting for the active gateway (§8.2 N): no
    // beat for one full period counts as one miss.
    if (now - g_rs.lastGwFrameMs > g_rs.readingPeriodMs) {
      if (g_rs.hbMissActive < 250) g_rs.hbMissActive++;
      if (g_rs.hbMissActive >= HB_MISS_LIMIT && espnow.gwCount() > 1) {
        uint8_t next = (uint8_t)((g_rs.activeGw + 1) % espnow.gwCount());
        Serial.printf("gateway failover: gw.%d silent, trying gw.%d\n",
                      g_rs.activeGw, next);
        g_rs.activeGw = next;
        g_rs.hbMissActive = 0;
        g_rs.hbConsecBetter = 0;
      }
    }
  }

  failSafeCheck();

  if (g_rs.relaysChanged) {
    sendReading(true);
    g_rs.relaysChanged = false;
  }
}
