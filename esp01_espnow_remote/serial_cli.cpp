#include "serial_cli.h"
#include "config.h"
#include "relay_ctrl.h"
#include "state.h"
#include "power.h"
#include "espnow_link.h"
#include "runtime.h"
#include "hkdf.h"

extern NvsStore nvs;
extern RelayCtrl relays;
extern Runtime runtime;

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
  } else if (strcmp(argv[0], "gws") == 0) {
    cmd_gws();
  } else if (strcmp(argv[0], "wake") == 0) {
    cmd_wake();
  } else if (strcmp(argv[0], "sleep") == 0) {
    cmd_sleep();
  } else if (strcmp(argv[0], "selftest") == 0) {
    cmd_selftest();
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
  Serial.print("firmware  = Chaba ESP-NOW remote (ESP-01) v");
  Serial.println(FW_VERSION);
  Serial.print("chip      = ESP8266 (");
  Serial.print(ESP.getCpuFreqMHz());
  Serial.println(" MHz)");
  Serial.print("build     = ");
  Serial.println(__DATE__ " " __TIME__);
  Serial.print("sdk       = ");
  Serial.println(ESP.getSdkVersion());
}

void SerialCli::cmd_help() {
  Serial.println("commands:");
  Serial.println("  help");
  Serial.println("  show");
  Serial.println("  gws            (gateway slots + link health)");
  Serial.println("  version");
  Serial.println("  set wifi ssid <text>          (bench bootstrap path)");
  Serial.println("  set wifi pass <text>");
  Serial.println("  set enroll secret <text>      (claim-once + LMK IKM, 32..128 chars)");
  Serial.println("  set id device <uid>");
  Serial.println("  set bootstrap host <host>");
  Serial.println("  set bootstrap port <port>");
  Serial.println("  set gw mac <idx> <hex>        (ordered gateway list)");
  Serial.println("  set gw channel <n>            (pinned site channel, §3)");
  Serial.println("  set power profile <always_on|sleep_tx>");
  Serial.println("  set power source <mains|battery|solar>");
  Serial.println("  set wake interval <s>");
  Serial.println("  set adapt mode <0|1>");
  Serial.println("  set adapt lowv <volts>");
  Serial.println("  set relay fail_safe <on|off>  (always_on relay remotes)");
  Serial.println("  relay <0> <on|off>            (always_on only; one relay)");
  Serial.println("  wake          (force one reading/hello cycle now)");
  Serial.println("  sleep         (sleep_tx bench: deep sleep now — needs XPD_DCDC→RST mod)");
  Serial.println("  selftest      (HKDF RFC 5869 vectors)");
  Serial.println("  reset         (reboot, keep settings)");
  Serial.println("  factory       (wipe settings + reboot)");
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

  Serial.print("firmware        = Chaba ESP-NOW remote (ESP-01) v");
  Serial.println(FW_VERSION);

  Serial.print("wifi.ssid       = ");
  if (!_nvs->getCStr(NVS_KEY_WIFI_SSID, buf, sizeof(buf))) buf[0] = 0;
  Serial.println(buf);

  printMasked("wifi.pass      ", _nvs, NVS_KEY_WIFI_PASS);
  printMasked("enroll.secret  ", _nvs, NVS_KEY_ENROLL_SECRET);

  Serial.print("mqtt.user       = ");
  if (!_nvs->getCStr(NVS_KEY_MQTT_USER, buf, sizeof(buf))) buf[0] = 0;
  Serial.println(buf);

  printMasked("mqtt.pass      ", _nvs, NVS_KEY_MQTT_PASS);

  Serial.print("id.customer     = ");
  if (!_nvs->getCStr(NVS_KEY_ID_CUSTOMER, buf, sizeof(buf))) buf[0] = 0;
  Serial.println(buf);

  Serial.print("id.device       = ");
  if (!_nvs->getCStr(NVS_KEY_ID_DEVICE, buf, sizeof(buf))) buf[0] = 0;
  Serial.println(buf);

  Serial.print("gw.channel      = ");
  int32_t ch = _nvs->getInt(NVS_KEY_GW_CHANNEL, 0);
  if (ch > 0) Serial.println(ch);
  else Serial.println("(unpinned!)");

  Serial.print("power.profile   = ");
  if (!_nvs->getCStr(NVS_KEY_POWER_PROFILE, buf, sizeof(buf)) || !buf[0]) {
    snprintf(buf, sizeof(buf), "%s (default)", PROFILE_ALWAYS_ON);
  }
  Serial.println(buf);

  Serial.print("power.source    = ");
  if (!_nvs->getCStr(NVS_KEY_POWER_SOURCE, buf, sizeof(buf)) || !buf[0]) {
    snprintf(buf, sizeof(buf), "%s (default)", SOURCE_MAINS);
  }
  Serial.println(buf);

  Serial.print("wake.interval_s = ");
  Serial.println(_nvs->getInt(NVS_KEY_WAKE_INTERVAL, DEFAULT_WAKE_INTERVAL_S));

  Serial.print("adapt / lowv    = ");
  Serial.print(_nvs->getInt(NVS_KEY_ADAPT, 0));
  Serial.print(" / ");
  Serial.println(_nvs->getInt(NVS_KEY_ADAPT_LOWV, 3300) / 1000.0f);

  Serial.print("relay.fail_safe = ");
  Serial.println(_nvs->getInt(NVS_KEY_FAIL_SAFE, 1) ? "on" : "off");

  Serial.println("--- runtime ---");
  Serial.printf("seq=%lu highest_cmd=%lu active_gw=%u offline_streak=%u gw_seen=%d\n",
                (unsigned long)g_rs.seq, (unsigned long)g_rs.highestCmdIdSeen,
                g_rs.activeGw, g_rs.offlineStreak, g_rs.gwSeen ? 1 : 0);
  Serial.println("--- type 'gws' for the gateway list ---");
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
    Serial.println(_nvs->putCStr(NVS_KEY_WIFI_SSID, value.c_str()) ? "OK" : "ERROR");
  } else if (strcmp(section, "wifi") == 0 && strcmp(key, "pass") == 0) {
    Serial.println(_nvs->putCStr(NVS_KEY_WIFI_PASS, value.c_str()) ? "OK" : "ERROR");
  } else if (strcmp(section, "enroll") == 0 && strcmp(key, "secret") == 0) {
    // Mirror the server schema (DeviceBootstrapRequest, min 32):
    // catch a too-short secret at the bench, not in a 422 loop.
    if (value.length() < 32 || value.length() > 128) {
      Serial.println("secret must be 32..128 chars (minted ones are 43) — 422 otherwise");
      return;
    }
    Serial.println(_nvs->putCStr(NVS_KEY_ENROLL_SECRET, value.c_str()) ? "OK" : "ERROR");
  } else if (strcmp(section, "bootstrap") == 0 && strcmp(key, "host") == 0) {
    Serial.println(_nvs->putCStr(NVS_KEY_BOOTSTRAP_HOST, value.c_str()) ? "OK" : "ERROR");
  } else if (strcmp(section, "bootstrap") == 0 && strcmp(key, "port") == 0) {
    int32_t port = value.toInt();
    if (port <= 0 || port > 65535) {
      Serial.println("invalid port");
      return;
    }
    _nvs->putInt(NVS_KEY_BOOTSTRAP_PORT, port);
    Serial.println("OK");
  } else if (strcmp(section, "id") == 0 && strcmp(key, "device") == 0) {
    Serial.println(_nvs->putCStr(NVS_KEY_ID_DEVICE, value.c_str()) ? "OK" : "ERROR");
  } else if (strcmp(section, "gw") == 0 && strcmp(key, "mac") == 0) {
    int sp = value.indexOf(' ');
    if (sp < 0) {
      Serial.println("usage: set gw mac <idx> <aa:bb:cc:dd:ee:ff>");
      return;
    }
    int idx = value.substring(0, sp).toInt();
    String mac = value.substring(sp + 1);
    mac.trim();
    uint8_t bytes[6];
    if (idx < 0 || idx >= ESPNOW_MAX_GWS ||
        !RemoteLink::parseMac(mac.c_str(), bytes)) {
      Serial.printf("usage: set gw mac <0..%d> <valid mac>\n", ESPNOW_MAX_GWS - 1);
      return;
    }
    char nkey[16];
    snprintf(nkey, sizeof(nkey), NVS_KEY_GW_FMT, idx);
    Serial.println(_nvs->putCStr(nkey, mac.c_str()) ? "OK" : "ERROR");
  } else if (strcmp(section, "gw") == 0 && strcmp(key, "channel") == 0) {
    int32_t ch = value.toInt();
    if (ch < 1 || ch > 13) {
      Serial.println("channel must be 1..13 (2.4 GHz, §3)");
      return;
    }
    _nvs->putInt(NVS_KEY_GW_CHANNEL, ch);
    Serial.println("OK (takes effect on next boot/reset)");
  } else if (strcmp(section, "power") == 0 && strcmp(key, "profile") == 0) {
    if (value == PROFILE_ALWAYS_ON || value == PROFILE_SLEEP_TX) {
      _nvs->putCStr(NVS_KEY_POWER_PROFILE, value.c_str());
      Serial.println("OK (takes effect on next boot/reset)");
    } else {
      Serial.println("profile must be 'always_on' or 'sleep_tx'");
    }
  } else if (strcmp(section, "power") == 0 && strcmp(key, "source") == 0) {
    if (value == SOURCE_MAINS || value == SOURCE_BATTERY || value == SOURCE_SOLAR) {
      _nvs->putCStr(NVS_KEY_POWER_SOURCE, value.c_str());
      Serial.println("OK");
    } else {
      Serial.println("source must be 'mains', 'battery' or 'solar'");
    }
  } else if (strcmp(section, "wake") == 0 && strcmp(key, "interval") == 0) {
    int32_t s = value.toInt();
    if (s < 10 || s > 86400) {
      Serial.println("interval must be 10..86400 seconds");
      return;
    }
    _nvs->putInt(NVS_KEY_WAKE_INTERVAL, s);
    Serial.println("OK");
  } else if (strcmp(section, "adapt") == 0 && strcmp(key, "mode") == 0) {
    _nvs->putInt(NVS_KEY_ADAPT, (value == "1" || value == "on") ? 1 : 0);
    Serial.println("OK");
  } else if (strcmp(section, "adapt") == 0 && strcmp(key, "lowv") == 0) {
    float v = value.toFloat();
    if (v < 1.0f || v > 20.0f) {
      Serial.println("lowv must be volts, 1.0..20.0");
      return;
    }
    _nvs->putInt(NVS_KEY_ADAPT_LOWV, (int32_t)(v * 1000.0f));
    Serial.println("OK");
  } else if (strcmp(section, "relay") == 0 && strcmp(key, "fail_safe") == 0) {
    if (value != "on" && value != "off") {
      Serial.println("value must be 'on' or 'off'");
      return;
    }
    _nvs->putInt(NVS_KEY_FAIL_SAFE, value == "on" ? 1 : 0);
    Serial.println("OK");
  } else {
    Serial.print("unknown setting: ");
    Serial.print(section);
    Serial.print(".");
    Serial.println(key);
  }
}

