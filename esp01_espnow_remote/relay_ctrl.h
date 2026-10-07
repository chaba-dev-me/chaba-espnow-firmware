#pragma once

#include <Arduino.h>

// The ESP-01 carries ONE relay (GPIO0, active-LOW — idle HIGH is
// bootloader-safe on this boot-strap pin). The multi-relay API is
// kept so the runtime and CLI read like the ESP32 remote; channel
// indices other than 0 are refused at the call sites.
class RelayCtrl {
public:
  void begin();
  void setAll(bool on);
  void setChannel(int ch, bool on);
  bool anyOn();
};

extern RelayCtrl relays;
