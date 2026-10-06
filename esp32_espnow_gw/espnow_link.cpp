#include "espnow_link.h"
#include <WiFi.h>
#include <esp_wifi.h>

EspNowLink espnow;

// RX ring + TX flags live at file scope: the ESP-NOW callbacks are
// plain functions registered with the IDF, not class members. They
// run in the WiFi task; the main loop is the only consumer, so plain
// volatile indices are enough (single producer, single consumer).
static EspNowRxItem s_ring[ESPNOW_RX_RING];
static volatile uint8_t s_head = 0;  // write index (RX callback)
static volatile uint8_t s_tail = 0;  // read index (main loop)
static volatile bool s_txDone = false;
static volatile esp_now_send_status_t s_txStatus = ESP_NOW_SEND_FAIL;

// Send callback — sets two flags, nothing else. Arduino core 3.x
// (IDF >= 5.3) passes esp_now_send_info_t; older cores pass the MAC
// directly. The gateway never acks on this callback: a MAC ack says
// the frame reached the radio, not that the relay flipped (§7.2).
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0)
static void onSent(const esp_now_send_info_t* info,
                   esp_now_send_status_t status) {
  (void)info;
  s_txStatus = status;
  s_txDone = true;
}
#else
static void onSent(const uint8_t* mac, esp_now_send_status_t status) {
  (void)mac;
  s_txStatus = status;
  s_txDone = true;
}
#endif

// Receive callback — house rule from the repeater's WDT step-machine:
// copy + flag, all real work happens in the main loop. Ring overflow
// silently drops the NEWEST frame of a burst; the 60 s heartbeat and
// the backend's reap loop republish everything that matters. Frames
// from unknown MACs are dropped here (peerIndex-style check): every
// peer is registered with encrypt=true, so plaintext injection from
// an unpaired sender never reaches the parser.
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
static void onRecv(const esp_now_recv_info_t* info,
                   const uint8_t* data, int len) {
#else
static void onRecv(const uint8_t* mac, const uint8_t* data, int len) {
  esp_now_recv_info_t localInfo = {};
  uint8_t src[6];
  memcpy(src, mac, 6);
  localInfo.src_addr = src;
  const esp_now_recv_info_t* info = &localInfo;
#endif
  if (len <= 0 || len > ESPNOW_FRAME_MAX) return;
  if (info->src_addr == NULL) return;
  bool known = esp_now_is_peer_exist(info->src_addr);
  if (!known) return;
  uint8_t nextHead = (s_head + 1) % ESPNOW_RX_RING;
  if (nextHead == s_tail) return;  // ring full — drop
  EspNowRxItem& slot = s_ring[s_head];
  memcpy(slot.mac, info->src_addr, 6);
  memcpy(slot.json, data, len);
  slot.json[len] = 0;
  slot.rssi = (info->rx_ctrl != NULL) ? info->rx_ctrl->rssi : 0;
  s_head = nextHead;
}

bool EspNowLink::begin() {
  WiFi.mode(WIFI_STA);
  esp_err_t err = esp_now_init();
  if (err != ESP_OK) {
    Serial.print("espnow: init failed: ");
    Serial.println(esp_err_to_name(err));
    return false;
  }
  esp_now_register_send_cb(onSent);
  esp_now_register_recv_cb(onRecv);
  Serial.println("espnow: initialised (STA role)");
  return true;
}

bool EspNowLink::addPeer(const uint8_t mac[6], const uint8_t lmk[16]) {
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, mac, 6);
  peer.channel = 0;  // 0 = current channel; the gateway never hops
                     // (its channel is the associated AP's, §3.1)
  peer.ifidx = WIFI_IF_STA;
  peer.encrypt = true;
  memcpy(peer.lmk, lmk, 16);
  esp_err_t err = esp_now_add_peer(&peer);
  if (err != ESP_OK && err != ESP_ERR_ESPNOW_EXIST) {
    Serial.print("espnow: add_peer failed: ");
    Serial.println(esp_err_to_name(err));
    return false;
  }
  return true;
}

bool EspNowLink::sendPaced(const uint8_t mac[6], const char* json) {
  size_t len = strlen(json);
  if (len == 0 || len > ESPNOW_FRAME_MAX) return false;

  s_txDone = false;
  s_txStatus = ESP_NOW_SEND_FAIL;
  esp_err_t err = esp_now_send(mac, (const uint8_t*)json, len);
  if (err != ESP_OK) {
    Serial.print("espnow: send error: ");
    Serial.println(esp_err_to_name(err));
    return false;
  }
  unsigned long deadline = millis() + ESPNOW_SEND_TIMEOUT_MS;
  while (!s_txDone && millis() < deadline) {
    delay(1);
  }
  return s_txDone && s_txStatus == ESP_NOW_SEND_SUCCESS;
}

bool EspNowLink::drain(EspNowRxItem* out) {
  if (s_tail == s_head) return false;
  *out = s_ring[s_tail];
  s_tail = (s_tail + 1) % ESPNOW_RX_RING;
  return true;
}

bool EspNowLink::parseMac(const char* text, uint8_t out[6]) {
  if (text == NULL) return false;
  uint8_t bytes[6];
  int n = 0;
  const char* p = text;
  while (*p && n < 6) {
    if (*p == ':' || *p == '-') { p++; continue; }
    if (!isxdigit((unsigned char)p[0]) || !isxdigit((unsigned char)p[1])) return false;
    char hex[3] = {p[0], p[1], 0};
    bytes[n++] = (uint8_t)strtoul(hex, NULL, 16);
    p += 2;
  }
  if (n != 6 || *p != 0) return false;
  memcpy(out, bytes, 6);
  return true;
}

void EspNowLink::formatMac(const uint8_t mac[6], char* out, size_t outLen) {
  snprintf(out, outLen, "%02x:%02x:%02x:%02x:%02x:%02x",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}
