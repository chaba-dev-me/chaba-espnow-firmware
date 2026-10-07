#pragma once

#include <Arduino.h>

// ESP-01 stub of the ESP32 remote's sensors block: the ESP-01
// exposes no ADC (TOUT is not on the header), so there is no
// battery/PV telemetry. The struct and call sites stay identical to
// the ESP32 sketch so the energy policy and frame builder port
// unchanged — reads simply report nothing, which the backend stores
// as no sensors block at all (migration 055). External I²C probes
// (ADS7830-class ADC, SHT-style temp/humidity) would hook in HERE.
struct SensorData {
  bool haveBatt = false;
  bool havePv = false;
  bool charging = false;
  float battV = 0.0f;
  float pvV = 0.0f;
};

// No-op on the ESP-01: reports no energy keys.
void sensorsRead(SensorData& out, const char* powerSource);
