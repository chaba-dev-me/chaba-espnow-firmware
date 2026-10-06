#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

// Gateway-visible ESP-NOW bookkeeping: the `gw` state block (peer
// health, same pattern as the repeater's `router` block) and the
// 60 s heartbeat scheduler that keeps every paired remote's
// fail-safe clock fed (spec §5, §7.5).

// Called from the runtime's tick; sends a heartbeat to every
// provisioned peer on the HEARTBEAT_PERIOD_MS cadence. Heartbeats to
// a sleeping (sleep_tx) remote vanish into the air — harmless, one
// frame per minute; an always_on remote needs them for its fail-safe.
void gwStateTick();

// Adds doc["gw"] = {"peers":[...]} summarizing each slot.
void appendGwBlock(JsonDocument& doc);
