#pragma once

#include <Arduino.h>
#include "config.h"

// Remote node state. On the ESP32 the RTC-persistent fields ride
// RTC_DATA_ATTR; the ESP8266's equivalent is the 128-byte RTC user
// store, which cannot hold a C++ object with constructors — so the
// persistent subset lives in a plain POD block (state.cpp) that is
// restored in setup() and saved before every deep sleep. CRC-guarded:
// a power-on with stale/garbage RTC reads as fresh.
//
// The persistent fields are exactly the ones deep sleep must carry
// across (they are the monotonic frame counter, the replay guard,
// the failover preference and the backoff counter). Cold power-on
// zero-initialises the lot, which is the correct fresh state.
//
// The `highest_cmd_id_seen` replay guard is anchored to the backend's
// own monotonic command_log counter — no new crypto (spec §7.4).

struct RtcPersist {
  uint32_t magic;             // RTC_PERSIST_MAGIC when valid
  uint16_t crc;               // over the fields below
  uint16_t _pad;
  uint32_t seq;               // monotonic ESP-NOW frame counter
  uint32_t highestCmdIdSeen;  // replay + failover dedup guard
  uint8_t activeGw;           // preferred gateway (list index)
  uint8_t offlineStreak;      // consecutive wakes reaching no gw
  uint8_t haveGwFrameEver;    // suppress fail-safe before first hb
};

#define RTC_PERSIST_MAGIC 0x43484231UL  // "CHB1"

struct RemoteState {
  // --- RTC-persistent (survive deep sleep via RtcPersist) ---
  uint32_t seq = 0;
  uint32_t highestCmdIdSeen = 0;
  uint8_t activeGw = 0;
  uint8_t offlineStreak = 0;
  bool haveGwFrameEver = false;

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

// RTC user-memory persistence (state.cpp).
void rtcPersistLoad();   // setup(): RTC → g_rs (CRC-checked)
void rtcPersistSave();   // before every deep sleep: g_rs → RTC
