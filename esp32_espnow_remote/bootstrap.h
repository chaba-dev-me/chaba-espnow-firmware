#pragma once

#include <Arduino.h>
#include <WiFiClient.h>

// Fetches the device's MQTT configuration from the backend bootstrap
// endpoint (migrations 039/040). Claim-once: a never-bootstrapped
// device needs only its device_uid; if an enrollment secret is also
// provisioned it is included and required by the server on every
// call (hardened devices). Rides the site's WireGuard tunnel; the
// one-time mqtt_password in the response is stored to NVS by the
// caller.
//
// fetch() returns the backend HTTP status: 200 on success, 0 when
// the backend was unreachable, otherwise the rejection code (401 =
// unknown/already-claimed/wrong secret — callers should back off
// hard, the rate limiter will 429 anything faster).
class Bootstrapper {
public:
  int fetch(const char* backendHost, uint16_t backendPort,
            const char* deviceUid, const char* enrollSecret,  // secret may be ""
            char* mqttUser, size_t mqttUserLen,
            char* mqttPass, size_t mqttPassLen,
            char* customerUid, size_t customerUidLen,
            char* mqttHost, size_t mqttHostLen,
            uint16_t* mqttPort);

private:
  WiFiClient _client;
};

extern Bootstrapper bootstrapper;
