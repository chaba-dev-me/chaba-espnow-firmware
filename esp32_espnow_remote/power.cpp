#include "power.h"
#include "nvs_store.h"
#include <esp_sleep.h>

extern NvsStore nvs;

void powerLoad(PowerPolicy& p) {
  char buf[16];
  strlcpy(p.profile, PROFILE_ALWAYS_ON, sizeof(p.profile));
  if (nvs.getCStr(NVS_KEY_POWER_PROFILE, buf, sizeof(buf)) &&
      strcmp(buf, PROFILE_SLEEP_TX) == 0) {
    strlcpy(p.profile, PROFILE_SLEEP_TX, sizeof(p.profile));
  }
  strlcpy(p.source, SOURCE_MAINS, sizeof(p.source));
  if (nvs.getCStr(NVS_KEY_POWER_SOURCE, buf, sizeof(buf))) {
    if (strcmp(buf, SOURCE_BATTERY) == 0) strlcpy(p.source, SOURCE_BATTERY, sizeof(p.source));
    else if (strcmp(buf, SOURCE_SOLAR) == 0) strlcpy(p.source, SOURCE_SOLAR, sizeof(p.source));
  }
  p.wakeIntervalS = nvs.getInt(NVS_KEY_WAKE_INTERVAL, DEFAULT_WAKE_INTERVAL_S);
  if (p.wakeIntervalS < 10) p.wakeIntervalS = DEFAULT_WAKE_INTERVAL_S;
  if (p.wakeIntervalS > 86400) p.wakeIntervalS = 86400;
  p.adapt = nvs.getInt(NVS_KEY_ADAPT, 0) ? 1 : 0;
  p.adaptLowV = nvs.getInt(NVS_KEY_ADAPT_LOWV, 3300) / 1000.0f;
}

bool powerIsAlwaysOn(const PowerPolicy& p) {
  return strcmp(p.profile, PROFILE_ALWAYS_ON) == 0;
}

bool powerAllowsRelays(const PowerPolicy& p) {
  return powerIsAlwaysOn(p);
}

uint32_t powerEffectiveIntervalS(const PowerPolicy& p, const SensorData& e,
                                 uint8_t offlineStreak, bool gwSeenThisCycle) {
  uint32_t eff = (uint32_t)p.wakeIntervalS;

  // Adaptive cadence: ride out overcast weeks instead of
  // deep-discharging. Charging nodes run the configured interval
  // regardless (§7.6).
  if (p.adapt && strcmp(p.source, SOURCE_MAINS) != 0 && e.haveBatt &&
      !e.charging && e.battV < p.adaptLowV) {
    eff *= ADAPT_STRETCH;
  }

  // Offline backoff: protect the pack during gateway outages.
  if (!gwSeenThisCycle && offlineStreak >= OFFLINE_BACKOFF_K) {
    int extra = offlineStreak - OFFLINE_BACKOFF_K + 1;
    if (extra > 4) extra = 4;  // 2^4 = ×16 → the cap below binds first
    eff <<= extra;
  }

  if (eff > OFFLINE_BACKOFF_CAP_S) eff = OFFLINE_BACKOFF_CAP_S;
  return eff;
}

void powerSleepNow(uint32_t seconds) {
  if (seconds < 10) seconds = 10;
  Serial.printf("power: deep sleep for %lus\n", (unsigned long)seconds);
  Serial.flush();
  esp_sleep_enable_timer_wakeup((uint64_t)seconds * 1000000ULL);
  esp_deep_sleep_start();
  // never returns; wake = reboot through setup()
}
