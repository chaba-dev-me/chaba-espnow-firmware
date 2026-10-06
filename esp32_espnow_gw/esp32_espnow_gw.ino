#include "config.h"
#include "nvs_store.h"
#include "serial_cli.h"
#include "state.h"
#include "relay_ctrl.h"
#include "wifi_mgr.h"
#include "mqtt_client.h"
#include "runtime.h"

NvsStore nvs;
SerialCli cli;

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(200);
  Serial.println();
  Serial.println("========================================");
  Serial.print("  ESP32 AgroIoT ESP-NOW gateway v");
  Serial.println(FW_VERSION);
  Serial.print("  built ");
  Serial.println(__DATE__ " " __TIME__);
  Serial.println("========================================");

  relays.begin();

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
  if (!nvs.hasKey(NVS_KEY_MQTT_HOST) && strlen(DEFAULT_MQTT_HOST) > 0) {
    nvs.putCStr(NVS_KEY_MQTT_HOST, DEFAULT_MQTT_HOST);
    Serial.print("loaded DEFAULT_MQTT_HOST from config.h: ");
    Serial.println(DEFAULT_MQTT_HOST);
  }
  if (!nvs.hasKey(NVS_KEY_MQTT_PORT)) {
    nvs.putInt(NVS_KEY_MQTT_PORT, DEFAULT_MQTT_PORT);
    Serial.print("loaded DEFAULT_MQTT_PORT from config.h: ");
    Serial.println(DEFAULT_MQTT_PORT);
  }

  runtime.start();

  Serial.println("CLI ready. type 'help'");
  Serial.print(SERIAL_PROMPT);
}

void loop() {
  cli.tick();
  runtime.tick();
}
