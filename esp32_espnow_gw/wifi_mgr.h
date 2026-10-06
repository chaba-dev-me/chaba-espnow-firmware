#pragma once

#include <Arduino.h>
#include <WiFi.h>

class WiFiMgr {
public:
  void begin(const char* ssid, const char* pass);
  void tick();
  bool isConnected();
  int32_t rssi();
private:
  unsigned long _lastTickMs = 0;
  unsigned long _connectStartedMs = 0;
  bool _connecting = false;
};

extern WiFiMgr wifi;
