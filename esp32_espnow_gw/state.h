#pragma once

#include <Arduino.h>

struct State {
  bool wifiConnected = false;
  bool mqttConnected = false;
  unsigned long wifiLastGoodMs = 0;
  unsigned long mqttLastGoodMs = 0;
  bool relays[4] = {false, false, false, false};
  bool relaysChanged = true;
  uint32_t lastCommandLogId = 0;
  bool lastCommandFailed = false;
  char lastError[64] = "";
  bool failSafe = true;
  bool failSafeTriggered = false;
  // Set by MqttClient when the broker rejects our credentials
  // (CONACK rc=4/5); Runtime responds by re-bootstrapping — that is
  // how server-side credential rotation reaches the device without
  // OTA.
  bool mqttAuthFailed = false;
  unsigned long bootMs = 0;
  unsigned long lastHeartbeatMs = 0;
  unsigned long lastStatePublishMs = 0;
};

extern State g_state;
