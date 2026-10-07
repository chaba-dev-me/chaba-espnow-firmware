#include "espnow_link.h"
#include "nvs_store.h"
#include "hkdf.h"
#include <ESP8266WiFi.h>
#include <espnow.h>

extern NvsStore nvs;

// Per-device random Primary Master Key (red-team H3): replaces the
// legacy repo-constant ESPNOW_PMK (SHA256("chaba-espnow-v1-pmk")
// [0..15]) that shipped in this public repo. The PMK is never the
// effective frame key — every peer registers encrypt=true with a
// per-pair HKDF LMK — but a per-device random value means no shared,
// publicly-known constant survives in the source. Persisted via the
// same key-value store ("espnow.pmk", 32 hex chars) so it is stable
// across reboots.
static void chabaInstallPmk() {
  uint8_t pmk[16];
  char hex[33];
  bool have = nvs.getCStr("espnow.pmk", hex, sizeof(hex)) && strlen(hex) == 32;
  if (have) {
    for (int i = 0; i < 16; i++) {
      char b[3] = {hex[i * 2], hex[i * 2 + 1], 0};
      pmk[i] = (uint8_t)strtoul(b, NULL, 16);
    }
  } else {
    for (int i = 0; i < 16; i++) pmk[i] = (uint8_t)esp_random();
    for (int i = 0; i < 16; i++) snprintf(hex + i * 2, 3, "%02x", pmk[i]);
    nvs.putCStr("espnow.pmk", hex);
  }
  esp_now_set_kok(pmk, 16);
}

RemoteLink espnow;

static volatile bool s_txDone = false;
static volatile bool s_txOk = false;
static EspNowRxItem s_ring[ESPNOW_RX_RING];
static volatile uint8_t s_head = 0;
static volatile uint8_t s_tail = 0;

// SDK callback shapes (espnow.h): recv (mac, data, len) runs in the
// WiFi task context — keep it to the ring push, no Serial. The peer
// check drops frames from unregistered MACs (they'd be plaintext or
// foreign anyway — the LMK gate is the real filter; this keeps the
// ring clean).
static void onRecv(u8* mac, u8* data, u8 len) {
  if (len <= 0 || len > ESPNOW_FRAME_MAX) return;
  if (mac == NULL || !esp_now_is_peer_exist(mac)) return;
  uint8_t nextHead = (s_head + 1) % ESPNOW_RX_RING;
  if (nextHead == s_tail) return;
  EspNowRxItem& slot = s_ring[s_head];
  memcpy(slot.mac, mac, 6);
  memcpy(slot.json, data, len);
  slot.json[len] = 0;
  slot.rssi = 0;
  s_head = nextHead;
}

static void onSent(u8* mac, u8 status) {
  (void)mac;
  s_txOk = (status == 0);
  s_txDone = true;
}

bool RemoteLink::begin(bool pinChannel) {
  // STA role, never WiFi.begin() — a remote never associates in the
  // field. MAC from the driver: unlike the ESP32 (where the bench
  // OFF→STA cycle races the driver), the 8266 reports its MAC
  // reliably right after the opmode set.
  wifi_set_opmode_current(STATION_MODE);
  String macStr = WiFi.macAddress();
  Serial.printf("espnow: remote MAC = %s"
                "  (this is what 'set peer mac' on the gateway wants)\n",
                macStr.c_str());
  if (pinChannel) {
    int32_t ch = nvs.getInt(NVS_KEY_GW_CHANNEL, 0);
    if (ch >= 1 && ch <= 13) {
      if (wifi_set_channel((uint8)ch)) {
        Serial.printf("espnow: channel pinned to %d (get=%d)\n",
                      (int)ch, (int)wifi_get_channel());
      } else {
        Serial.println("espnow: pin channel FAILED");
      }
    } else {
      Serial.println("espnow: WARNING no gw.channel pinned — set 'set gw channel <n>' (spec §3)");
    }
  }
  if (esp_now_init() != 0) {
    Serial.println("espnow: init failed");
    return false;
  }
  chabaInstallPmk();
  esp_now_register_send_cb(onSent);
  esp_now_register_recv_cb(onRecv);
  Serial.println("espnow: initialised (remote role, esp8266)");
  return true;
}

