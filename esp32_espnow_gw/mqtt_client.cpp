#include "mqtt_client.h"
#include "state.h"
#include "relay_ctrl.h"
#include "config.h"
#include <ArduinoJson.h>

MqttClient mqtt;

bool MqttClient::begin(const char* host, uint16_t port, const char* clientId,
                       const char* username, const char* password,
                       const char* cmdTopic, const char* stateTopic) {
  strncpy(_host, host, sizeof(_host) - 1);
  _host[sizeof(_host) - 1] = 0;
  strncpy(_clientId, clientId, sizeof(_clientId) - 1);
  _clientId[sizeof(_clientId) - 1] = 0;
  strncpy(_user, username, sizeof(_user) - 1);
  _user[sizeof(_user) - 1] = 0;
  strncpy(_pass, password, sizeof(_pass) - 1);
  _pass[sizeof(_pass) - 1] = 0;
  strncpy(_cmdTopic, cmdTopic, sizeof(_cmdTopic) - 1);
  _cmdTopic[sizeof(_cmdTopic) - 1] = 0;
  strncpy(_stateTopic, stateTopic, sizeof(_stateTopic) - 1);
  _stateTopic[sizeof(_stateTopic) - 1] = 0;
  _client.setClient(_wifi);
  _client.setServer(_host, port);
  _client.setBufferSize(MQTT_BUFFER_SIZE);
  _client.setKeepAlive(MQTT_KEEPALIVE_S);
  _client.setSocketTimeout(5);
  _client.setCallback([this](char* t, byte* p, unsigned int l) { this->onMessage(t, p, l); });
  return true;
}

void MqttClient::tick() {
  unsigned long now = millis();
  if (now - _lastTickMs < MQTT_TICK_MS) return;
  _lastTickMs = now;

  if (_client.connected()) {
    _client.loop();
    g_state.mqttConnected = true;
    g_state.mqttLastGoodMs = now;
    // A successful connection invalidates a stale auth-failure signal
    // (e.g. rc=5 seen while the broker password file lagged behind a
    // bootstrap). Without this, the first 5-minute uptime mark would
    // trigger a needless re-bootstrap — which rotates the credential
    // again, recreating the rc=5 window, forever.
    g_state.mqttAuthFailed = false;
  } else {
    g_state.mqttConnected = false;
    if (now - _lastConnectAttemptMs > 5000) {
      _lastConnectAttemptMs = now;
      Serial.print("mqtt: connecting as ");
      Serial.print(_clientId);
      Serial.print(" ... ");
      if (_client.connect(_clientId, _user, _pass)) {
        Serial.println("OK");
        // Set before anything else can publish: the state publish that
        // immediately follows connect must not report "disconnected".
        g_state.mqttConnected = true;
        // subscribe() only reports local failures (not connected,
        // packet build/buffer) — PubSubClient does not parse the
        // SUBACK, so an ACL-denied subscription is NOT visible here.
        // If this logs OK but commands never arrive, check the
        // broker ACL for this device's username.
        if (_client.subscribe(_cmdTopic, MQTT_QOS)) {
          Serial.print("mqtt: subscribed ");
          Serial.println(_cmdTopic);
        } else {
          Serial.print("mqtt: SUBSCRIBE failed (local) for ");
          Serial.println(_cmdTopic);
        }
        g_state.relaysChanged = true;
        g_state.lastHeartbeatMs = now;
      } else {
        int8_t st = (int8_t)_client.state();
        Serial.print("FAIL rc=");
        Serial.println(st);
        if (st == 4 || st == 5) {
          // Broker knows us but rejected the credential — it was
          // rotated server-side. Runtime sees this flag and
          // re-bootstraps for a fresh one.
          g_state.mqttAuthFailed = true;
        }
        // rc < 0 = TCP/DNS trouble; rc 1-3 = broker config problems.
      }
    }
  }
}

bool MqttClient::isConnected() {
  return _client.connected();
}

bool MqttClient::publishState(const char* payload) {
  if (!_client.connected()) return false;
  return _client.publish(_stateTopic, (const uint8_t*)payload, strlen(payload), false);
}

void MqttClient::onMessage(char* topic, byte* payload, unsigned int length) {
  char buf[512];
  unsigned int n = length;
  if (n >= sizeof(buf)) n = sizeof(buf) - 1;
  memcpy(buf, payload, n);
  buf[n] = 0;

  Serial.print("mqtt in: ");
  Serial.println(buf);

  StaticJsonDocument<512> doc;
  DeserializationError err = deserializeJson(doc, buf);
  if (err) {
    Serial.print("json parse error: ");
    Serial.println(err.c_str());
    return;
  }

  const char* action = doc["action"] | "";
  uint32_t cmdLogId = doc["command_log_id"] | 0;

  g_state.lastCommandLogId = cmdLogId;
  g_state.lastCommandFailed = false;
  g_state.lastError[0] = 0;

  // relay: optional int, 1-based. Absent or 0 means "all relays"
  // (legacy behaviour, kept for backward compat with pre-migration-023
  // backends). See SPECIFICATION_CUSTOMER_API.md §14 and migration
  // 023 daily log entry for the rationale.
  int relay = doc["relay"] | 0;

  if (strcmp(action, "switch_on") == 0) {
    if (relay >= 1 && relay <= RELAY_COUNT) {
      relays.setChannel(relay - 1, true);  // 1-based API → 0-based array
      Serial.print("relay ");
      Serial.print(relay);
      Serial.println(": ON");
    } else if (relay == 0) {
      // Legacy: no relay field → all relays
      relays.setAll(true);
      Serial.println("relays: all ON (legacy)");
    } else {
      g_state.lastCommandFailed = true;
      strncpy(g_state.lastError, "relay_out_of_range", sizeof(g_state.lastError) - 1);
      Serial.print("relay out of range: ");
      Serial.println(relay);
    }
  } else if (strcmp(action, "switch_off") == 0) {
    if (relay >= 1 && relay <= RELAY_COUNT) {
      relays.setChannel(relay - 1, false);
      Serial.print("relay ");
      Serial.print(relay);
      Serial.println(": OFF");
    } else if (relay == 0) {
      relays.setAll(false);
      Serial.println("relays: all OFF (legacy)");
    } else {
      g_state.lastCommandFailed = true;
      strncpy(g_state.lastError, "relay_out_of_range", sizeof(g_state.lastError) - 1);
      Serial.print("relay out of range: ");
      Serial.println(relay);
    }
  } else {
    g_state.lastCommandFailed = true;
    strncpy(g_state.lastError, "unknown_action", sizeof(g_state.lastError) - 1);
    Serial.print("unknown action: ");
    Serial.println(action);
  }

  g_state.relaysChanged = true;
}
