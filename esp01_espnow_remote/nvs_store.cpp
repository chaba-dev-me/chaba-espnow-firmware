#include "nvs_store.h"
#include "config.h"
#include <EEPROM.h>

void NvsStore::begin() {
  EEPROM.begin(NVS_EEPROM_SIZE);  // void on this core; commit() reports errors
  for (uint16_t i = 0; i < NVS_EEPROM_SIZE; i++) {
    _buf[i] = EEPROM.read(i);
  }
  // Scan for the 0xFF terminator; a blank/wiped flash reads 0xFF at
  // offset 0 (valid empty store). Runaway garbage with no terminator
  // is treated as a corrupt store and wiped to empty.
  _used = 0;
  while (_used < NVS_EEPROM_SIZE && _buf[_used] != 0xFF) {
    uint8_t keyLen = _buf[_used];
    uint8_t valLen = _buf[_used + 1];
    if (keyLen == 0 || keyLen > 15 ||
        _used + 2u + keyLen + valLen >= NVS_EEPROM_SIZE) {
      Serial.println("settings: corrupt record — wiping");
      _used = 0;
      break;
    }
    _used += 2u + keyLen + valLen;
  }
  _ok = true;
}

int NvsStore::findRecord(const char* key, uint8_t* valLenOut) const {
  if (!_ok || key == NULL) return -1;
  size_t keyLen = strlen(key);
  if (keyLen == 0 || keyLen > 15) return -1;
  uint16_t off = 0;
  while (off < _used) {
    uint8_t kLen = _buf[off];
    uint8_t vLen = _buf[off + 1];
    if (kLen == keyLen && memcmp(_buf + off + 2, key, keyLen) == 0) {
      if (valLenOut) *valLenOut = vLen;
      return off;
    }
    off += 2u + kLen + vLen;
  }
  return -1;
}

bool NvsStore::eraseRecord(int offset) {
  if (offset < 0) return false;
  uint8_t kLen = _buf[offset];
  uint8_t vLen = _buf[offset + 1];
  uint16_t recLen = 2u + kLen + vLen;
  memmove(_buf + offset, _buf + offset + recLen, _used - offset - recLen);
  _used -= recLen;
  return true;
}

bool NvsStore::flush() {
  if (!_ok) return false;
  if (_used + 1 >= NVS_EEPROM_SIZE) return false;
  _buf[_used] = 0xFF;
  for (uint16_t i = 0; i <= _used; i++) {
    EEPROM.write(i, _buf[i]);
  }
  return EEPROM.commit();
}

bool NvsStore::getCStr(const char* key, char* out, size_t outLen) {
  uint8_t vLen = 0;
  int off = findRecord(key, &vLen);
  if (off < 0) return false;
  if (outLen == 0) return false;
  size_t copy = (vLen < outLen - 1) ? vLen : outLen - 1;
  memcpy(out, _buf + off + 2 + strlen(key), copy);
  out[copy] = 0;
  return true;
}

bool NvsStore::putCStr(const char* key, const char* value) {
  if (!_ok || key == NULL || value == NULL) return false;
  size_t keyLen = strlen(key);
  size_t valLen = strlen(value);
  if (keyLen == 0 || keyLen > 15 || valLen > 128) return false;
  eraseRecord(findRecord(key, NULL));
  if (_used + 2u + keyLen + valLen >= NVS_EEPROM_SIZE) return false;
  _buf[_used] = (uint8_t)keyLen;
  _buf[_used + 1] = (uint8_t)valLen;
  memcpy(_buf + _used + 2, key, keyLen);
  memcpy(_buf + _used + 2 + keyLen, value, valLen);
  _used += 2u + keyLen + valLen;
  return flush();
}

int32_t NvsStore::getInt(const char* key, int32_t defaultValue) {
  uint8_t vLen = 0;
  int off = findRecord(key, &vLen);
  if (off < 0 || vLen != 4) return defaultValue;
  const uint8_t* p = _buf + off + 2 + strlen(key);
  return (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                   ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
}

bool NvsStore::putInt(const char* key, int32_t value) {
  uint8_t le[4] = {
    (uint8_t)((uint32_t)value & 0xFF),
    (uint8_t)(((uint32_t)value >> 8) & 0xFF),
    (uint8_t)(((uint32_t)value >> 16) & 0xFF),
    (uint8_t)(((uint32_t)value >> 24) & 0xFF),
  };
  eraseRecord(findRecord(key, NULL));
  size_t keyLen = strlen(key);
  if (!_ok || keyLen == 0 || keyLen > 15) return false;
  if (_used + 2u + keyLen + 4 >= NVS_EEPROM_SIZE) return false;
  _buf[_used] = (uint8_t)keyLen;
  _buf[_used + 1] = 4;
  memcpy(_buf + _used + 2, key, keyLen);
  memcpy(_buf + _used + 2 + keyLen, le, 4);
  _used += 2u + keyLen + 4;
  return flush();
}

bool NvsStore::hasKey(const char* key) {
  return findRecord(key, NULL) >= 0;
}

bool NvsStore::wipe() {
  if (!_ok) return false;
  _used = 0;
  _buf[0] = 0xFF;
  for (uint16_t i = 0; i < NVS_EEPROM_SIZE; i++) {
    EEPROM.write(i, 0xFF);
  }
  return EEPROM.commit();
}
