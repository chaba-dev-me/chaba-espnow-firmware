#pragma once

#include <Arduino.h>
#include "config.h"
#include "sensors.h"

// Mode / source / cadence policy (spec §2, §7.6). Loaded from NVS
// with spec defaults; the gateway's cfg-on-wake frames rewrite the
// NVS keys, so a re-read picks up pushed config on the next wake.
struct PowerPolicy {
  char profile[12];  // PROFILE_ALWAYS_ON / PROFILE_SLEEP_TX
  char source[10];   // SOURCE_MAINS / SOURCE_BATTERY / SOURCE_SOLAR
  int32_t wakeIntervalS;
  int32_t adapt;      // 0/1
  float adaptLowV;    // volts
};

void powerLoad(PowerPolicy& p);

bool powerIsAlwaysOn(const PowerPolicy& p);
// sleep_tx refuses relays (spec §2): no continuous RX = no fail-safe
// path for a load.
bool powerAllowsRelays(const PowerPolicy& p);

// Effective wake interval for this cycle:
//   - adaptive: source != mains AND adapt AND b < adapt.low_v AND not
//     charging  → ×4 until the pack recovers;
//   - offline backoff: offlineStreak ≥ K failed cycles → double per
//     additional cycle, capped at OFFLINE_BACKOFF_CAP_S;
//   - success resets the streak (caller's job).
uint32_t powerEffectiveIntervalS(const PowerPolicy& p, const SensorData& e,
                                 uint8_t offlineStreak, bool gwSeenThisCycle);

// Enter deep sleep for `seconds`. Never returns; the wake is a boot.
void powerSleepNow(uint32_t seconds);
