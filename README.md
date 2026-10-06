# Chaba ESP-NOW firmware — gateway & remote (ESP32)

Two Arduino sketches that give a farm a low-power sensor/relay fleet
without running WiFi to every box:

- **`esp32_espnow_gw`** — a mains-powered ESP32 gateway. It is a
  normal relay device on your WiFi/MQTT network *and* it speaks
  ESP-NOW to up to 8 paired remotes, proxying their MQTT traffic.
- **`esp32_espnow_remote`** — a battery/solar/mains ESP32 remote.
  It never joins WiFi in the field: it talks ESP-NOW to the gateway
  and hands its cloud credentials to the gateway once, at pairing.

Both sketches are firmware **v0.5.0** (see `config.h`), tested on the
bench end-to-end: a PWA switch click reaches a remote's relay over
ESP-NOW, and the relay's state change is honestly acked back to the
cloud by `command_log_id`.

---

## 1. How it fits together

```
   [PWA / WhatsApp agent]
            │  MQTT (TLS/WSS at the cloud edge)
   [MQTT broker]                 retained cmd per device
            │
   [GATEWAY  ESP32]  ── its own MQTT client (as itself)
            │   └─ one proxy MQTT client PER remote,
            │      connecting AS that remote
            │  ESP-NOW encrypted unicast (per-pair LMK)
   [REMOTE 1]   [REMOTE 2] …  relay(s), readings, heartbeats
```

Key ideas, in one line each:

- **The topic is the identity.** A remote publishes/reads
  `tenants/<customer_uid>/devices/<device_uid>/{state,cmd}` exactly
  like a direct device — the gateway just carries the bytes. The
  backend never learns ESP-NOW exists.
- **Per-pair key.** The ESP-NOW encryption key (LMK) for a
  gateway↔remote pair is `HKDF-SHA256(remote.enroll_secret,
  salt=gateway_MAC, info="chaba-espnow-v1", 16 bytes)`. Both sides
  derive it from the enroll secret you type at the bench; frames are
  ESP-NOW-encrypted.
- **Claim-once enrollment.** On the bench the remote briefly joins
  WiFi and POSTs `{device_uid, enrollment_secret}` to the backend
  (`/api/v1/devices/bootstrap`). The first device to present a
  never-claimed uid + secret *claims* that identity and receives its
  one-time MQTT credentials. After that, the secret is required every
  time. The credentials are then handed to the gateway over
  ESP-NOW ("handover"), and the remote goes WiFi-free for life.
- **Honest acks.** A remote's reading frame carries the
  `command_log_id` it has applied; the backend acks exactly those
  commands — never anything older still pending.

## 2. Repository layout

```
esp32_espnow_gw/        gateway sketch (folder name must match the .ino)
esp32_espnow_remote/    remote sketch
```

Each sketch folder is self-contained (its own `nvs_store`,
`espnow_link`, `hkdf`, `runtime`, `serial_cli`, …). There is no
shared library to install.

## 3. Hardware

| | Gateway | Remote |
|---|---|---|
| Board | ESP32-WROOM-32 DevKitC (also builds for ESP32-C6) | same |
| Power | mains/USB (always on) | mains, battery or solar |
| Relays | 4× on GPIO16, 17, 18, 19 (active-**LOW**) | 4× when `always_on`; refused in `sleep_tx` |
| Status LED | GPIO2 | GPIO2 |
| Battery sense | — | GPIO34 via 100k/100k divider |
| Solar/PV sense | — | GPIO35 via 100k/100k divider |

ESP32-C6 carriers: the ADC pins switch automatically in `config.h`
(GPIO3 battery, GPIO2 PV) when built for `esp32c6`.

ESP-01 (ESP8266) is **not** supported yet — porting it is on the
roadmap (§12).

## 4. Toolchain

Two supported paths.

### Arduino IDE

1. Boards Manager → install **esp32 by Espressif Systems** (3.x —
   tested with 3.3.x).
