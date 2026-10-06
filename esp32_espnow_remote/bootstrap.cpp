#include "bootstrap.h"
#include <ArduinoJson.h>

Bootstrapper bootstrapper;

// The JSON body is built by hand; only the token_urlsafe / UUID
// charset may pass, or the request could be malformed (or smuggle
// extra JSON fields). Anything outside printable ASCII minus
// quote/backslash is refused.
static bool bodySafe(const char* s) {
  if (s == nullptr) return false;
  for (; *s; s++) {
    char c = *s;
    if (c < 33 || c > 126 || c == '"' || c == '\\') return false;
  }
  return true;
}

int Bootstrapper::fetch(const char* backendHost, uint16_t backendPort,
                        const char* deviceUid, const char* enrollSecret,
                        char* mqttUser, size_t mqttUserLen,
                        char* mqttPass, size_t mqttPassLen,
                        char* customerUid, size_t customerUidLen,
                        char* mqttHost, size_t mqttHostLen,
                        uint16_t* mqttPort) {
  if (!bodySafe(deviceUid)) {
    Serial.println("bootstrap: device_uid has unsafe characters");
    return 0;
  }
  bool haveSecret = enrollSecret != nullptr && strlen(enrollSecret) > 0;
  if (haveSecret && !bodySafe(enrollSecret)) {
    Serial.println("bootstrap: enroll.secret has unsafe characters");
    return 0;
  }

  // Claim-once: the enrollment secret is optional. A never-bootstrapped
  // device claims on uid alone; a hardened device must present its
  // secret on every call.
  char body[192];
  if (haveSecret) {
    snprintf(body, sizeof(body),
             "{\"device_uid\":\"%s\",\"enrollment_secret\":\"%s\"}",
             deviceUid, enrollSecret);
  } else {
    snprintf(body, sizeof(body), "{\"device_uid\":\"%s\"}", deviceUid);
  }

  if (!_client.connect(backendHost, backendPort)) {
    Serial.println("bootstrap: backend connect failed");
    return 0;
  }

  char head[256];
  int headLen = snprintf(head, sizeof(head),
                         "POST /api/v1/devices/bootstrap HTTP/1.1\r\n"
                         "Host: %s:%u\r\n"
                         "Content-Type: application/json\r\n"
                         "Content-Length: %u\r\n"
                         "Connection: close\r\n"
                         "\r\n",
                         backendHost, (unsigned)backendPort,
                         (unsigned)strlen(body));
  _client.write((const uint8_t*)head, headLen);
  _client.write((const uint8_t*)body, strlen(body));

  // Status line: "HTTP/1.1 200 OK".
  String status = _client.readStringUntil('\n');
  int httpCode = 0;
  if (status.startsWith("HTTP/")) {
    int sp = status.indexOf(' ');
    httpCode = status.substring(sp + 1, sp + 4).toInt();
  }

  // Skip headers: read until the blank line, bounded by a deadline.
  unsigned long deadline = millis() + 10000;
  while (millis() < deadline) {
    String line = _client.readStringUntil('\n');
    if (line.length() == 0 || line == "\r") break;
  }

  if (httpCode != 200) {
    Serial.print("bootstrap: backend HTTP ");
    Serial.println(httpCode);
    _client.stop();
    return httpCode;
  }

  // Stream-parse the body straight off the socket (no body buffer).
  StaticJsonDocument<640> doc;
  DeserializationError err = deserializeJson(doc, _client);
  _client.stop();
  if (err) {
    Serial.print("bootstrap: json error: ");
    Serial.println(err.c_str());
    return 0;
  }

  const char* u = doc["mqtt_username"] | "";
  const char* p = doc["mqtt_password"] | "";
  const char* c = doc["customer_uid"] | "";
  const char* h = doc["mqtt_host"] | "";
  long port = doc["mqtt_port"] | 0;
  if (strlen(u) == 0 || strlen(p) == 0 || strlen(c) == 0 ||
      strlen(h) == 0 || port <= 0 || port > 65535) {
    Serial.println("bootstrap: response missing fields");
    return 0;
  }
  if (strlen(u) >= mqttUserLen || strlen(p) >= mqttPassLen ||
      strlen(c) >= customerUidLen || strlen(h) >= mqttHostLen) {
    Serial.println("bootstrap: response field too long");
    return 0;
  }

  strlcpy(mqttUser, u, mqttUserLen);
  strlcpy(mqttPass, p, mqttPassLen);
  strlcpy(customerUid, c, customerUidLen);
  strlcpy(mqttHost, h, mqttHostLen);
  *mqttPort = (uint16_t)port;
  return httpCode;
}
