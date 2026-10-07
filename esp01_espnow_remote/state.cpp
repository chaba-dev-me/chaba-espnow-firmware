#include "state.h"

RemoteState g_rs;

// Plain RAM mirror of the POD block: ESP.rtcUserMemoryRead/Write
// wants 32-bit-aligned uint32_t*, and the struct itself must be
// trivially copyable — it is (all POD fields).
static RtcPersist s_persist;

static uint16_t persistCrc() {
  // CRC-16/CCITT over everything after the crc field.
  const uint8_t* p = reinterpret_cast<const uint8_t*>(&s_persist) + 6;
  size_t len = sizeof(RtcPersist) - 6;
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= (uint16_t)p[i] << 8;
    for (uint8_t b = 0; b < 8; b++) {
      crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021)
                           : (uint16_t)(crc << 1);
    }
  }
  return crc;
}

static void packFromState() {
  memset(&s_persist, 0, sizeof(s_persist));
  s_persist.magic = RTC_PERSIST_MAGIC;
  s_persist.seq = g_rs.seq;
  s_persist.highestCmdIdSeen = g_rs.highestCmdIdSeen;
  s_persist.activeGw = g_rs.activeGw;
  s_persist.offlineStreak = g_rs.offlineStreak;
  s_persist.haveGwFrameEver = g_rs.haveGwFrameEver ? 1 : 0;
  s_persist.crc = persistCrc();
}

static void unpackToState(bool valid) {
  if (valid && s_persist.magic == RTC_PERSIST_MAGIC &&
      s_persist.crc == persistCrc()) {
    g_rs.seq = s_persist.seq;
    g_rs.highestCmdIdSeen = s_persist.highestCmdIdSeen;
    g_rs.activeGw = s_persist.activeGw;
    g_rs.offlineStreak = s_persist.offlineStreak;
    g_rs.haveGwFrameEver = s_persist.haveGwFrameEver != 0;
  }
  // Invalid CRC/magic → leave the zero-initialised defaults: the
  // correct fresh state after a cold power-on.
}

void rtcPersistLoad() {
  bool ok = ESP.rtcUserMemoryRead(0, (uint32_t*)&s_persist,
                                  sizeof(s_persist));
  unpackToState(ok);
}

void rtcPersistSave() {
  packFromState();
  ESP.rtcUserMemoryWrite(0, (uint32_t*)&s_persist, sizeof(s_persist));
}