2. Library Manager → install **ArduinoJson** (v7).
3. Open `esp32_espnow_gw/esp32_espnow_gw.ino` (or the remote).
4. Board: **ESP32 Dev Module** (WROOM DevKitC) or your C6 board.
5. *(Optional)* copy `secrets.example.h` → `secrets.h` and fill in
   defaults. Everything there can also be set over serial later, so
   many builds skip this file entirely.

### arduino-cli

```bash
arduino-cli core update-index \
  --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core install esp32:esp32 \
  --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli lib install ArduinoJson

# compile (UploadSpeed pinned — some DevKitC boards negotiate badly)
arduino-cli compile --fqbn esp32:esp32:esp32:UploadSpeed=115200 \
  esp32_espnow_remote
# flash
arduino-cli upload -p /dev/ttyUSB0 \
  --fqbn esp32:esp32:esp32:UploadSpeed=115200 esp32_espnow_remote
```

For an ESP32-C6: `--fqbn esp32:esp32:esp32c6:UploadSpeed=115200`.

**Serial monitor:** 115200 baud, e.g.
`arduino-cli monitor -p /dev/ttyUSB0 --config 115200`
(or `screen /dev/ttyUSB0 115200`). If the port refuses or the board
hangs mid-erase, try a known-good USB **data** cable — marginal
cables were the #1 bench time-sink.

## 5. The serial console (both boards)

After flash + reset you get a prompt (`gw>` or `remote>`). Commands
are one per line; `help` lists everything; `show` dumps config;
`selftest` runs the HKDF crypto vectors (must PASS); `factory` wipes
all settings; `reset` reboots keeping settings.

Settings are stored in NVS and **survive reflashes**, so
re-flashing new firmware does not lose your configuration.

Useful everywhere:

```
set wifi ssid <text>        # WiFi (gateway: always; remote: bench bootstrap)
set wifi pass <text>
set id device <device_uid>  # the uid your platform issued
set enroll secret <text>    # 32..128 chars — YOU choose it, once
set bootstrap host <host>   # server serving /api/v1/devices/bootstrap
set bootstrap port <port>   # default 8000
relay <ch> <on|off>         # manual wiring test (ch = 0..3)
```

## 6. Before you start: register two devices

Each physical board is a separate device on the platform (the
gateway **and every remote**). For each one you need a **device_uid**
(UUID). With the Chaba service: message the agent on WhatsApp and ask
to register a device — it returns the uid. Self-hosting the backend:
create the device rows yourself; the bootstrap contract is a single
endpoint:

```
POST http://<server>:8000/api/v1/devices/bootstrap
{"device_uid": "...", "enrollment_secret": "..."}
→ 200 {"mqtt_username","mqtt_password","customer_uid","mqtt_host","mqtt_port"}
```

Generate your own enroll secrets: **32–128 random printable
characters each** (shorter ones are rejected — the serial CLI
checks and the backend answers HTTP 422). Example:
`openssl rand -base64 48 | tr -d '/+=' ` → trim to ~43 chars.

## 7. Gateway setup, step by step

1. **Flash** `esp32_espnow_gw`, open the serial monitor, `reset`.
   The banner shows `ESP32 AgroIoT ESP-NOW gateway v0.5.0`.
2. Configure the bench essentials:

   ```
   set wifi ssid MyFarmWiFi
   set wifi pass …
   set id device <gateway-device-uid>
   set enroll secret <gateway-random-32+>
   set bootstrap host 192.168.1.10     # your backend host
   set bootstrap port 8000
   selftest                            # expect PASS
   reset
   ```

3. On boot the runtime checks its config. If `wifi.ssid`,
   `wifi.pass` or `id.device` are missing it prints exactly what it
   needs and stays at the CLI. With config present:

   - `runtime: started` — it joins WiFi,
   - `bootstrap: requesting config from 192.168.1.10:8000` →
     `bootstrap: configured; mqtt starting` — MQTT credentials,
     broker host/port and `customer_uid` are fetched and stored,
   - `proxy: gateway MAC (HKDF salt) = aa:bb:cc:dd:ee:ff` —
     **write this down**, remotes need it,
   - one `proxy: connecting AS <uid> ... OK` line per paired remote
     (after pairing).

   First boot may log `FAIL rc=5` (bad credentials) for a minute or
   two — the broker's password file is rendered from the device
   table and needs its refresh step (platform operators run the
   auth-apply job). The gateway retries and recovers on its own.

