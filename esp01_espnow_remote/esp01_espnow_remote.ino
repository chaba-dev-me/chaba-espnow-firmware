#include "config.h"
#include "nvs_store.h"
#include "serial_cli.h"
#include "state.h"
#include "runtime.h"

NvsStore nvs;
SerialCli cli;

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(200);
  Serial.println();
  Serial.println("========================================");
  Serial.print("  Chaba ESP-NOW remote (ESP-01) v");
  Serial.println(FW_VERSION);
  Serial.print("  built ");
  Serial.println(__DATE__ " " __TIME__);
  Serial.println("========================================");

  rtcPersistLoad();  // seq / cmd cursor survive deep sleep (with the mod)

  nvs.begin();
  cli.begin(&nvs);

  if (!nvs.hasKey(NVS_KEY_WIFI_SSID) && strlen(DEFAULT_WIFI_SSID) > 0) {
    nvs.putCStr(NVS_KEY_WIFI_SSID, DEFAULT_WIFI_SSID);
    Serial.print("loaded DEFAULT_WIFI_SSID from secrets.h: ");
    Serial.println(DEFAULT_WIFI_SSID);
  }
  if (!nvs.hasKey(NVS_KEY_WIFI_PASS) && strlen(DEFAULT_WIFI_PASS) > 0) {
    nvs.putCStr(NVS_KEY_WIFI_PASS, DEFAULT_WIFI_PASS);
    Serial.println("loaded DEFAULT_WIFI_PASS from secrets.h");
  }

  runtime.start();

  if (runtime.isStarted()) {
    Serial.println("CLI ready. type 'help'");
  } else {
    Serial.println("CLI ready (runtime not started). type 'help'");
  }
  Serial.print(SERIAL_PROMPT);
}

void loop() {
  cli.tick();
  runtime.tick();
}
