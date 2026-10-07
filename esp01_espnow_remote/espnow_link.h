#pragma once

#include <Arduino.h>
#include "config.h"
#include "state.h"

// One received frame (from any registered gateway), drained by the
// main loop.
struct EspNowRxItem {
  uint8_t mac[6];
  char json[ESPNOW_FRAME_MAX + 1];
  int8_t rssi;  // ESP8266 recv cb carries no rssi — always 0
};

// Remote-side ESP-NOW transport (ESP8266 flavour). Peers are the
// ORDERED gateway list (NVS gw.0.mac, gw.1.mac, …); each gets an LMK
// derived from THIS node's own enroll secret and that gateway's MAC
// (spec §4):
//
//   LMK(self, gw_i) = HKDF-SHA256(ikm=enroll.secret,
//                                 salt=MAC(gw_i),
//                                 info="chaba-espnow-v1", L=16)
//
// ESP8266 API differences from the ESP32 (SDK espnow.h):
//   - peer added via esp_now_add_peer(mac, role, channel, key, 16),
//     key = the 16-byte LMK (NULL would mean plaintext);
//   - shared PMK set once via esp_now_set_kok() before peers;
//   - callbacks: recv (mac, data, len), send (mac, status).
// A remote never associates to an AP in the field, so it pins the
// site channel explicitly with wifi_set_channel (§3.1). All sends
// walk the list through sendWalk(); TX-done is the send-callback,
// never a timer.
class RemoteLink {
public:
  bool begin(bool pinChannel);
  // Load gw.N.mac slots from the settings store, derive + register
  // peers. Returns the count of usable gateways (0 = nothing to talk
  // to).
  int loadGateways(const char* enrollSecret);
  // Try gateways: the currently-active one first, then list order.
  // On success returns true, sets *gwIdx, and updates the active
  // index; on total failure returns false.
  bool sendWalk(const char* json, int* gwIdx);
  // Addressed send (no walk) — the handover goes to the ONE gateway
  // whose heartbeat advertised ho:0.
  bool sendTo(const uint8_t mac[6], const char* json);
  bool drain(EspNowRxItem* out);
  int gwCount() const { return _gwCount; }
  const uint8_t* gwMac(int i) const;

  static bool parseMac(const char* text, uint8_t out[6]);
  static void formatMac(const uint8_t mac[6], char* out, size_t outLen);

private:
  uint8_t _gws[ESPNOW_MAX_GWS][6];
  int _gwCount = 0;
  bool sendPaced(const uint8_t mac[6], const char* json);
};

extern RemoteLink espnow;
