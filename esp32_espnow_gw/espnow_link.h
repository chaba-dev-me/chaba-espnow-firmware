#pragma once

#include <Arduino.h>
#include <esp_now.h>
#include "config.h"

// One received frame, drained from the ring buffer by the main loop.
struct EspNowRxItem {
  uint8_t mac[6];
  char json[ESPNOW_FRAME_MAX + 1];
  int8_t rssi;  // as observed by OUR radio (the gateway overwrites
                // the remote's own rssi with this before publishing)
};

// ESP-NOW transport: peer registration with per-peer LMK encryption,
// a receive ring filled by the (short) RX callback, and serialized
// sends paced by the send callback. All sends go through sendPaced()
// so only one frame is ever in flight on the radio — that is the
// forwarder's pacing primitive (spec §7.2).
class EspNowLink {
public:
  bool begin();
  // Registers an encrypted unicast peer. Adding an already-known MAC
  // is a no-op success (IDF returns ESP_ERR_ESPNOW_EXIST).
  bool addPeer(const uint8_t mac[6], const uint8_t lmk[16]);
  // Blocking send; waits for the MAC-layer send callback (bounded by
  // ESPNOW_SEND_TIMEOUT_MS). Returns true only on ESP_NOW_SEND_SUCCESS.
  // Sending to an unregistered MAC fails with a logged esp_now error.
  bool sendPaced(const uint8_t mac[6], const char* json);
  // Pop one received frame. Returns false when the ring is empty.
  bool drain(EspNowRxItem* out);
  // "aa:bb:cc:dd:ee:ff" / "aabbccddeeff" → 6 bytes. False on garbage.
  static bool parseMac(const char* text, uint8_t out[6]);
  static void formatMac(const uint8_t mac[6], char* out, size_t outLen);
};

extern EspNowLink espnow;
