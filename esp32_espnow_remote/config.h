#pragma once

// --------------------------------------------------------------
// esp32_espnow_remote — Chaba ESP-NOW remote node
//
// One sketch, two modes (spec §2): `always_on` (radio RX always on,
// full command path + fail-safe, relays allowed) and `sleep_tx`
// (TX, then deep sleep as soon as the send-callback resolves).
// Energy source (mains/battery/solar) is a separate, orthogonal
// config that drives telemetry and the adaptive-cadence policy.
//
// The remote NEVER opens an MQTT connection in the field: at the
// bench it bootstraps over WiFi (claim-once) and later hands the
// issued credentials to the gateway (spec §6.2 handover).
// --------------------------------------------------------------

#define RELAY_PIN_0 16
#define RELAY_PIN_1 17
#define RELAY_PIN_2 18
#define RELAY_PIN_3 19
#define RELAY_COUNT 4
#define RELAY_ACTIVE_LOW 1

#define STATUS_LED_PIN 2

#define SERIAL_BAUD 115200
#define SERIAL_PROMPT "remote> "

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
#ifndef DEFAULT_MQTT_HOST
#define DEFAULT_MQTT_HOST "10.0.4.1"
#endif
#ifndef DEFAULT_MQTT_PORT
#define DEFAULT_MQTT_PORT 1883
#endif
#ifndef DEFAULT_BOOTSTRAP_HOST
#define DEFAULT_BOOTSTRAP_HOST "10.0.4.1"
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
#define ESPNOW_RX_RING 8
#define ESPNOW_SEND_TIMEOUT_MS 1000
// Bounded RX window after a wake's TX (§7.6): long enough for the
// gateway's hb/cfg/cmd replies, short enough to respect the pack.
#define RX_WINDOW_MS 2000

// Cadence & fail-safe ------------------------------------------
#define READING_PERIOD_MS 60000   // always_on reading cadence (and the
                                  // default cfg.interval_s target)
// Fail-safe must tolerate LOST FRAMES, not just a dead gateway: the
// heartbeat also runs at 60 s, so a 60 s timeout trips on a single
// dropped frame and yanks relays off mid-operation (seen on the bench
// 2026-10-06: commands acked while the relay read off). 3× the
// heartbeat period still fails safe within ~3 min of a truly dead
// gateway.
#define FAIL_SAFE_TIMEOUT_MS 180000
// Multi-gateway hysteresis (§8.2): N misses → move on; M good hb
// from a better-ranked gateway → come back. No flapping.
#define HB_MISS_LIMIT 3
#define HB_RETURN_COUNT 5

// Energy policy (§7.6) ------------------------------------------
#define DEFAULT_WAKE_INTERVAL_S 300
#define ADAPT_STRETCH 4            // ×4 below adapt.low_v while not charging
#define OFFLINE_BACKOFF_K 3        // failed cycles before doubling starts
#define OFFLINE_BACKOFF_CAP_S 3600

// ADC / energy telemetry ----------------------------------------
// Carrier-specific: classic WROOM DevKitC pins on ADC1 (GPIO32-39,
// input-only is fine); C6 carriers expose ADC1 on GPIO0-6. Re-map in
// config.h per carrier and verify with `show` against a meter.
#if defined(CONFIG_IDF_TARGET_ESP32C6)
#define BATT_ADC_PIN 3   // ADC1_CH3
#define PV_ADC_PIN 2     // ADC1_CH2
#else
#define BATT_ADC_PIN 34  // ADC1_CH6
#define PV_ADC_PIN 35    // ADC1_CH7
#endif
#define ADC_DIV_RATIO 2.0f   // divider: Batt+ —[100k]—•—[100k]— GND
#define CHG_MARGIN_MV 200    // pv > batt + margin ⇒ chg=1

// NVS ----------------------------------------------------------
#define NVS_NS "agroiot"
#define NVS_KEY_WIFI_SSID "wifi.ssid"
#define NVS_KEY_WIFI_PASS "wifi.pass"
#define NVS_KEY_MQTT_HOST "mqtt.host"
#define NVS_KEY_MQTT_PORT "mqtt.port"
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

#define FW_VERSION "0.5.0"
