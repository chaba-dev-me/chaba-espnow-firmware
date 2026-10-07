# DESIGN — esp32_espnow_remote (Chaba ESP-NOW remote)

Firmware 0.5.0. The governing document is
[`docs/SPECIFICATION_ESP_NOW.md`](../../docs/SPECIFICATION_ESP_NOW.md);
this file carries the remote half and the bench notes.

## What this sketch is

One sketch, two modes — a single config choice, orthogonal to the
energy source (spec §2):

| profile | behaviour | relays |
|---|---|---|
| `always_on` (default) | radio RX always on; hello on boot, readings every `wake.interval_s`, full command path, 60 s heartbeat fail-safe | allowed |
| `sleep_tx` | TX, then deep sleep the moment the send-callback resolves (walking the gateway list on failure, §8.2); config reaches it only as cfg-on-wake | forbidden — firmware refuses relay work |

Energy sources (`mains`/`battery`/`solar`) change telemetry and the
cadence policy, not the behaviour.

Compiles for classic ESP32 (WROOM spike) and ESP32-C6 from the same
source (arduino-cli core 3.3.12):

| target | flash | static RAM |
|---|---|---|
| esp32:esp32:esp32 | 961 KB (73%) | 49.8 KB (15%) |
| esp32:esp32:esp32c6 | 1078 KB (82%) | 46.6 KB (14%) |

## File map

| file | origin |
|---|---|
| `nvs_store / bootstrap` | verbatim from `esp32_agroiot` (bootstrap is bench-only) |
| `relay_ctrl` | adapted: tracks `g_rs` instead of the gateway's `g_state` |
| `state` | new — `RemoteState` in RTC memory: `seq`, `highestCmdIdSeen`, `activeGw`, `offlineStreak` must survive deep sleep |
| `hkdf` | new — same module as the gateway (both sides derive the LMK) |
| `espnow_link` | new — ordered gateway list, per-gateway LMK, TX-walk paced by the send callback, channel pinning |
| `sensors` | new — ADC energy telemetry; the extension point for real probes |
| `power` | new — mode/source policy, adaptive ×4 stretch, offline backoff, deep-sleep entry |
| `runtime` / `serial_cli` / `config` | new — remote loop + remote CLI subset |

## Load-bearing decisions

- **`highestCmdIdSeen` is the only command gate** — replay guard and
  failover dedup in one, anchored to the backend's `command_log_id`
  (§7.4, §8.4). Lower ids are dropped with a log line, not applied.
- **Deep-sleep persistence via RTC memory**, not NVS: `seq` and the
  counters are written every cycle and NVS would wear.
- **The remote never associates in the field.** WiFi exists ONLY as
  the bench bootstrap path; `benchBootstrap()` disconnects the radio
  before the first wake cycle, and `gw.channel` pinning applies only
  to the unassociated state (§3.1).
- **Handover is stateless from the remote's side**: any heartbeat
  advertising `ho:0` triggers a handover to THAT gateway. This is
  what makes failover (§8), gateway `factory` recovery, and a
  standby gateway's first contact all work with zero bookkeeping.
- **Fail-safe keys on the radio, not the internet** (§7.5): only
  60 s without ANY gateway heartbeat trips it. Starlink can drop and
  scheduled operation continues.

## The sleep_tx wake cycle

```
boot → (bench bootstrap if creds missing) → pin channel → load peers
  hello {v,seq,uid,type:hello,last_cmd_id}   → TX-walk, gw_seen
  RX window 2 s (bounded; hb/cfg/cmd land here)
  reading {v,seq,uid,type:reading,sensors:{…},rssi,up,gw_seen}
  offline streak update; effective interval = base
      ×4 if adapt && b < adapt.low_v && !chg
      ×2^k if offlineStreak ≥ 3 (cap 3600 s)
  deep sleep → wake = reboot through setup()
```

RTC-preserved across all of it: `seq` (monotonic frames),
`highestCmdIdSeen` (replay guard), `activeGw` (failover position),
`offlineStreak` (backoff).

## Energy telemetry keys (inside `sensors`)

| key | meaning | source |
|---|---|---|
| `b` | battery volts | ADC (divider, `ADC_DIV_RATIO`) |
| `chg` | 1 = charging | solar builds: `pv > b + 0.2` |
| `pv` | panel volts | ADC, solar builds |

Real probes (`t`, `h`, level) hook into `appendSensors()` — the
backend stores the block verbatim (migration 055), so no firmware
schema work per new key.

Pin map (re-map in `config.h` per carrier and verify against a meter):

| | classic WROOM DevKitC | C6 carrier (example) |
|---|---|---|
| battery ADC | GPIO34 (ADC1_CH6) | GPIO3 (ADC1_CH3) |
| panel ADC | GPIO35 (ADC1_CH7) | GPIO2 (ADC1_CH2) |
| relays | GPIO16/17/18/19 | re-map — 18/19 are USB on many C6 boards |

## Bench procedure

```
Jax:     register remote uid + enroll secret
remote>  set id device <uid>; set enroll secret <secret>
remote>  set wifi ssid/pass            (bench only — bootstrap path)
remote>  set gw mac 0 <gateway MAC>; set gw channel <site ch>
remote>  set power profile sleep_tx    (or leave always_on)
remote>  set power source solar        (if applicable)
remote>  selftest                      → HKDF A.1 must PASS
remote>  reset                         → bootstraps, first reading, sleeps
```

- Confirm the PWA: remote tile ONLINE with the `sensors` block.
- Ammeter check (§12.6): sleep current, wake burst, and that the
  radio window ends on TX-done, not a timer.
- `wake` runs one cycle on the bench without sleeping (serial
  visibility); `sleep` enters the real deep sleep immediately.

## Battery/solar bench data (to be filled on the bench)

Carrier rule from the spec: **a DevKitC is not a battery carrier**
(10–20 mA through USB-UART + LED swamps deep-sleep µA). Field
battery/solar nodes ship on a bare-module carrier.

| measurement | target | measured |
|---|---|---|
| deep sleep current (bare module) | < 50 µA | — |
| wake burst (TX + window) | ~100 mA × < 2.5 s | — |
| average @ 300 s cadence | < 0.5 mA | — |
| average @ 300 s, adapted (×4) | < 0.15 mA | — |

## Gotchas

- **A `gw.channel` mismatch looks like a dead remote** — frames
  decrypt-fail on the gateway as `frame json parse error`, or never
  arrive. Check channel first, secrets second (`selftest`).
- **Deep sleep loses Serial** mid-print unless `Serial.flush()`
  (handled in `powerSleepNow`).
- **Cold boot vs wake**: RTC attrs are zero only on cold power-on;
  after `reset` (soft) or wake they persist. `factory` wipes NVS but
  RTC counters survive — harmless (seq monotonicity is the point),
  but don't be surprised that `show` keeps its counters.
- **C6 note**: ESP-NOW + LR PHY availability differs between
  targets; this sketch uses the default PHY on both ends. Enabling
  LR is a BOTH-ENDS change and a future bench item.
