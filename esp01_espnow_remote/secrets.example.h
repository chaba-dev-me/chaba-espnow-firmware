#pragma once
// Copy this file to secrets.h and fill in your own values (optional —
// everything here can also be set over the serial CLI; settings live
// in flash and survive reflashes).
//
//   cp secrets.example.h secrets.h

#define DEFAULT_WIFI_SSID ""
#define DEFAULT_WIFI_PASS ""

// HTTP host:port serving POST /api/v1/devices/bootstrap (bench only).
#define DEFAULT_BOOTSTRAP_HOST ""
#define DEFAULT_BOOTSTRAP_PORT 8000
