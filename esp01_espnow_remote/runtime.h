#pragma once

#include <Arduino.h>
#include "config.h"
#include "power.h"
#include "espnow_link.h"

class Runtime {
public:
  void start();
  void tick();
  bool isStarted() const { return _started; }

private:
  bool _started = false;
  bool _benchHold = false;  // waiting for bench bootstrap (WiFi + API)
  unsigned long _lastBootstrapAttemptMs = 0;
  PowerPolicy _policy;
  unsigned long _lastReadingMs = 0;

  void completeInit();
  bool benchBootstrap();       // WiFi → POST /devices/bootstrap → settings
  bool haveMqttCreds() const;
  bool sendHello();            // boot/wake announce; returns TX success
  void runWakeCycle();          // sleep_tx: hello → RX window → reading → sleep
  void sendReading(bool includeRelays);
  void sendHandover(const uint8_t mac[6]);
  void processFrame(const struct EspNowRxItem& item);
  void applyCommand(uint32_t cmdId, const char* action, int relay);
  void drainEspNow();
  void failSafeCheck();
};

extern Runtime runtime;
