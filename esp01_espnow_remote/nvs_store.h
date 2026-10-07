#pragma once

#include <Arduino.h>
#include "config.h"

// Key-value settings store for the ESP-01. The ESP32 line uses NVS;
// the ESP8266 has none — so the same API (same keys, same call
// sites) rides the EEPROM flash emulation. The active region is
// kept in RAM and rewritten whole on every put: bench-rate writes,
// negligible wear.
//
// Record layout: { u8 keyLen, u8 valLen, key[keyLen], val[valLen] }*
// terminated by 0xFF. Keys ≤ 15 chars (the ESP32 NVS cap, kept for
// parity); values ≤ 128 bytes (the enroll secret and mqtt password
// top out at 96).
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
  bool _ok = false;
  uint8_t _buf[NVS_EEPROM_SIZE];
  uint16_t _used = 0;  // bytes of _buf in use; _buf[_used] == 0xFF

  int findRecord(const char* key, uint8_t* valLenOut) const;
  bool eraseRecord(int offset);
  bool flush();
};