4. **Pair a remote** (once the remote exists, §8):

   ```
   set peer mac 0 <remote-MAC>
   set peer secret 0 <that remote's enroll secret>
   set peer interval 0 60        # config-on-wake: wake cadence, seconds
   set peer adapt 0 0            # 1 = adaptive cadence on low battery
   set peer lowv 0 3.30          # adapt threshold, volts
   ```

   Slot index 0–7 (`peers` lists them; `del peer 0` unpairs). When
   the remote next says hello you'll see
   `peer 0: hello from <uid>` then
   `peer 0: handover stored (creds ok)` and
   `proxy: connecting AS <uid> ... OK`.

## 8. Remote setup, step by step

1. **Flash** `esp32_espnow_remote`, open the monitor, `reset`.
   The boot log prints, right after the banner:

   ```
   espnow: remote MAC = 00:4b:12:3a:31:b0  (this is what 'set peer mac' on the gateway wants)
   ```

2. Configure:

   ```
   set wifi ssid MyFarmWiFi          # bench only — for the one-time bootstrap
   set wifi pass …
   set id device <remote-device-uid>
   set enroll secret <remote-random-32+>
   set bootstrap host 192.168.1.10
   set bootstrap port 8000
   set gw mac 0 <gateway-MAC>        # from the gateway's boot log
   set gw channel 9                  # 1..13 — your site's fixed channel
   set power profile always_on       # or sleep_tx (see §9)
   set power source mains            # mains | battery | solar
   set wake interval 60              # sleep_tx cadence, seconds
   selftest
   reset
   ```

3. On the next boot the remote claims its credentials:

   ```
   bootstrap: claimed; credentials stored for handover
   ```

   then hands them to the gateway (`handover: sending credentials to
   gateway ... ok`). From now on the remote does **not** need WiFi —
   you can remove it from the bench entirely. `show` should end with
   `gw_seen=1`.

4. **Verify the round trip.** Put the relay module on GPIO16–19
   (active-LOW: the board sinks the pin to switch ON — most 5 V relay
   modules with an opto input work directly). Switch the relay from
   the platform (PWA/WhatsApp). Expect on the remote:

   ```
   cmd 1234 (on relay 1) applied; acking with reading
   ```

   and a click. If the command never arrives, see §11 —
   `frame: unknown type ''` means the *gateway* is still on old
   firmware (both sides must be ≥ 0.5.0).

Up to **4 gateways** can be configured (`set gw mac 1 …`) — the list
order is preference; the remote hops after 3 missed heartbeats and
returns after 5 good ones.

## 9. Power profiles (remote)

Two orthogonal settings drive behaviour:

- **`power profile`**
  - `always_on` — radio RX always on, full command path, fail-safe
    (relays drop OFF if no gateway frame for 180 s; disable with
    `set relay fail_safe off`). Use for relay nodes on mains.
  - `sleep_tx` — wakes every `wake interval`, sends a reading,
    listens ~2 s for heartbeats/config/queued commands, deep-sleeps
    on TX-done. **Relay commands are refused** in this profile (the
    radio is asleep between wakes) — use it for sensor-only nodes.
- **`power source`** — `mains`, `battery` or `solar`. With `solar`,
  when the pack is below `adapt lowv` and not charging, the wake
  interval stretches ×4 (`set adapt mode 1`). When the gateway is
  unreachable the interval backs off (doubles after 3 failed cycles,
  capped at 1 h) and the node reports `gw_seen:0`.

Battery/PV telemetry: 100k/100k divider from Batt+ to GPIO34 (ratio
2.0 in `config.h`), panel voltage to GPIO35. `chg` (charging) is
reported when PV exceeds battery + 200 mV. Check `show` against a
multimeter on first power-up and adjust `ADC_DIV_RATIO` if needed.

