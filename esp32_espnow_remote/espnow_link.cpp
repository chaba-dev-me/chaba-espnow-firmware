#include "espnow_link.h"
#include "nvs_store.h"
#include "hkdf.h"
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_mac.h>

extern NvsStore nvs;

// Per-device random Primary Master Key (red-team H3): the legacy
// shared constant (SHA256("chaba-espnow-v1-pmk")[0..15]) shipped in
// this public repo. The PMK is never the effective frame key — every
// peer registers encrypt=true with a per-pair HKDF LMK — but a
// per-device random value means no shared, publicly-harvestable
// constant survives in the source. Persisted as "espnow.pmk" (32 hex
// chars) so it is stable across reboots.
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
  esp_now_set_pmk(pmk);
}

RemoteLink espnow;

static volatile bool s_txDone = false;
static volatile esp_now_send_status_t s_txStatus = ESP_NOW_SEND_FAIL;
static EspNowRxItem s_ring[ESPNOW_RX_RING];
static volatile uint8_t s_head = 0;
static volatile uint8_t s_tail = 0;

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
  if (!esp_now_is_peer_exist(info->src_addr)) return;
  uint8_t nextHead = (s_head + 1) % ESPNOW_RX_RING;
  if (nextHead == s_tail) return;
  EspNowRxItem& slot = s_ring[s_head];
  memcpy(slot.mac, info->src_addr, 6);
  memcpy(slot.json, data, len);
  slot.json[len] = 0;
  slot.rssi = (info->rx_ctrl != NULL) ? info->rx_ctrl->rssi : 0;
  s_head = nextHead;
}

bool RemoteLink::begin(bool pinChannel) {
  WiFi.mode(WIFI_STA);  // STA role, never begin() — no association
  // eFuse read, NOT WiFi.macAddress(): after the bench bootstrap path
  // cycles the radio OFF→STA, the driver isn't up yet and macAddress()
  // returns all zeros. The eFuse value is what the STA MAC will be.
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  Serial.printf("espnow: remote MAC = %02x:%02x:%02x:%02x:%02x:%02x"
                "  (this is what 'set peer mac' on the gateway wants)\n",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  if (pinChannel) {
    // Site channel pinning (§3). Only when we will NOT associate:
    // an associated STA drags the channel to its AP's.
    int32_t ch = nvs.getInt(NVS_KEY_GW_CHANNEL, 0);
    if (ch >= 1 && ch <= 13) {
      esp_wifi_set_promiscuous(true);
      esp_err_t err = esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
      esp_wifi_set_promiscuous(false);
      if (err == ESP_OK) {
        Serial.printf("espnow: channel pinned to %d\n", (int)ch);
      } else {
        Serial.printf("espnow: pin channel FAILED: %s\n",
                      esp_err_to_name(err));
      }
    } else {
      Serial.println("espnow: WARNING no gw.channel pinned — set 'set gw channel <n>' (spec §3)");
    }
  }
  esp_err_t err = esp_now_init();
  if (err != ESP_OK) {
    Serial.print("espnow: init failed: ");
    Serial.println(esp_err_to_name(err));
    return false;
  }
  chabaInstallPmk();
  esp_now_register_send_cb(onSent);
  esp_now_register_recv_cb(onRecv);
  Serial.println("espnow: initialised (remote role)");
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
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, mac, 6);
    peer.channel = 0;  // current (pinned) channel
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = true;
    memcpy(peer.lmk, lmk, 16);
    esp_err_t err = esp_now_add_peer(&peer);
    if (err != ESP_OK && err != ESP_ERR_ESPNOW_EXIST) {
      Serial.printf("espnow: add_peer gw.%d failed: %s\n", i,
                    esp_err_to_name(err));
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
