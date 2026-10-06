#pragma once

#include <Arduino.h>
#include <nvs.h>

class NvsStore {
public:
  void begin();
  bool getCStr(const char* key, char* buf, size_t bufLen);
  bool putCStr(const char* key, const char* value);
  int32_t getInt(const char* key, int32_t defaultValue = 0);
  bool putInt(const char* key, int32_t value);
  bool hasKey(const char* key);
  bool wipe();
private:
  nvs_handle_t _handle = 0;
  bool _ok = false;
};
