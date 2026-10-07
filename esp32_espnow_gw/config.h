#pragma once

// --------------------------------------------------------------
// esp32_espnow_gw — Chaba ESP-NOW gateway
//
// An esp32_agroiot device (relays, heartbeat, fail-safe, bootstrap)
// that ADDITIONALLY runs the ESP-NOW link and one MQTT proxy
// connection per paired remote. SPECIFICATION_ESP_NOW.md §5-§8.
// The backend never learns that ESP-NOW exists: remotes are
// first-class devices; the gateway publishes their state AS them.
// --------------------------------------------------------------

#define RELAY_PIN_0 16
#define RELAY_PIN_1 17
#define RELAY_PIN_2 18
#define RELAY_PIN_3 19
#define RELAY_COUNT 4
#define RELAY_ACTIVE_LOW 1

#define STATUS_LED_PIN 2

#define WIFI_CONNECT_TIMEOUT_MS 15000
#define MQTT_CONNECT_TIMEOUT_MS 10000
#define WIFI_TICK_MS 100
#define MQTT_TICK_MS 100
#define STATE_PUBLISH_DEBOUNCE_MS 200
#define HEARTBEAT_PERIOD_MS 60000
#define FAIL_SAFE_TIMEOUT_MS 60000

#define SERIAL_BAUD 115200
#define SERIAL_PROMPT "gw> "

#if __has_include("secrets.h")
#include "secrets.h"
#endif

#ifndef DEFAULT_WIFI_SSID
#define DEFAULT_WIFI_SSID ""
#endif

#ifndef DEFAULT_WIFI_PASS
#define DEFAULT_WIFI_PASS ""
#endif

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
#define BOOTSTRAP_REBOOTSTRAP_MIN_MS 300000UL
#define BOOTSTRAP_BACKOFF_401_MS 600000UL

#define MQTT_KEEPALIVE_S 60
#define MQTT_QOS 1
#define MQTT_BUFFER_SIZE 1024

// ESP-NOW link ------------------------------------------------
#define ESPNOW_FRAME_VERSION 1
// (The legacy shared ESPNOW_PMK constant was removed 2026-10-07 —
// red-team H3. The PMK is now random per device and persisted via
// the key-value store as "espnow.pmk"; see espnow_link.cpp. Per-pair
// HKDF LMKs remain the effective frame keys for all encrypted peers.)
// "a handful to a dozen" remotes per gateway (spec §5); 8 slots keeps
// proxy RAM (WiFiClient + PubSubClient + buffers ≈ 6 KB each) inside
// a WROOM's 320 KB with headroom.
#define ESPNOW_MAX_PEERS 8
#define ESPNOW_FRAME_MAX 250
#define ESPNOW_RX_RING 8
// Send-callback deadline. A healthy link resolves in tens of ms; the
// full second only burns on a dead peer, which is a bench/ops event.
#define ESPNOW_SEND_TIMEOUT_MS 1000
// Fast retries per command frame before giving up (spec §7.2). The
// backend's 60 s reap-and-reclaim loop is the real retry engine.
#define ESPNOW_CMD_RETRIES 3

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

// Peer slots (spec §8.1: list-shaped from day one). Keys are
// "peer.<n>.<field>", n = 0..ESPNOW_MAX_PEERS-1. mac+secret are
// bench-provisioned; uid/cust/user/pass arrive over the air via
// hello+handover and persist (spec §8.6 honest cost); interval/
// adapt/lowv are the config-on-wake values pushed to the remote
// (spec §7.6).
#define NVS_KEY_PEER_FMT "peer.%d.%s"
#define PEER_FIELD_MAC "mac"
#define PEER_FIELD_SECRET "secret"
#define PEER_FIELD_UID "uid"
#define PEER_FIELD_CUST "cust"
#define PEER_FIELD_USER "user"
#define PEER_FIELD_PASS "pass"
#define PEER_FIELD_INTERVAL "interval"  // 15 chars — NVS key cap
#define PEER_FIELD_ADAPT "adapt"
#define PEER_FIELD_LOWV "lowv"

#define TOPIC_CMD_FMT "tenants/%s/devices/%s/cmd"
#define TOPIC_STATE_FMT "tenants/%s/devices/%s/state"

#define FW_VERSION "0.6.0"
