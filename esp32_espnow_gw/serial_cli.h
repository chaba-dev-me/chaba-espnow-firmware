#pragma once

#include <Arduino.h>
#include "nvs_store.h"

class SerialCli {
public:
  void begin(NvsStore* nvs);
  void tick();
  bool runtimeBootRequested() const { return _bootRequested; }
private:
  NvsStore* _nvs = nullptr;
  String _line;
  bool _bootRequested = false;
  bool _echo = true;

  void handleLine();
  void cmd_help();
  void cmd_show();
  void cmd_version();
  void cmd_set(const String& line, int argc, char** argv);
  void cmd_set_peer(const char* key, const String& value);
  void cmd_relay(int argc, char** argv);
  void cmd_peers();
  void cmd_del(int argc, char** argv);
  void cmd_selftest();
  void cmd_boot();
  void cmd_reset();
  void cmd_factory();

  int tokenize(char* buf, char** argv, int maxArgs);
  String getValue(const String& line, int argc, char** argv);
  void printPrompt();
};
