#include "proxy.h"
#include "config.h"
#include "nvs_store.h"
#include "hkdf.h"
#include <WiFi.h>

extern NvsStore nvs;
extern const char* runtimeMqttHost();
extern uint16_t runtimeMqttPort();

ProxyMgr proxies;

static uint32_t s_txSeq = 1;
static uint32_t nextSeq() { return s_txSeq++; }

// Gateway's own STA MAC — the HKDF salt for every LMK (spec §4).
static uint8_t s_gwMac[6] = {0};

// NVS key helper: "peer.<n>.<field>" (all ≤ 15 chars, NVS cap)
static void peerKey(int n, const char* field, char* buf, size_t len) {
  snprintf(buf, len, NVS_KEY_PEER_FMT, n, field);
}

// ----------------------------------------------------------------
// Slot loading (bench CLI → NVS → RAM + ESP-NOW peer registration)
// ----------------------------------------------------------------

void ProxyMgr::loadSlot(int n) {
  if (n < 0 || n >= ESPNOW_MAX_PEERS) return;
  PeerSlot& slot = slots[n];

  uint8_t oldMac[6];
  memcpy(oldMac, slot.mac, 6);
  bool wasProvisioned = slot.provisioned;

  char key[24];
  char buf[128];

  slot = PeerSlot();  // reset

  peerKey(n, PEER_FIELD_MAC, key, sizeof(key));
  if (nvs.getCStr(key, buf, sizeof(buf)) && strlen(buf) > 0) {
    if (EspNowLink::parseMac(buf, slot.mac)) {
      slot.provisioned = true;
    }
  }
  peerKey(n, PEER_FIELD_SECRET, key, sizeof(key));
  if (nvs.getCStr(key, buf, sizeof(buf))) {
    strlcpy(slot.secret, buf, sizeof(slot.secret));
  }
  peerKey(n, PEER_FIELD_UID, key, sizeof(key));
  if (nvs.getCStr(key, buf, sizeof(buf))) {
    strlcpy(slot.uid, buf, sizeof(slot.uid));
  }
  peerKey(n, PEER_FIELD_CUST, key, sizeof(key));
  if (nvs.getCStr(key, buf, sizeof(buf))) {
    strlcpy(slot.cust, buf, sizeof(slot.cust));
  }
  peerKey(n, PEER_FIELD_USER, key, sizeof(key));
  if (nvs.getCStr(key, buf, sizeof(buf))) {
    strlcpy(slot.mqttUser, buf, sizeof(slot.mqttUser));
  }
  peerKey(n, PEER_FIELD_PASS, key, sizeof(key));
  if (nvs.getCStr(key, buf, sizeof(buf))) {
    strlcpy(slot.mqttPass, buf, sizeof(slot.mqttPass));
  }
  peerKey(n, PEER_FIELD_INTERVAL, key, sizeof(key));
  slot.cfgInterval = nvs.getInt(key, 0);
  peerKey(n, PEER_FIELD_ADAPT, key, sizeof(key));
  slot.cfgAdapt = nvs.getInt(key, -1);
  peerKey(n, PEER_FIELD_LOWV, key, sizeof(key));
  // float through NVS as millivolts (i32) to dodge float plumbing
  slot.cfgLowV = nvs.getInt(key, 0) / 1000.0f;

  slot.credsValid =
      slot.uid[0] && slot.cust[0] && slot.mqttUser[0] && slot.mqttPass[0];

  if (wasProvisioned) {
    esp_now_del_peer(oldMac);  // idempotent; re-added below if still ours
  }
  if (slot.provisioned) {
    uint8_t lmk[16];
    if (espnow_lmk(slot.secret, s_gwMac, lmk)) {
      espnow.addPeer(slot.mac, lmk);
    } else {
      Serial.printf("peer %d: bad enroll secret, ESP-NOW key not set\n", n);
    }
  }
  if (slot.credsValid) {
    buildTopics(slot);
  }
}

void ProxyMgr::applySlot(int n) { loadSlot(n); }

