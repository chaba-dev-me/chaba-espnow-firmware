#pragma once

#include <Arduino.h>
#include "config.h"

// Remote node state. The whole struct lives in RTC memory: seq,
// highest_cmd_id_seen, the active-gateway index and the offline
// streak MUST survive deep sleep (they are the monotonic frame
// counter, the replay guard and the backoff counter), and deep sleep
// resets ordinary RAM. Cold power-on zero-initialises the lot, which
// is the correct fresh state. In always_on (never sleeps) RTC attrs
// behave as ordinary RAM.
//
// The `highest_cmd_id_seen` replay guard is anchored to the backend's
// own monotonic command_log counter — no new crypto (spec §7.4).

struct RemoteState {
  // --- RTC-persistent (survive deep sleep) ---
  uint32_t seq = 0;               // monotonic ESP-NOW frame counter
  uint32_t highestCmdIdSeen = 0;  // replay + failover dedup guard
  uint8_t activeGw = 0;           // preferred gateway (list index)
  uint8_t offlineStreak = 0;      // consecutive wakes reaching no gw
  bool haveGwFrameEver = false;   // suppress fail-safe before first hb

  // --- volatile (reset each boot/wake) ---
  bool gwSeen = false;            // this cycle's TX reached a gateway
  int32_t lastHbRssi = 0;
  bool haveHbRssi = false;
  unsigned long lastGwFrameMs = 0;
  uint8_t hbMissActive = 0;       // §8.2 N-counter (always_on)
  uint8_t hbConsecBetter = 0;     // §8.2 M-counter (always_on)
  bool failSafe = true;
  bool failSafeTriggered = false;
  bool relays[RELAY_COUNT] = {false};
  bool relaysChanged = true;
  bool profileAlwaysOn = true;
  uint32_t readingPeriodMs = READING_PERIOD_MS;
  unsigned long bootMs = 0;
  bool forceCycle = false;        // `wake` CLI command
  bool benchHold = false;         // sleep_tx waiting for bootstrap
};

extern RemoteState g_rs;
