#pragma once

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFiClient.h>

class MqttClient {
public:
  bool begin(const char* host, uint16_t port, const char* clientId,
             const char* username, const char* password,
             const char* cmdTopic, const char* stateTopic);
  void tick();
  bool isConnected();
  bool publishState(const char* payload);
  void onMessage(char* topic, byte* payload, unsigned int length);

private:
  WiFiClient _wifi;
  PubSubClient _client;
  char _host[64];
  char _cmdTopic[128];
  char _stateTopic[128];
  char _clientId[64];
  // Credentials must be class members, not stack buffers passed by
  // the caller: PubSubClient::connect() stores only POINTERS to
  // user/pass and dereferences them on every reconnect.
  char _user[64];
  char _pass[96];
  unsigned long _lastTickMs = 0;
  unsigned long _lastConnectAttemptMs = 0;
};

extern MqttClient mqtt;
