#pragma once

// --------------------------------------------------------------
// esp01_espnow_remote — Chaba ESP-NOW remote for the ESP-01/ESP-01S
//
// ESP8266 port of esp32_espnow_remote (SPECIFICATION_ESP_NOW.md §14
// TODO). REMOTE ONLY — an ESP-01 is never a gateway. An ESP-01 and an
// ESP32 running the remote sketch are indistinguishable to the
// gateway: same claim-once bootstrap, same v1 frame shapes, same LMK
// derivation, pairing into an unmodified peer slot.
//
// Hardware reality this port lives inside:
//   - ONE relay, on GPIO0 (boot-strap pin: idle HIGH keeps it
//     bootloader-safe; the relay input must not pull it down hard).
//     GPIO2 is the alternative (config) — it carries the blue LED.
//   - No ADC (TOUT unexposed): no battery/PV telemetry; the sensors
//     block reports nothing and the adaptive policy rides the
//     gateway's cfg-on-wake instead.
//   - No native deep-sleep wake: GPIO16/XPD_DCDC is not broken out.
//     `sleep_tx` therefore REQUIRES the solder mod (XPD_DCDC → RST)
//     — we can't detect the mod, so we warn loudly instead. Without
//     it use `always_on`.
//   - 1 MB flash, no OTA — bench reflashes via USB-serial adapter,
//     GPIO0 to GND at reset.
// --------------------------------------------------------------

#define RELAY_PIN_0 0
#define RELAY_COUNT 1
#define RELAY_ACTIVE_LOW 1

// The ESP-01's on-board blue LED (active LOW). GPIO2 is a boot-strap
// pin too, so the LED is driven only after boot completes.
#define STATUS_LED_PIN 2

#define SERIAL_BAUD 115200
#define SERIAL_PROMPT "remote01> "

#if __has_include("secrets.h")
#include "secrets.h"
#endif

#ifndef DEFAULT_WIFI_SSID
#define DEFAULT_WIFI_SSID ""
#endif

#ifndef DEFAULT_WIFI_PASS
#define DEFAULT_WIFI_PASS ""
#endif

// Bench-only: the remote associates just long enough to bootstrap.
#ifndef DEFAULT_BOOTSTRAP_HOST
#define DEFAULT_BOOTSTRAP_HOST ""
#endif
#ifndef DEFAULT_BOOTSTRAP_PORT
#define DEFAULT_BOOTSTRAP_PORT 8000
#endif

#define BOOTSTRAP_RETRY_MS 30000
#define WIFI_CONNECT_TIMEOUT_MS 20000

// ESP-NOW link ------------------------------------------------
#define ESPNOW_FRAME_VERSION 1
#define ESPNOW_MAX_GWS 4          // ordered gateway list (§8.1)
#define ESPNOW_FRAME_MAX 250
#define ESPNOW_RX_RING 6
#define ESPNOW_SEND_TIMEOUT_MS 1000
// Bounded RX window after a wake's TX (§7.6).
#define RX_WINDOW_MS 2000

// (The legacy shared ESPNOW_PMK constant was removed 2026-10-07 —
// red-team H3. The PMK is now random per device and persisted via
// the key-value store as "espnow.pmk"; see espnow_link.cpp.)

// Cadence & fail-safe ------------------------------------------
#define READING_PERIOD_MS 60000
// Fail-safe must tolerate LOST FRAMES, not just a dead gateway —
// 3× the heartbeat period (same value as the ESP32 remote).
#define FAIL_SAFE_TIMEOUT_MS 180000
#define HB_MISS_LIMIT 3
#define HB_RETURN_COUNT 5

// Energy policy (§7.6) ------------------------------------------
#define DEFAULT_WAKE_INTERVAL_S 300
#define ADAPT_STRETCH 4
#define OFFLINE_BACKOFF_K 3
#define OFFLINE_BACKOFF_CAP_S 3600
// ESP8266 deep sleep takes uint32 microseconds: 3600 s = 3.6e9 µs
// fits, and the cap above is also the hardware safety net.

// Settings storage ----------------------------------------------
// ESP8266 has no NVS: the same key-value API rides the EEPROM
// emulation (flash-backed, committed on write).
#define NVS_EEPROM_SIZE 4096

#define NVS_KEY_WIFI_SSID "wifi.ssid"
#define NVS_KEY_WIFI_PASS "wifi.pass"
#define NVS_KEY_MQTT_USER "mqtt.user"
#define NVS_KEY_MQTT_PASS "mqtt.pass"
#define NVS_KEY_ENROLL_SECRET "enroll.secret"
#define NVS_KEY_BOOTSTRAP_HOST "bootstrap.host"
#define NVS_KEY_BOOTSTRAP_PORT "bootstrap.port"
#define NVS_KEY_ID_CUSTOMER "id.customer"
#define NVS_KEY_ID_DEVICE "id.device"
#define NVS_KEY_FAIL_SAFE "relay.fail_safe"

#define NVS_KEY_GW_FMT "gw.%d.mac"     // ordered gateway list (§8.1)
#define NVS_KEY_GW_CHANNEL "gw.channel"
#define NVS_KEY_POWER_PROFILE "power.profile"
#define NVS_KEY_POWER_SOURCE "power.source"
#define NVS_KEY_WAKE_INTERVAL "wake.interval_s"
#define NVS_KEY_ADAPT "adapt"
#define NVS_KEY_ADAPT_LOWV "adapt.low_v"

#define PROFILE_ALWAYS_ON "always_on"
#define PROFILE_SLEEP_TX "sleep_tx"
#define SOURCE_MAINS "mains"
#define SOURCE_BATTERY "battery"
#define SOURCE_SOLAR "solar"

#define FW_VERSION "0.6.0-esp01"
