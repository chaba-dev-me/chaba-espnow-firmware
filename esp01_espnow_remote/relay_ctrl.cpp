#include "relay_ctrl.h"
#include "state.h"
#include "config.h"

RelayCtrl relays;

void RelayCtrl::begin() {
  pinMode(RELAY_PIN_0, OUTPUT);
#if RELAY_ACTIVE_LOW
  digitalWrite(RELAY_PIN_0, HIGH);
#else
  digitalWrite(RELAY_PIN_0, LOW);
#endif
  g_rs.relays[0] = false;
}

void RelayCtrl::setAll(bool on) {
  setChannel(0, on);
}

void RelayCtrl::setChannel(int ch, bool on) {
  if (ch < 0 || ch >= RELAY_COUNT) return;
#if RELAY_ACTIVE_LOW
  digitalWrite(RELAY_PIN_0, on ? LOW : HIGH);
#else
  digitalWrite(RELAY_PIN_0, on ? HIGH : LOW);
#endif
  g_rs.relays[ch] = on;
}

bool RelayCtrl::anyOn() {
  return g_rs.relays[0];
}
