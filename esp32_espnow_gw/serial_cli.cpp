#include "serial_cli.h"
#include "config.h"
#include "relay_ctrl.h"
#include "state.h"
#include "proxy.h"
#include "espnow_link.h"
#include "hkdf.h"

extern NvsStore nvs;
extern ProxyMgr proxies;

void SerialCli::begin(NvsStore* nvs) {
  _nvs = nvs;
}

void SerialCli::tick() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r' || c == '\n') {
      if (_line.length() > 0) {
        if (_echo) Serial.println();
        handleLine();
        _line = "";
      }
      printPrompt();
    } else if (c == 127 || c == 8) {
      if (_line.length() > 0) {
        _line.remove(_line.length() - 1);
        if (_echo) {
          Serial.write(8);
          Serial.write(' ');
          Serial.write(8);
        }
      }
    } else if (c >= 32 && c < 127) {
      _line += c;
      if (_echo) Serial.write(c);
    }
  }
}

void SerialCli::printPrompt() {
  Serial.print(SERIAL_PROMPT);
}

int SerialCli::tokenize(char* buf, char** argv, int maxArgs) {
  int argc = 0;
  char* tok = strtok(buf, " ");
  while (tok && argc < maxArgs) {
    argv[argc++] = tok;
    tok = strtok(NULL, " ");
  }
  return argc;
}

String SerialCli::getValue(const String& line, int argc, char** argv) {
  if (argc < 4) return "";
  int consumed = 0;
  int pos = 0;
  while (pos < (int)line.length() && consumed < 3) {
    if (line[pos] == ' ') consumed++;
    pos++;
  }
  while (pos < (int)line.length() && line[pos] == ' ') pos++;
  String v = line.substring(pos);
  v.trim();
  return v;
}

