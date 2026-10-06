#pragma once

#include <Arduino.h>
#include "config.h"

class Runtime {
public:
  void start();
  void tick();
  bool isStarted() const { return _started; }

private:
  bool _started = false;
  // MQTT config state: false until valid credentials are loaded
  // (from NVS at boot, or fetched via bootstrap on first boot /
  // after the broker rejects them).
  bool _mqttConfigured = false;
  unsigned long _lastBootstrapMs = 0;
  // Delay before the NEXT bootstrap attempt. Never encode the
  // delay as a future timestamp: unsigned (now - future) wraps
  // and the backoff silently becomes no backoff (0.4.3 bug).
  unsigned long _bootstrapDelayMs = BOOTSTRAP_RETRY_MS;
  unsigned long _lastReBootstrapMs = 0;
  void publishState();
  bool startMqtt();
  void runBootstrap();
  void drainEspNow();
};

extern Runtime runtime;

// Broker coordinates the proxy connections share with the gateway's
// own client (bootstrap/NVS holds one broker per site).
const char* runtimeMqttHost();
uint16_t runtimeMqttPort();