void SerialCli::cmd_relay(int argc, char** argv) {
  PowerPolicy p;
  powerLoad(p);
  if (!powerAllowsRelays(p)) {
    Serial.println("refused: sleep_tx carries no relays (spec §2)");
    return;
  }
  if (argc < 3) {
    Serial.println("usage: relay <0> <on|off>   (the ESP-01 has one relay, ch 0)");
    return;
  }
  int ch = atoi(argv[1]);
  if (ch < 0 || ch >= RELAY_COUNT) {
    Serial.println("ch out of range (0..0 — one relay on this board)");
    return;
  }
  if (strcmp(argv[2], "on") != 0 && strcmp(argv[2], "off") != 0) {
    Serial.println("value must be 'on' or 'off'");
    return;
  }
  relays.setChannel(ch, strcmp(argv[2], "on") == 0);
  Serial.println("OK");
}

void SerialCli::cmd_gws() {
  Serial.println();
  Serial.println("--- gateway slots (settings order = preference) ---");
  int count = 0;
  for (int i = 0; i < ESPNOW_MAX_GWS; i++) {
    char nkey[16];
    char buf[24];
    snprintf(nkey, sizeof(nkey), NVS_KEY_GW_FMT, i);
    if (!_nvs->getCStr(nkey, buf, sizeof(buf)) || !buf[0]) continue;
    count++;
    Serial.printf("gw.%d = %s%s\n", i, buf,
                  i == g_rs.activeGw ? "   <-- active" : "");
  }
  if (count == 0) {
    Serial.println("(none — 'set gw mac 0 <gateway MAC>', spec §11)");
  }
  Serial.printf("runtime: paired=%d, last hb rssi=%ld\n",
                espnow.gwCount(), (long)g_rs.lastHbRssi);
  Serial.println();
}

