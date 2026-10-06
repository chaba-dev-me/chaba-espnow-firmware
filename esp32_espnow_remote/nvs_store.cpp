#include "nvs_store.h"
#include "config.h"
#include <nvs_flash.h>

void NvsStore::begin() {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    nvs_flash_erase();
    err = nvs_flash_init();
  }
  if (err != ESP_OK) {
    Serial.print("nvs_flash_init failed: ");
    Serial.println(esp_err_to_name(err));
    _ok = false;
    return;
  }
  err = nvs_open(NVS_NS, NVS_READWRITE, &_handle);
  if (err != ESP_OK) {
    Serial.print("nvs_open failed: ");
    Serial.println(esp_err_to_name(err));
    _handle = 0;
    _ok = false;
    return;
  }
  _ok = true;
}

bool NvsStore::getCStr(const char* key, char* buf, size_t bufLen) {
  if (!_ok || _handle == 0 || buf == NULL || bufLen == 0) return false;
  size_t required = bufLen;
  esp_err_t err = nvs_get_str(_handle, key, buf, &required);
  if (err != ESP_OK) return false;
  if (required < bufLen) {
    buf[required] = '\0';
  } else {
    buf[bufLen - 1] = '\0';
  }
  return true;
}

bool NvsStore::putCStr(const char* key, const char* value) {
  if (!_ok || _handle == 0 || value == NULL) return false;
  esp_err_t err = nvs_set_str(_handle, key, value);
  if (err != ESP_OK) return false;
  return nvs_commit(_handle) == ESP_OK;
}

int32_t NvsStore::getInt(const char* key, int32_t defaultValue) {
  if (!_ok || _handle == 0) return defaultValue;
  int32_t v = 0;
  esp_err_t err = nvs_get_i32(_handle, key, &v);
  if (err != ESP_OK) return defaultValue;
  return v;
}

bool NvsStore::putInt(const char* key, int32_t value) {
  if (!_ok || _handle == 0) return false;
  esp_err_t err = nvs_set_i32(_handle, key, value);
  if (err != ESP_OK) return false;
  return nvs_commit(_handle) == ESP_OK;
}

bool NvsStore::hasKey(const char* key) {
  if (!_ok || _handle == 0) return false;
  size_t required = 0;
  esp_err_t err = nvs_get_str(_handle, key, NULL, &required);
  if (err == ESP_OK) return true;
  int32_t v = 0;
  err = nvs_get_i32(_handle, key, &v);
  return err == ESP_OK;
}

bool NvsStore::wipe() {
  if (!_ok || _handle == 0) return false;
  esp_err_t err = nvs_erase_all(_handle);
  if (err != ESP_OK) return false;
  return nvs_commit(_handle) == ESP_OK;
}
