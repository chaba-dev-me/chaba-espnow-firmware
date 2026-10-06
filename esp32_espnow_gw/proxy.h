#pragma once

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>
#include "config.h"
#include "espnow_link.h"

// Commanded/known relay bookkeeping for one remote (relay-remotes
// only; sensor remotes leave hasRelayState false).
struct RemoteCmdCache {
  bool valid = false;
  uint32_t cmdId = 0;
  char action[24] = "";
  int relay = 0;  // 1-based, 0 = all
};

// One paired remote = one peer slot = (at most) one proxy connection.
// mac + secret are bench-provisioned (NVS); uid/cust/mqtt credentials
// arrive over the air (hello + handover) and persist to NVS — the
// honest cost in spec §8.6. RAM cost when active: this struct plus a
// heap WiFiClient + PubSubClient.
struct PeerSlot {
  bool provisioned = false;
  uint8_t mac[6] = {0};
  char secret[97] = "";  // remote enroll secret (HKDF IKM)

  char uid[40] = "";
  char cust[40] = "";
  char mqttUser[64] = "";
  char mqttPass[96] = "";
  bool credsValid = false;

  // Config-on-wake values pushed to the remote on hello/reading
  // replies (§7.6). interval 0 = not configured.
  int32_t cfgInterval = 0;
  int32_t cfgAdapt = -1;
  float cfgLowV = 0.0f;

  WiFiClient* wifi = nullptr;
  PubSubClient* client = nullptr;
  char cmdTopic[128] = "";
  char stateTopic[128] = "";
  unsigned long lastConnectAttemptMs = 0;

  RemoteCmdCache cache;
  bool hasRelayState = false;
  bool knownRelays[RELAY_COUNT] = {false};

  // Forwarder: single in-flight slot, paced by espnow.sendPaced().
  // Depth 1 is enough: the backend's 60 s reap loop re-publishes
  // anything we drop (§7.3).
  char pendingCmd[ESPNOW_FRAME_MAX + 1] = "";
  bool hasPending = false;
  int retries = 0;

  // Health (surfaced in the `gw` state block)
  unsigned long lastRxMs = 0;
  bool lastDeliverOk = true;
  bool lastDeliverKnown = false;
};

class ProxyMgr {
public:
  void begin();      // load peer slots from NVS, register ESP-NOW peers
  void applySlot(int n);  // (re)load one slot after CLI changes
  void tick();       // proxy MQTT keepsalive + forwarder pump
  void onFrame(const EspNowRxItem& f);  // hello/handover/reading dispatch
  // Plain periodic heartbeat (fail-safe clock + ho flag + cfg carrier)
  void heartbeat(PeerSlot& slot);
  bool deliver(PeerSlot& slot, const char* json);  // one paced attempt
  void deliverRetried(PeerSlot& slot, const char* json);  // §7.2: 3× fast
  void enqueueCommand(PeerSlot& slot, uint32_t cmdId,
                      const char* action, int relay);

  PeerSlot slots[ESPNOW_MAX_PEERS];

private:
  int slotByMac(const uint8_t mac[6]);  // -1 if not provisioned
  void buildTopics(PeerSlot& slot);
  void ensureConnected(PeerSlot& slot);
  void onMqttMessage(PeerSlot& slot, char* topic, byte* payload,
                     unsigned int length);
  void replyHeartbeat(PeerSlot& slot, bool toHello,
                      uint32_t remoteLastCmdId);
  void pushConfig(PeerSlot& slot);
  void publishRemoteState(PeerSlot& slot, JsonObject frame,
                          int rssiObserved);
  void loadSlot(int n);
};

extern ProxyMgr proxies;