void ProxyMgr::begin() {
  // espnow.begin() has put the radio in STA mode, so the MAC is
  // queryable. MACs print lowercase; parseMac is case-tolerant.
  String macStr = WiFi.macAddress();
  if (!EspNowLink::parseMac(macStr.c_str(), s_gwMac)) {
    Serial.println("proxy: could not read gateway MAC — LMKs wrong until fixed");
  } else {
    Serial.print("proxy: gateway MAC (HKDF salt) = ");
    Serial.println(macStr);
  }
  for (int n = 0; n < ESPNOW_MAX_PEERS; n++) {
    loadSlot(n);
  }
}

void ProxyMgr::buildTopics(PeerSlot& slot) {
  snprintf(slot.cmdTopic, sizeof(slot.cmdTopic), TOPIC_CMD_FMT,
           slot.cust, slot.uid);
  snprintf(slot.stateTopic, sizeof(slot.stateTopic), TOPIC_STATE_FMT,
           slot.cust, slot.uid);
}

// ----------------------------------------------------------------
// MQTT proxy connection (one PubSubClient per remote, AS the remote)
// ----------------------------------------------------------------

void ProxyMgr::ensureConnected(PeerSlot& slot) {
  if (!slot.credsValid || !WiFi.isConnected()) return;

  if (slot.client == nullptr) {
    slot.wifi = new WiFiClient();
    slot.client = new PubSubClient(*slot.wifi);
    // Credentials live in the slot (persistent storage), never on the
    // stack: PubSubClient::connect() keeps only POINTERS to user/pass
    // and dereferences them on every reconnect.
    slot.client->setServer(runtimeMqttHost(), runtimeMqttPort());
    slot.client->setBufferSize(512);
    slot.client->setKeepAlive(MQTT_KEEPALIVE_S);
    slot.client->setSocketTimeout(5);
    slot.client->setCallback([this, &slot](char* t, byte* p, unsigned int l) {
      this->onMqttMessage(slot, t, p, l);
    });
  }

  if (slot.client->connected()) {
    slot.client->loop();
    return;
  }

  unsigned long now = millis();
  if (now - slot.lastConnectAttemptMs < 5000) return;
  slot.lastConnectAttemptMs = now;

  Serial.printf("proxy: connecting AS %s ... ", slot.uid);
  if (slot.client->connect(slot.uid, slot.mqttUser, slot.mqttPass)) {
    Serial.println("OK");
    if (slot.client->subscribe(slot.cmdTopic, MQTT_QOS)) {
      Serial.printf("proxy: subscribed %s (retained cmd arrives here)\n",
                    slot.cmdTopic);
    }
  } else {
    int8_t st = (int8_t)slot.client->state();
    Serial.printf("FAIL rc=%d\n", st);
  }
}

void ProxyMgr::onMqttMessage(PeerSlot& slot, char* topic, byte* payload,
                             unsigned int length) {
  char buf[400];
  unsigned int n = length;
  if (n >= sizeof(buf)) n = sizeof(buf) - 1;
  memcpy(buf, payload, n);
  buf[n] = 0;

  StaticJsonDocument<384> doc;
  if (deserializeJson(doc, buf)) {
    Serial.println("proxy: cmd json parse error");
    return;
  }
  uint32_t cmdId = doc["command_log_id"] | 0;
  const char* action = doc["action"] | "";
  int relay = doc["relay"] | 0;
  if (cmdId == 0 || action[0] == 0) {
    Serial.println("proxy: cmd missing command_log_id/action");
    return;
  }
  enqueueCommand(slot, cmdId, action, relay);
}

