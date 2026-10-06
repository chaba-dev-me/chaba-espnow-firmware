#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

// Energy telemetry for the `sensors` block (spec §7.6). Firmware owns
// the keys; the backend stores the block verbatim (migration 055).
struct SensorData {
  bool haveBatt = false;
  bool havePv = false;
  bool charging = false;
  float battV = 0.0f;
  float pvV = 0.0f;
};

// Reads the ADC pins per the configured energy source.
void sensorsRead(SensorData& out, const char* powerSource);

// Builds frame["sensors"] from a SensorData:
//   battery: {"b":3.92}
//   solar:   {"b":3.92,"chg":1,"pv":5.10}
//   mains:   no energy keys at all.
// External probes (t/h/level) hook in HERE — add keys to this block
// and the backend picks them up with no firmware-side schema change.
void appendSensors(JsonObject frame, const SensorData& e);