void SerialCli::handleLine() {
  String line = _line;
  line.trim();
  if (line.length() == 0) return;

  char buf[256];
  if (line.length() >= (int)sizeof(buf)) {
    Serial.println("line too long");
    return;
  }
  strncpy(buf, line.c_str(), sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = 0;

  char* argv[8];
  int argc = tokenize(buf, argv, 8);

  if (strcmp(argv[0], "help") == 0 || strcmp(argv[0], "?") == 0) {
    cmd_help();
  } else if (strcmp(argv[0], "show") == 0) {
    cmd_show();
  } else if (strcmp(argv[0], "version") == 0 ||
             strcmp(argv[0], "ver") == 0) {
    cmd_version();
  } else if (strcmp(argv[0], "set") == 0) {
    cmd_set(line, argc, argv);
  } else if (strcmp(argv[0], "relay") == 0) {
    cmd_relay(argc, argv);
  } else if (strcmp(argv[0], "peers") == 0) {
    cmd_peers();
  } else if (strcmp(argv[0], "del") == 0) {
    cmd_del(argc, argv);
  } else if (strcmp(argv[0], "selftest") == 0) {
    cmd_selftest();
  } else if (strcmp(argv[0], "boot") == 0) {
    cmd_boot();
  } else if (strcmp(argv[0], "reset") == 0) {
    cmd_reset();
  } else if (strcmp(argv[0], "factory") == 0) {
    cmd_factory();
  } else {
    Serial.print("unknown command: ");
    Serial.println(argv[0]);
    Serial.println("type 'help' for a list");
  }
}

void SerialCli::cmd_version() {
  Serial.print("firmware  = ESP32 AgroIoT ESP-NOW gateway v");
  Serial.println(FW_VERSION);
  Serial.print("board     = ESP32 (");
  Serial.print(F_CPU / 1000000);
  Serial.println(" MHz)");
  Serial.print("build     = ");
  Serial.println(__DATE__ " " __TIME__);
  Serial.print("sdk       = ");
  #ifdef ESP32
    Serial.print("Arduino ESP32 ");
    Serial.println(ESP.getSdkVersion());
  #else
    Serial.println("(unknown)");
  #endif
}

void SerialCli::cmd_help() {
  Serial.println("commands:");
  Serial.println("  help");
  Serial.println("  show");
  Serial.println("  peers        (peer slots + link health)");
  Serial.println("  version      (firmware version + build info)");
  Serial.println("  set wifi ssid <text>");
  Serial.println("  set wifi pass <text>");
  Serial.println("  set mqtt host <host>");
  Serial.println("  set mqtt port <port>");
  Serial.println("  set mqtt user <text>  (optional; fetched via bootstrap)");
  Serial.println("  set mqtt pass <text>  (optional; fetched via bootstrap)");
  Serial.println("  set enroll secret <text> (bench: enables bootstrap)");
  Serial.println("  set bootstrap host <host>");
  Serial.println("  set bootstrap port <port>");
  Serial.println("  set id customer <uid> (optional; fetched via bootstrap)");
  Serial.println("  set id device <uid>");
  Serial.println("  set relay fail_safe <on|off>");
  Serial.println("  set peer mac <idx> <hex>      (pair a remote: slot + its MAC)");
  Serial.println("  set peer secret <idx> <text>  (remote's enroll secret = LMK IKM)");
  Serial.println("  set peer interval <idx> <s>   (config-on-wake wake cadence)");
  Serial.println("  set peer adapt <idx> <0|1>    (config-on-wake adaptive policy)");
  Serial.println("  set peer lowv <idx> <volts>   (config-on-wake adapt threshold)");
  Serial.println("  del peer <idx>");
  Serial.println("  relay <ch> <on|off>    (manual: ch = 0..3, toggles GPIO directly)");
  Serial.println("  selftest      (HKDF RFC 5869 vectors)");
  Serial.println("  boot");
  Serial.println("  reset    (reboot, keep NVS)");
  Serial.println("  factory  (wipe NVS + reboot)");
}

static void printMasked(const char* label, NvsStore* nvs, const char* key) {
  char buf[64];
  int len = 0;
  if (nvs->getCStr(key, buf, sizeof(buf))) len = strlen(buf);
  if (len > (int)sizeof(buf) - 1) len = sizeof(buf) - 1;
  for (int i = 0; i < len; i++) buf[i] = '*';
  buf[len] = 0;
  Serial.print(label);
  Serial.print(" = ");
  Serial.println(buf);
}

void SerialCli::cmd_show() {
  char buf[64];
  Serial.println();
  Serial.println("--- configuration ---");

  Serial.print("firmware        = ESP32 AgroIoT ESP-NOW gateway v");
  Serial.println(FW_VERSION);

  Serial.print("wifi.ssid       = ");
  if (!_nvs->getCStr(NVS_KEY_WIFI_SSID, buf, sizeof(buf))) buf[0] = 0;
  Serial.println(buf);

  printMasked("wifi.pass      ", _nvs, NVS_KEY_WIFI_PASS);

  Serial.print("mqtt.host       = ");
  if (!_nvs->getCStr(NVS_KEY_MQTT_HOST, buf, sizeof(buf))) buf[0] = 0;
  Serial.println(buf);

  Serial.print("mqtt.port       = ");
  Serial.println(_nvs->getInt(NVS_KEY_MQTT_PORT, DEFAULT_MQTT_PORT));

  Serial.print("mqtt.user       = ");
  if (!_nvs->getCStr(NVS_KEY_MQTT_USER, buf, sizeof(buf))) buf[0] = 0;
  Serial.println(buf);

  printMasked("mqtt.pass      ", _nvs, NVS_KEY_MQTT_PASS);
  printMasked("enroll.secret  ", _nvs, NVS_KEY_ENROLL_SECRET);

  Serial.print("bootstrap.host  = ");
  if (!_nvs->getCStr(NVS_KEY_BOOTSTRAP_HOST, buf, sizeof(buf)) || buf[0] == 0) {
    snprintf(buf, sizeof(buf), "%s", DEFAULT_BOOTSTRAP_HOST);
  }
  Serial.println(buf);

  Serial.print("bootstrap.port  = ");
  int32_t bpShow = _nvs->getInt(NVS_KEY_BOOTSTRAP_PORT, DEFAULT_BOOTSTRAP_PORT);
  if (bpShow <= 0 || bpShow > 65535) bpShow = DEFAULT_BOOTSTRAP_PORT;
  Serial.println(bpShow);

  Serial.print("id.customer     = ");
  if (!_nvs->getCStr(NVS_KEY_ID_CUSTOMER, buf, sizeof(buf))) buf[0] = 0;
  Serial.println(buf);

  Serial.print("id.device       = ");
  if (!_nvs->getCStr(NVS_KEY_ID_DEVICE, buf, sizeof(buf))) buf[0] = 0;
  Serial.println(buf);

  Serial.print("relay.fail_safe = ");
  Serial.println(_nvs->getInt(NVS_KEY_FAIL_SAFE) ? "on" : "off");

  Serial.println("--- type 'peers' for ESP-NOW slots ---");
  Serial.println();
}

void SerialCli::cmd_set(const String& line, int argc, char** argv) {
  if (argc < 4) {
    Serial.println("usage: set <section> <key> <value>");
    return;
  }
  const char* section = argv[1];
  const char* key = argv[2];
  String value = getValue(line, argc, argv);

  if (strcmp(section, "wifi") == 0 && strcmp(key, "ssid") == 0) {
    Serial.println(_nvs->putCStr(NVS_KEY_WIFI_SSID, value.c_str()) ? "OK" : "ERROR: not stored");
  } else if (strcmp(section, "wifi") == 0 && strcmp(key, "pass") == 0) {
    Serial.println(_nvs->putCStr(NVS_KEY_WIFI_PASS, value.c_str()) ? "OK" : "ERROR: not stored");
  } else if (strcmp(section, "mqtt") == 0 && strcmp(key, "host") == 0) {
    Serial.println(_nvs->putCStr(NVS_KEY_MQTT_HOST, value.c_str()) ? "OK" : "ERROR: not stored");
  } else if (strcmp(section, "mqtt") == 0 && strcmp(key, "port") == 0) {
    int32_t port = value.toInt();
    if (port <= 0 || port > 65535) {
      Serial.println("invalid port");
      return;
    }
    _nvs->putInt(NVS_KEY_MQTT_PORT, port);
    Serial.println("OK");
  } else if (strcmp(section, "mqtt") == 0 && strcmp(key, "user") == 0) {
    Serial.println(_nvs->putCStr(NVS_KEY_MQTT_USER, value.c_str()) ? "OK" : "ERROR: not stored");
  } else if (strcmp(section, "mqtt") == 0 && strcmp(key, "pass") == 0) {
    Serial.println(_nvs->putCStr(NVS_KEY_MQTT_PASS, value.c_str()) ? "OK" : "ERROR: not stored");
  } else if (strcmp(section, "enroll") == 0 && strcmp(key, "secret") == 0) {
    // Mirror the server schema (DeviceBootstrapRequest, min 32).
    if (value.length() < 32 || value.length() > 128) {
      Serial.println("secret must be 32..128 chars (minted ones are 43) — 422 otherwise");
      return;
    }
    Serial.println(_nvs->putCStr(NVS_KEY_ENROLL_SECRET, value.c_str()) ? "OK" : "ERROR: not stored");
  } else if (strcmp(section, "bootstrap") == 0 && strcmp(key, "host") == 0) {
    Serial.println(_nvs->putCStr(NVS_KEY_BOOTSTRAP_HOST, value.c_str()) ? "OK" : "ERROR: not stored");
  } else if (strcmp(section, "bootstrap") == 0 && strcmp(key, "port") == 0) {
    int32_t port = value.toInt();
    if (port <= 0 || port > 65535) {
      Serial.println("invalid port");
      return;
    }
    _nvs->putInt(NVS_KEY_BOOTSTRAP_PORT, port);
    Serial.println("OK");
  } else if (strcmp(section, "id") == 0 && strcmp(key, "customer") == 0) {
    Serial.println(_nvs->putCStr(NVS_KEY_ID_CUSTOMER, value.c_str()) ? "OK" : "ERROR: not stored");
  } else if (strcmp(section, "id") == 0 && strcmp(key, "device") == 0) {
    Serial.println(_nvs->putCStr(NVS_KEY_ID_DEVICE, value.c_str()) ? "OK" : "ERROR: not stored");
  } else if (strcmp(section, "relay") == 0 && strcmp(key, "fail_safe") == 0) {
    int v;
    if (value == "on") v = 1;
    else if (value == "off") v = 0;
    else {
      Serial.println("value must be 'on' or 'off'");
      return;
    }
    _nvs->putInt(NVS_KEY_FAIL_SAFE, v);
    Serial.println("OK");
  } else if (strcmp(section, "peer") == 0) {
    cmd_set_peer(key, value);
  } else {
    Serial.print("unknown setting: ");
    Serial.print(section);
    Serial.print(".");
    Serial.println(key);
  }
}

// "set peer <field> <idx> <value>" — idx splits off the front of the
// house-grammar value; the remainder is the setting itself.
void SerialCli::cmd_set_peer(const char* key, const String& value) {
  int sp = value.indexOf(' ');
  if (sp < 0) {
    Serial.println("usage: set peer <mac|secret|interval|adapt|lowv> <idx> <value>");
    return;
  }
  int idx = value.substring(0, sp).toInt();
  String rest = value.substring(sp + 1);
  rest.trim();
  if (idx < 0 || idx >= ESPNOW_MAX_PEERS || rest.length() == 0) {
    Serial.printf("idx must be 0..%d and a value required\n", ESPNOW_MAX_PEERS - 1);
    return;
  }
  if (strcmp(key, "mac") == 0) {
    uint8_t mac[6];
    if (!EspNowLink::parseMac(rest.c_str(), mac)) {
      Serial.println("invalid MAC (want aa:bb:cc:dd:ee:ff or aabbccddeeff)");
      return;
    }
    char macKey[24];
    snprintf(macKey, sizeof(macKey), NVS_KEY_PEER_FMT, idx, PEER_FIELD_MAC);
    Serial.println(_nvs->putCStr(macKey, rest.c_str()) ? "OK" : "ERROR");
  } else if (strcmp(key, "secret") == 0) {
    if (rest.length() > 96) {
      Serial.println("secret too long (max 96)");
      return;
    }
    char sKey[24];
    snprintf(sKey, sizeof(sKey), NVS_KEY_PEER_FMT, idx, PEER_FIELD_SECRET);
    Serial.println(_nvs->putCStr(sKey, rest.c_str()) ? "OK" : "ERROR");
  } else if (strcmp(key, "interval") == 0) {
    int32_t s = rest.toInt();
    if (s < 10 || s > 86400) {
      Serial.println("interval must be 10..86400 seconds");
      return;
    }
    char iKey[24];
    snprintf(iKey, sizeof(iKey), NVS_KEY_PEER_FMT, idx, PEER_FIELD_INTERVAL);
    _nvs->putInt(iKey, s);
    Serial.println("OK");
  } else if (strcmp(key, "adapt") == 0) {
    int32_t a = (rest == "1" || rest == "on") ? 1 : 0;
    char aKey[24];
    snprintf(aKey, sizeof(aKey), NVS_KEY_PEER_FMT, idx, PEER_FIELD_ADAPT);
    _nvs->putInt(aKey, a);
    Serial.println("OK");
  } else if (strcmp(key, "lowv") == 0) {
    float v = rest.toFloat();
    if (v < 1.0f || v > 20.0f) {
      Serial.println("lowv must be volts, 1.0..20.0");
      return;
    }
    char lKey[24];
    snprintf(lKey, sizeof(lKey), NVS_KEY_PEER_FMT, idx, PEER_FIELD_LOWV);
    _nvs->putInt(lKey, (int32_t)(v * 1000.0f));  // millivolts
    Serial.println("OK");
  } else {
    Serial.print("unknown peer setting: ");
    Serial.println(key);
    return;
  }
  proxies.applySlot(idx);
}

void SerialCli::cmd_peers() {
  Serial.println();
  Serial.println("--- ESP-NOW peer slots ---");
  int count = 0;
  unsigned long now = millis();
  for (int n = 0; n < ESPNOW_MAX_PEERS; n++) {
    PeerSlot& s = proxies.slots[n];
    if (!s.provisioned) continue;
    count++;
    char macStr[18];
    EspNowLink::formatMac(s.mac, macStr, sizeof(macStr));
    Serial.printf("peer %d mac=%s\n", n, macStr);
    Serial.printf("  uid    = %s\n", s.uid[0] ? s.uid : "(awaiting hello)");
    Serial.printf("  creds  = %s (cust %s)\n",
                  s.credsValid ? "held" : "none",
                  s.cust[0] ? s.cust : "-");
    Serial.printf("  cfg    = interval=%ld adapt=%ld lowv=%.2f\n",
                  (long)s.cfgInterval, (long)s.cfgAdapt, s.cfgLowV);
    uint32_t rxAge = s.lastRxMs ? (now - s.lastRxMs) / 1000 : 0;
    Serial.printf("  last rx= %lus ago, tx=%s, proxy=%s\n",
                  (unsigned long)rxAge,
                  !s.lastDeliverKnown ? "na"
                    : (s.lastDeliverOk ? "ok" : "fail"),
                  !s.credsValid ? "off"
                    : (s.client && s.client->connected() ? "up" : "down"));
  }
  if (count == 0) {
    Serial.println("(none provisioned — see 'set peer mac', spec §11)");
  }
  Serial.println();
}

void SerialCli::cmd_del(int argc, char** argv) {
  if (argc < 3 || strcmp(argv[1], "peer") != 0) {
    Serial.println("usage: del peer <idx>");
    return;
  }
  int idx = atoi(argv[2]);
  if (idx < 0 || idx >= ESPNOW_MAX_PEERS) {
    Serial.println("idx out of range");
    return;
  }
  char macKey[24], sKey[24], uKey[24], cKey[24], u2Key[24], p2Key[24];
  snprintf(macKey, sizeof(macKey), NVS_KEY_PEER_FMT, idx, PEER_FIELD_MAC);
  snprintf(sKey, sizeof(sKey), NVS_KEY_PEER_FMT, idx, PEER_FIELD_SECRET);
  snprintf(uKey, sizeof(uKey), NVS_KEY_PEER_FMT, idx, PEER_FIELD_UID);
  snprintf(cKey, sizeof(cKey), NVS_KEY_PEER_FMT, idx, PEER_FIELD_CUST);
  snprintf(u2Key, sizeof(u2Key), NVS_KEY_PEER_FMT, idx, PEER_FIELD_USER);
  snprintf(p2Key, sizeof(p2Key), NVS_KEY_PEER_FMT, idx, PEER_FIELD_PASS);
  // Empty string = unset for the loader; secrets overwritten with
  // zeros rather than left on flash.
  _nvs->putCStr(macKey, "");
  _nvs->putCStr(sKey, "");
  _nvs->putCStr(uKey, "");
  _nvs->putCStr(cKey, "");
  _nvs->putCStr(u2Key, "");
  _nvs->putCStr(p2Key, "");
  proxies.applySlot(idx);
  Serial.println("OK");
}

void SerialCli::cmd_selftest() {
  hkdf_selftest();
}

void SerialCli::cmd_relay(int argc, char** argv) {
  if (argc < 3) {
    Serial.println("usage: relay <ch> <on|off>     (ch = 0..3)");
    Serial.println("chips: 0=GPIO16  1=GPIO17  2=GPIO18  3=GPIO19");
    Serial.println("manual control bypasses MQTT. Use this to verify wiring.");
    return;
  }
  int ch = atoi(argv[1]);
  if (ch < 0 || ch >= 4) {
    Serial.println("ch must be 0..3");
    return;
  }
  bool on = (strcmp(argv[2], "on") == 0);
  if (strcmp(argv[2], "on") != 0 && strcmp(argv[2], "off") != 0) {
    Serial.println("value must be 'on' or 'off'");
    return;
  }
  relays.setChannel(ch, on);
  Serial.print("relay ");
  Serial.print(ch);
  Serial.print(" (GPIO");
  const int pins[4] = {16, 17, 18, 19};
  Serial.print(pins[ch]);
  Serial.print(") -> ");
#if RELAY_ACTIVE_LOW
  Serial.print(on ? "ON (drive LOW)" : "OFF (drive HIGH)");
#else
  Serial.print(on ? "ON (drive HIGH)" : "OFF (drive LOW)");
#endif
  Serial.print("  level=");
  Serial.println(digitalRead(pins[ch]) ? "HIGH" : "LOW");
}

void SerialCli::cmd_boot() {
  Serial.println("boot: runtime auto-starts in setup() if config is complete");
  Serial.println("boot: this command is reserved for a future manual restart");
}

void SerialCli::cmd_reset() {
  Serial.println("rebooting");
  delay(300);
  ESP.restart();
}

void SerialCli::cmd_factory() {
  Serial.println("wiping NVS...");
  _nvs->wipe();
  Serial.println("OK; rebooting");
  delay(500);
  ESP.restart();
}
