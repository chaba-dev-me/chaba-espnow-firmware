#include "wifi_mgr.h"
#include "state.h"
#include "config.h"

WiFiMgr wifi;

void WiFiMgr::begin(const char* ssid, const char* pass) {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);
  _connectStartedMs = millis();
  _connecting = true;
  g_state.wifiConnected = false;
  Serial.print("wifi: connecting to ");
  Serial.println(ssid);
}

void WiFiMgr::tick() {
  unsigned long now = millis();
  if (now - _lastTickMs < WIFI_TICK_MS) return;
  _lastTickMs = now;

  wl_status_t s = WiFi.status();
  if (s == WL_CONNECTED) {
    if (!g_state.wifiConnected) {
      Serial.print("wifi: connected, IP ");
      Serial.println(WiFi.localIP());
    }
    g_state.wifiConnected = true;
    g_state.wifiLastGoodMs = now;
    _connecting = false;
  } else {
    if (g_state.wifiConnected) {
      Serial.print("wifi: disconnected, status=");
      Serial.println((int)s);
    }
    g_state.wifiConnected = false;
    if (!_connecting) {
      _connecting = true;
      _connectStartedMs = now;
      Serial.println("wifi: reconnecting");
      WiFi.reconnect();
    } else if (now - _connectStartedMs > WIFI_CONNECT_TIMEOUT_MS) {
      Serial.println("wifi: timeout, retry");
      _connectStartedMs = now;
      WiFi.reconnect();
    }
  }
}

bool WiFiMgr::isConnected() {
  return WiFi.status() == WL_CONNECTED;
}

int32_t WiFiMgr::rssi() {
  return WiFi.RSSI();
}
