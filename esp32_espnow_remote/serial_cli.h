#pragma once

#include <Arduino.h>
#include "nvs_store.h"

class SerialCli {
public:
  void begin(NvsStore* nvs);
  void tick();
private:
  NvsStore* _nvs = nullptr;
  String _line;
  bool _echo = true;

  void handleLine();
  void cmd_help();
  void cmd_show();
  void cmd_version();
  void cmd_set(const String& line, int argc, char** argv);
  void cmd_relay(int argc, char** argv);
  void cmd_gws();
  void cmd_wake();
  void cmd_sleep();
  void cmd_selftest();
  void cmd_reset();
  void cmd_factory();

  int tokenize(char* buf, char** argv, int maxArgs);
  String getValue(const String& line, int argc, char** argv);
  void printPrompt();
};