void ProxyMgr::enqueueCommand(PeerSlot& slot, uint32_t cmdId,
                              const char* action, int relay) {
  slot.cache.valid = true;
  slot.cache.cmdId = cmdId;
  strlcpy(slot.cache.action, action, sizeof(slot.cache.action));
  slot.cache.relay = relay;

  char frame[ESPNOW_FRAME_MAX + 1];
  if (relay > 0) {
    snprintf(frame, sizeof(frame),
             "{\"v\":%d,\"seq\":%u,\"type\":\"cmd\",\"cmd_id\":%u,\"action\":\"%s\",\"relay\":%d}",
             ESPNOW_FRAME_VERSION, (unsigned)nextSeq(),
             (unsigned)cmdId, action, relay);
  } else {
    snprintf(frame, sizeof(frame),
             "{\"v\":%d,\"seq\":%u,\"type\":\"cmd\",\"cmd_id\":%u,\"action\":\"%s\"}",
             ESPNOW_FRAME_VERSION, (unsigned)nextSeq(),
             (unsigned)cmdId, action);
  }
  strlcpy(slot.pendingCmd, frame, sizeof(slot.pendingCmd));
  slot.hasPending = true;
  slot.retries = 0;
  Serial.printf("proxy %s: cmd %u (%s) queued for forward\n",
                slot.uid, (unsigned)cmdId, action);
}

// ----------------------------------------------------------------
// ESP-NOW frame dispatch (uplink)
// ----------------------------------------------------------------

void ProxyMgr::onFrame(const EspNowRxItem& f) {
  int n = slotByMac(f.mac);
  if (n < 0) return;  // unprovisioned MAC — pairing is the bench's job
  PeerSlot& slot = slots[n];

  StaticJsonDocument<384> doc;
  if (deserializeJson(doc, f.json)) {
    Serial.printf("peer %d: frame json parse error (bad key or corrupt)\n", n);
    return;
  }

  slot.lastRxMs = millis();
  uint32_t remoteLastCmdId = doc["last_cmd_id"] | 0;
  const char* type = doc["type"] | "";

  if (strcmp(type, "hello") == 0) {
    const char* uid = doc["uid"] | "";
    if (uid[0] && strcmp(uid, slot.uid) != 0) {
      strlcpy(slot.uid, uid, sizeof(slot.uid));
      char key[24];
      peerKey(n, PEER_FIELD_UID, key, sizeof(key));
      nvs.putCStr(key, slot.uid);
      if (slot.cust[0]) buildTopics(slot);
      Serial.printf("peer %d: hello from %s\n", n, slot.uid);
    }
    replyHeartbeat(slot, true, remoteLastCmdId);
  } else if (strcmp(type, "handover") == 0) {
    // Normally the hello introduced the uid; learn it here too so an
    // always_on remote that never sent a boot hello can't livelock
    // (credsValid requires uid + cust + user + pass).
    const char* uid = doc["uid"] | "";
    if (uid[0] && strcmp(uid, slot.uid) != 0) {
      strlcpy(slot.uid, uid, sizeof(slot.uid));
      char ukey[24];
      peerKey(n, PEER_FIELD_UID, ukey, sizeof(ukey));
      nvs.putCStr(ukey, slot.uid);
    }
    const char* cust = doc["cust"] | "";
    const char* user = doc["mqtt_user"] | "";
    const char* pass = doc["mqtt_pass"] | "";
    if (cust[0]) strlcpy(slot.cust, cust, sizeof(slot.cust));
    if (user[0]) strlcpy(slot.mqttUser, user, sizeof(slot.mqttUser));
    if (pass[0]) strlcpy(slot.mqttPass, pass, sizeof(slot.mqttPass));
    slot.credsValid =
        slot.uid[0] && slot.cust[0] && slot.mqttUser[0] && slot.mqttPass[0];
    char key[24];
    peerKey(n, PEER_FIELD_CUST, key, sizeof(key));
    nvs.putCStr(key, slot.cust);
    peerKey(n, PEER_FIELD_USER, key, sizeof(key));
    nvs.putCStr(key, slot.mqttUser);
    peerKey(n, PEER_FIELD_PASS, key, sizeof(key));
    nvs.putCStr(key, slot.mqttPass);
    Serial.printf("peer %d: handover stored (creds %s)\n", n,
                  slot.credsValid ? "complete" : "incomplete");
    if (slot.credsValid) buildTopics(slot);
    replyHeartbeat(slot, false, remoteLastCmdId);
  } else if (strcmp(type, "reading") == 0) {
    if (doc["relays"].is<JsonObject>()) {
      slot.hasRelayState = true;
      for (int i = 0; i < RELAY_COUNT; i++) {
        char rkey[16];
        snprintf(rkey, sizeof(rkey), "relay_%d", i);
        slot.knownRelays[i] =
            strcmp(doc["relays"][rkey] | "", "on") == 0;
      }
    }
    publishRemoteState(slot, doc.as<JsonObject>(), f.rssi);
    replyHeartbeat(slot, false, remoteLastCmdId);
  } else {
    Serial.printf("peer %d: unknown frame type '%s'\n", n, type);
  }
}

