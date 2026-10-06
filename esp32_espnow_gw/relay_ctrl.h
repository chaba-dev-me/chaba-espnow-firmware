#pragma once

#include <Arduino.h>

class RelayCtrl {
public:
  void begin();
  void setAll(bool on);
  void setChannel(int ch, bool on);
  bool anyOn();
};

extern RelayCtrl relays;
