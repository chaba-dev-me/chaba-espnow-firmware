#pragma once
// Copy this file to secrets.h and fill in your own values (optional —
// everything here can also be set over the serial CLI and lives in
// NVS, surviving reflashes).
//
//   cp secrets.example.h secrets.h

#define DEFAULT_WIFI_SSID ""
#define DEFAULT_WIFI_PASS ""

// Host running the Chaba backend / MQTT broker (LAN IP or hostname).
#define DEFAULT_MQTT_HOST ""
#define DEFAULT_MQTT_PORT 1883

// HTTP host:port serving POST /api/v1/devices/bootstrap.
#define DEFAULT_BOOTSTRAP_HOST ""
#define DEFAULT_BOOTSTRAP_PORT 8000
