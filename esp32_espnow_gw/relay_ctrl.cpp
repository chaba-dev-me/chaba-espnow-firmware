#include "relay_ctrl.h"
#include "state.h"
#include "config.h"
#include <driver/gpio.h>

RelayCtrl relays;

static const int pins[RELAY_COUNT] = {RELAY_PIN_0, RELAY_PIN_1, RELAY_PIN_2, RELAY_PIN_3};

void RelayCtrl::begin() {
  for (int i = 0; i < RELAY_COUNT; i++) {
    pinMode(pins[i], OUTPUT);
    gpio_set_drive_capability((gpio_num_t)pins[i], GPIO_DRIVE_CAP_3);
#if RELAY_ACTIVE_LOW
    digitalWrite(pins[i], HIGH);
#else
    digitalWrite(pins[i], LOW);
#endif
    g_state.relays[i] = false;
  }
}

void RelayCtrl::setAll(bool on) {
  for (int i = 0; i < RELAY_COUNT; i++) {
    setChannel(i, on);
  }
}

void RelayCtrl::setChannel(int ch, bool on) {
  if (ch < 0 || ch >= RELAY_COUNT) return;
#if RELAY_ACTIVE_LOW
  digitalWrite(pins[ch], on ? LOW : HIGH);
#else
  digitalWrite(pins[ch], on ? HIGH : LOW);
#endif
  g_state.relays[ch] = on;
}

bool RelayCtrl::anyOn() {
  for (int i = 0; i < RELAY_COUNT; i++) {
    if (g_state.relays[i]) return true;
  }
  return false;
}