## 10. What you must copy where (pairing checklist)

| From | Value | To |
|---|---|---|
| remote boot log | `espnow: remote MAC = …` | gateway `set peer mac 0 …` |
| you (chosen at bench) | remote's enroll secret | gateway `set peer secret 0 …` |
| gateway boot log | `proxy: gateway MAC (HKDF salt) = …` | remote `set gw mac 0 …` |
| your site plan | fixed WiFi channel (1–13) | remote `set gw channel N` (gateway's AP pinned to the same) |

Everything else — uids, MQTT creds, customer uid — flows over the
air (bootstrap + hello/handover).

## 11. Troubleshooting

| Symptom | Cause / fix |
|---|---|
| `bootstrap: backend HTTP 422` loop | Enroll secret shorter than 32 chars (or unsafe characters). Set a proper random secret. |
| Gateway `FAIL rc=5` on MQTT for a while after first bootstrap | Broker password file not yet refreshed with the new device credentials. Retries recover it; operators run the auth-apply step. |
| `rc=-2` / `bootstrap: backend connect failed` | Wrong `bootstrap host/port`, or the server isn't reachable from that WiFi. |
| Remote prints `MAC 00:00:00:00:00:00` at boot | Firmware older than 0.5.0 (driver race). Reflash — 0.5.0 reads the MAC from eFuse. |
| Remote: `frame: unknown type ''` bursts; commands never apply | The **gateway** is on firmware without the `type` field. Reflash the gateway to ≥ 0.5.0 (both sides). |
| `peer 0: bad enroll secret, ESP-NOW key not set` | Gateway peer secret ≠ the remote's actual enroll secret. Re-set `set peer secret 0 …`. |
| Remote never says hello (`gw_seen=0`) | Channel mismatch. Pin both sides: gateway's AP channel == remote `gw channel`. On managed/multi-AP WiFi force a fixed channel — "auto" moves under you. |
| Relay ON but the platform says OFF after ~3 min | Fail-safe tripped (no gateway frame for 180 s). Check the gateway↔remote link; `show` on the remote. |
| `cmd …: refused (sleep_tx carries no relays)` | That remote is `sleep_tx` — relay commands are refused by design. Use `always_on` for relay nodes. |
| Garbled serial / IDE unresponsive / erase hangs | Cable or power. Use a known-good **data** USB cable, 115200 baud; avoid DTR/RTS weirdness by using the board's BOOT button if auto-reset fails. |
| `frame: json parse error (wrong LMK?)` | Encrypted garbage — secrets don't match on the pair. Re-do §10. |

## 12. Security notes & roadmap

- `secrets.h` is for **your** defaults only — never commit it (the
  `.gitignore` here already excludes it). Anyone holding a device's
  enroll secret owns that device; treat secrets like passwords.
- Claim-once means a uid that has already claimed cannot be silently
  re-claimed by someone else — later bootstrap calls must present
  the secret.
- MQTT stays plain `1883` inside the site's VPN by design; put TLS
  in front if you expose the broker.
- No OTA (same position as the rest of the fleet) — updates are
  bench reflashes.

**Roadmap:** ESP-01 / ESP8266 remote port (one relay, no native
deep-sleep wake without a solder mod, BearSSL-based HKDF) — tracked
as the next big TODO; credential rotation for deployed remotes via
config-on-wake; interlocks (float switch → pump) as a local rules
engine.

## 13. Version history

- **0.5.0** (2026-10-06) — first public release. Gateway proxy line
  end-to-end (hello/handover/commands/readings/heartbeats/
  config-on-wake, multi-gateway list), remote always_on + sleep_tx
  with adaptive cadence, sensors block (`b`/`chg`/`pv`), honest
  `command_log_id` acks, fail-safe raised to 180 s, RFC 5869
  `selftest`.

*License: not yet assigned — all rights reserved until a LICENSE
file lands.*
