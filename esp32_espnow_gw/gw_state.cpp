#include "gw_state.h"
#include "config.h"
#include "proxy.h"
#include "espnow_link.h"

extern ProxyMgr proxies;

static unsigned long s_lastHeartbeatMs = 0;

void gwStateTick() {
  unsigned long now = millis();
  if (s_lastHeartbeatMs != 0 && now - s_lastHeartbeatMs < HEARTBEAT_PERIOD_MS) {
    return;
  }
  s_lastHeartbeatMs = now;
  for (int n = 0; n < ESPNOW_MAX_PEERS; n++) {
    PeerSlot& slot = proxies.slots[n];
    if (slot.provisioned) {
      proxies.heartbeat(slot);
    }
  }
}

void appendGwBlock(JsonDocument& doc) {
  JsonObject gw = doc.createNestedObject("gw");
  JsonArray peers = gw.createNestedArray("peers");
  unsigned long now = millis();
  for (int n = 0; n < ESPNOW_MAX_PEERS; n++) {
    PeerSlot& slot = proxies.slots[n];
    if (!slot.provisioned) continue;
    JsonObject p = peers.createNestedObject();
    char macStr[18];
    EspNowLink::formatMac(slot.mac, macStr, sizeof(macStr));
    p["mac"] = macStr;
    p["uid"] = slot.uid[0] ? slot.uid : "-";
    uint32_t rxAge = slot.lastRxMs ? (now - slot.lastRxMs) / 1000 : 0;
    p["rx_s"] = (int)rxAge;
    p["tx"] = !slot.lastDeliverKnown ? "na"
              : (slot.lastDeliverOk ? "ok" : "fail");
    p["proxy"] = !slot.credsValid ? "off"
                 : (slot.client && slot.client->connected() ? "up" : "down");
  }
}