int RemoteLink::loadGateways(const char* enrollSecret) {
  _gwCount = 0;
  if (enrollSecret == NULL || enrollSecret[0] == 0) {
    Serial.println("espnow: no enroll.secret — cannot derive LMKs");
    return 0;
  }
  char key[16];
  char buf[24];
  for (int i = 0; i < ESPNOW_MAX_GWS; i++) {
    snprintf(key, sizeof(key), NVS_KEY_GW_FMT, i);
    if (!nvs.getCStr(key, buf, sizeof(buf)) || buf[0] == 0) continue;
    uint8_t mac[6];
    if (!parseMac(buf, mac)) {
      Serial.printf("espnow: gw.%d mac '%s' invalid, skipped\n", i, buf);
      continue;
    }
    uint8_t lmk[16];
    if (!espnow_lmk(enrollSecret, mac, lmk)) {
      Serial.printf("espnow: LMK for gw.%d failed\n", i);
      continue;
    }
    int32_t ch = nvs.getInt(NVS_KEY_GW_CHANNEL, 0);
    // (mac, role, channel, key, keyLen) — key non-NULL = encrypted;
    // channel 0 = current (pinned) channel.
    int err = esp_now_add_peer(mac, ESP_NOW_ROLE_COMBO,
                               (u8)((ch >= 1 && ch <= 13) ? ch : 0),
                               lmk, 16);
    if (err != 0) {
      Serial.printf("espnow: add_peer gw.%d failed: %d\n", i, err);
      continue;
    }
    memcpy(_gws[_gwCount++], mac, 6);
    char macStr[18];
    formatMac(mac, macStr, sizeof(macStr));
    Serial.printf("espnow: gw.%d = %s paired\n", i, macStr);
  }
  return _gwCount;
}

bool RemoteLink::sendWalk(const char* json, int* gwIdx) {
  if (_gwCount == 0) return false;
  // Active gateway first, then list order (§8.2).
  uint8_t start = (g_rs.activeGw < _gwCount) ? g_rs.activeGw : 0;
  for (int attempt = 0; attempt < _gwCount; attempt++) {
    int i = (start + attempt) % _gwCount;
    if (sendPaced(_gws[i], json)) {
      if (gwIdx) *gwIdx = i;
      g_rs.activeGw = (uint8_t)i;
      return true;
    }
  }
  return false;
}

bool RemoteLink::sendTo(const uint8_t mac[6], const char* json) {
  return sendPaced(mac, json);
}

bool RemoteLink::sendPaced(const uint8_t mac[6], const char* json) {
  size_t len = strlen(json);
  if (len == 0 || len > ESPNOW_FRAME_MAX) return false;
  s_txDone = false;
  s_txOk = false;
  int err = esp_now_send((u8*)mac, (u8*)json, (int)len);
  if (err != 0) {
    Serial.printf("espnow: send error: %d\n", err);
    return false;
  }
  unsigned long deadline = millis() + ESPNOW_SEND_TIMEOUT_MS;
  while (!s_txDone && millis() < deadline) {
    delay(1);
  }
  return s_txDone && s_txOk;
}

bool RemoteLink::drain(EspNowRxItem* out) {
  if (s_tail == s_head) return false;
  *out = s_ring[s_tail];
  s_tail = (s_tail + 1) % ESPNOW_RX_RING;
  return true;
}

const uint8_t* RemoteLink::gwMac(int i) const {
  if (i < 0 || i >= _gwCount) return nullptr;
  return _gws[i];
}

bool RemoteLink::parseMac(const char* text, uint8_t out[6]) {
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

void RemoteLink::formatMac(const uint8_t mac[6], char* out, size_t outLen) {
  snprintf(out, outLen, "%02x:%02x:%02x:%02x:%02x:%02x",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}
