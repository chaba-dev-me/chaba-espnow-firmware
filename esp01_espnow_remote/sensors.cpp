#include "sensors.h"

void sensorsRead(SensorData& out, const char* powerSource) {
  (void)powerSource;
  // No ADC on the ESP-01 — nothing to measure, nothing to report.
}