void SerialCli::cmd_wake() {
  g_rs.forceCycle = true;
  Serial.println("wake: cycle queued (runs on the next loop tick)");
}

void SerialCli::cmd_sleep() {
  PowerPolicy p;
  powerLoad(p);
  if (powerIsAlwaysOn(p)) {
    Serial.println("refused: always_on never sleeps (RX is the point)");
    return;
  }
  char src[10] = SOURCE_MAINS;
  char buf[16];
  if (_nvs->getCStr(NVS_KEY_POWER_SOURCE, buf, sizeof(buf)) && buf[0]) {
    snprintf(src, sizeof(src), "%s", buf);
  }
  SensorData e;
  sensorsRead(e, src);
  // Same policy math as the wake cycle (adapt stretch + offline
  // backoff included).
  uint32_t eff = powerEffectiveIntervalS(p, e, g_rs.offlineStreak, true);
  powerSleepNow(eff);
}

void SerialCli::cmd_selftest() {
  hkdf_selftest();
}

void SerialCli::cmd_reset() {
  Serial.println("rebooting");
  delay(300);
  ESP.restart();
}

void SerialCli::cmd_factory() {
  Serial.println("wiping settings...");
  _nvs->wipe();
  Serial.println("OK; rebooting");
  delay(500);
  ESP.restart();
}
