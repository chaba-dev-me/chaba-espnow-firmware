#include "sensors.h"
#include "config.h"

void sensorsRead(SensorData& out, const char* powerSource) {
  if (strcmp(powerSource, SOURCE_MAINS) == 0) {
    return;  // mains nodes report no energy keys
  }

  analogSetPinAttenuation(BATT_ADC_PIN, ADC_11db);
  uint32_t mv = analogReadMilliVolts(BATT_ADC_PIN);
  out.battV = (mv * ADC_DIV_RATIO) / 1000.0f;
  out.haveBatt = true;

  if (strcmp(powerSource, SOURCE_SOLAR) == 0) {
    analogSetPinAttenuation(PV_ADC_PIN, ADC_11db);
    uint32_t pvmv = analogReadMilliVolts(PV_ADC_PIN);
    out.pvV = (pvmv * ADC_DIV_RATIO) / 1000.0f;
    out.havePv = true;
    // Crude but honest charging heuristic: the panel is above the
    // pack by more than the diode/route margin. Bench: verify with
    // `show` against a meter before trusting the adaptive policy.
    out.charging = out.pvV > out.battV + (CHG_MARGIN_MV / 1000.0f);
  }
}

void appendSensors(JsonObject frame, const SensorData& e) {
  if (!e.haveBatt && !e.havePv) return;
  JsonObject sensors = frame.createNestedObject("sensors");
  if (e.haveBatt) {
    sensors["b"] = e.battV;   // ArduinoJson prints the short form (3.92)
  }
  if (e.havePv) {
    sensors["chg"] = e.charging ? 1 : 0;
    sensors["pv"] = e.pvV;
  }
}