// Gateway → cloud state publish AS the remote (spec §6.2/§6.3): the
// topic is the identity, and rssi is overwritten with what WE heard.
void ProxyMgr::publishRemoteState(PeerSlot& slot, JsonObject frame,
                                  int rssiObserved) {
  if (!slot.credsValid || slot.client == nullptr ||
      !slot.client->connected()) {
    return;  // no proxy yet — readings resume once handover completes
  }
  bool isRelay = frame["relays"].is<JsonObject>();
  StaticJsonDocument<512> out;
  out["device_type"] = isRelay ? "relay" : "sensor";
  out["link"] = "espnow";
  if (isRelay) {
    JsonObject relays = out.createNestedObject("relays_state");
    JsonObject srcRelays = frame["relays"];
    for (JsonPair kv : srcRelays) {
      relays[kv.key()] = kv.value();
    }
  }
  if (frame["sensors"].is<JsonObject>()) {
    JsonObject sensors = out.createNestedObject("sensors");
    JsonObject srcSensors = frame["sensors"];
    for (JsonPair kv : srcSensors) {
      sensors[kv.key()] = kv.value();
    }
  }
  uint32_t cmdId = frame["cmd_id"] | 0;
  if (cmdId > 0) out["command_log_id"] = cmdId;
  out["rssi"] = rssiObserved;
  out["uptime_s"] = frame["up"] | 0;
  out["wifi_status"] = "connected";
  out["mqtt_status"] = "connected";

  char buf[512];
  size_t len = serializeJson(out, buf, sizeof(buf));
  if (len == 0 || len >= sizeof(buf)) return;
  slot.client->publish(slot.stateTopic, (const uint8_t*)buf, len, false);
}

// ----------------------------------------------------------------
// Gateway → remote replies (heartbeat / config / reconciliation)
// ----------------------------------------------------------------

void ProxyMgr::heartbeat(PeerSlot& slot) {
  replyHeartbeat(slot, false, 0);
}

void ProxyMgr::replyHeartbeat(PeerSlot& slot, bool toHello,
                              uint32_t remoteLastCmdId) {
  StaticJsonDocument<256> hb;
  hb["v"] = ESPNOW_FRAME_VERSION;
  hb["type"] = "hb";
  hb["cmd_id"] = slot.cache.valid ? slot.cache.cmdId : 0;
  hb["ho"] = slot.credsValid ? 1 : 0;
  // Hello reconciliation (§7.4): remote behind → send the cached
  // command; equal → echo our last-known relay state so the remote
  // can fix drift after its own reboot. last_cmd_id 0 (fresh boot,
  // field absent) is maximally behind — still send it.
  if (toHello && slot.cache.valid) {
    if (remoteLastCmdId < slot.cache.cmdId) {
      char frame[ESPNOW_FRAME_MAX + 1];
      if (slot.cache.relay > 0) {
        snprintf(frame, sizeof(frame),
                 "{\"v\":%d,\"seq\":%u,\"type\":\"cmd\",\"cmd_id\":%u,\"action\":\"%s\",\"relay\":%d}",
                 ESPNOW_FRAME_VERSION, (unsigned)nextSeq(),
                 (unsigned)slot.cache.cmdId, slot.cache.action,
                 slot.cache.relay);
      } else {
        snprintf(frame, sizeof(frame),
                 "{\"v\":%d,\"seq\":%u,\"type\":\"cmd\",\"cmd_id\":%u,\"action\":\"%s\"}",
                 ESPNOW_FRAME_VERSION, (unsigned)nextSeq(),
                 (unsigned)slot.cache.cmdId, slot.cache.action);
      }
      deliverRetried(slot, frame);
      pushConfig(slot);
      return;  // the command frame carries the state the remote needs
    }
    if (remoteLastCmdId == slot.cache.cmdId && slot.hasRelayState) {
      JsonObject rel = hb.createNestedObject("rel");
      for (int i = 0; i < RELAY_COUNT; i++) {
        char rkey[16];
        snprintf(rkey, sizeof(rkey), "relay_%d", i);
        rel[rkey] = slot.knownRelays[i] ? "on" : "off";
      }
    }
  }
  char buf[256];
  size_t len = serializeJson(hb, buf, sizeof(buf));
  if (len > 0 && len < sizeof(buf)) deliver(slot, buf);

  if (slot.cfgInterval > 0) pushConfig(slot);
}

// Config-on-wake (§7.6): piggybacked on hello/reading replies; the
// remote stores it in NVS and follows it from its next wake on.
void ProxyMgr::pushConfig(PeerSlot& slot) {
  if (slot.cfgInterval <= 0) return;
  StaticJsonDocument<256> cfg;
  cfg["v"] = ESPNOW_FRAME_VERSION;
  cfg["type"] = "cfg";
  cfg["interval_s"] = slot.cfgInterval;
  cfg["adapt"] = slot.cfgAdapt > 0 ? 1 : 0;
  cfg["adapt_low_v"] = slot.cfgLowV;
  cfg["gw_epoch"] = 1;
  char buf[192];
  size_t len = serializeJson(cfg, buf, sizeof(buf));
  if (len > 0 && len < sizeof(buf)) deliver(slot, buf);
}

// ----------------------------------------------------------------
// Forwarder + tick
// ----------------------------------------------------------------

bool ProxyMgr::deliver(PeerSlot& slot, const char* json) {
  bool ok = espnow.sendPaced(slot.mac, json);
  slot.lastDeliverOk = ok;
  slot.lastDeliverKnown = true;
  return ok;
}

void ProxyMgr::deliverRetried(PeerSlot& slot, const char* json) {
  for (int attempt = 0; attempt < ESPNOW_CMD_RETRIES; attempt++) {
    if (deliver(slot, json)) return;
  }
}

void ProxyMgr::tick() {
  for (int n = 0; n < ESPNOW_MAX_PEERS; n++) {
    PeerSlot& slot = slots[n];
    if (!slot.provisioned) continue;
    ensureConnected(slot);
    if (!slot.hasPending) continue;
    if (slot.retries >= ESPNOW_CMD_RETRIES) {
      // Give up quietly: the backend's reap loop re-claims and
      // re-publishes the retained cmd within 60 s (§7.3).
      slot.hasPending = false;
      Serial.printf("proxy %s: cmd %u forward failed; reaper will retry\n",
                    slot.uid, (unsigned)slot.cache.cmdId);
      continue;
    }
    if (deliver(slot, slot.pendingCmd)) {
      slot.hasPending = false;
      // Deliberately NOT acking the backend here: a MAC ack is not a
      // relay flip. The remote's state publish (carrying
      // command_log_id) is the only ack the backend trusts (§7.2).
      Serial.printf("proxy %s: cmd %u delivered; waiting for remote ack\n",
                    slot.uid, (unsigned)slot.cache.cmdId);
    } else {
      slot.retries++;
    }
  }
}

int ProxyMgr::slotByMac(const uint8_t mac[6]) {
  for (int n = 0; n < ESPNOW_MAX_PEERS; n++) {
    if (slots[n].provisioned && memcmp(slots[n].mac, mac, 6) == 0) {
      return n;
    }
  }
  return -1;
}
