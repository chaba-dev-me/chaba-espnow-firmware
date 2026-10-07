# DESIGN — esp32_espnow_gw (Chaba ESP-NOW gateway)

Firmware 0.5.0. The governing document is
[`docs/SPECIFICATION_ESP_NOW.md`](../../docs/SPECIFICATION_ESP_NOW.md);
this file carries the gateway half and the bench notes.

## What this sketch is

An `esp32_agroiot` device (relays, 60 s heartbeat, retained-cmd boot
recovery, fail-safe, claim-once bootstrap) that additionally runs the
ESP-NOW link and one MQTT proxy connection per paired remote. The
backend never learns ESP-NOW exists: remotes are first-class devices,
their readings/command path flow through this box AS them.

Compiles for classic ESP32 (WROOM, the bench spike) and ESP32-C6 from
the same source (verified on arduino-cli core 3.3.12):

| target | flash | static RAM |
|---|---|---|
| esp32:esp32:esp32 | 969 KB (73%) | 58.5 KB (17%) |
| esp32:esp32:esp32c6 | 1086 KB (82%) | 55.0 KB (16%) |

## File map

| file | origin |
|---|---|
| `nvs_store / wifi_mgr / state / relay_ctrl / mqtt_client / bootstrap` | verbatim from `esp32_agroiot` |
| `runtime` | adapted: ESP-NOW drain, proxy tick, heartbeat scheduler, `gw` block in the state payload, larger JSON buffer |
| `serial_cli` | adapted: peer provisioning + `peers`/`selftest` added to the house grammar |
| `hkdf` | new — RFC 5869 HKDF-SHA256 over mbedTLS HMAC; `selftest` runs the A.1 vector |
| `espnow_link` | new — peer registration (per-peer LMK), RX ring, send-callback-paced sends |
| `proxy` | new — per-remote PubSubClient AS the remote, forwarder queue, hello/handover/reading dispatch, config push |
| `gw_state` | new — 60 s heartbeat scheduler + the `gw` block builder |

## Load-bearing decisions (do not "simplify" these)

- **The proxy connects AS the remote** (`client_id = remote uid`,
  remote's bootstrap-issued credentials from the handover). That is
  why the backend attributes everything correctly (spec §6.2). The
  one-device-one-topic invariant is what the whole backend trusts.
- **The gateway never acks on the send callback.** A MAC ack is not a
  relay flip; only the remote's state publish (carrying
  `command_log_id`) acks a command. The backend's reap loop is the
  retry engine.
- **HKDF salt is THIS gateway's STA MAC** (printed at boot). Both
  sides must agree on the salt; if you re-MAC a gateway (rare), every
  paired remote's LMK changes and peers must be re-secreted at the
  bench.
- **Retained cmds are welcome redeliveries.** Every cmd JSON arriving
  on a proxy's cmd topic is cached + queued for forwarding; the
  remote's `highest_cmd_id_seen` dedup makes duplicates harmless.
- **Peer slots are lists from day one** (`peer.<n>.*`, spec §8.1).

## Frame shapes as implemented (v1)

Uplink (remote → gateway), JSON, ≤250 B, encrypted unicast:

```json
{"v":1,"seq":4821,"uid":"<remote uid>","type":"hello","last_cmd_id":511}
{"v":1,"seq":4822,"uid":"…","type":"handover","cust":"<cust uid>",
 "mqtt_user":"…","mqtt_pass":"…"}            // `cust` added so the
                                              // gateway can build topics
{"v":1,"seq":4823,"uid":"…","type":"reading",
 "sensors":{"b":3.92,"chg":1,"pv":5.1},"relays":{"relay_0":"on"},
 "cmd_id":512,"rssi":-71,"up":84600,"gw_seen":1}
```

`reading` is one frame type carrying whatever the node has: sensor
remotes send `sensors`, relay-remotes send `relays` + `cmd_id` (the
§6.2 "sensor" vs §6.3 "relay" state payloads are derived on the
gateway from which blocks are present).

Downlink (gateway → remote):

```json
{"v":1,"seq":77,"cmd_id":512,"action":"switch_on","relay":1}
{"v":1,"type":"hb","cmd_id":512,"ho":1}
{"v":1,"type":"hb","cmd_id":512,"rel":{"relay_0":"on"}}   // hello reply,
                                                          // relay echo
{"v":1,"type":"cfg","interval_s":300,"adapt":1,"adapt_low_v":3.3,
 "gw_epoch":1}
```

`ho` (0/1) tells the remote whether this gateway holds its
credentials — a 0 triggers the handover, which is also how a standby
gateway (§8) and a post-`factory` gateway get re-provisioned
over the air.

## Bench procedure (matches spec §11)

```
Jax:    register gateway uid + enroll secret (register/by-customer
        + scripts/rotate_device_credential.py --enroll)
gw>     set id device <uid>
gw>     set enroll secret <secret>
gw>     set wifi ssid/pass          (bench AP)
gw>     reset                       → bootstraps, MQTT up
remote> (same 4 steps on the remote sketch first)
gw>     set peer mac 0 <remote MAC>       (MAC prints on the remote boot banner
                                           or 'status'/gws output on the remote)
gw>     set peer secret 0 <remote enroll secret>
```

From here everything is automatic: remote hello → LMK peer is
already registered → handover (or stored creds) → proxy connects AS
the remote → readings appear under the remote's topic →
`get_device_state` shows the `sensors` block.

Config-on-wake pushes:

```
gw> set peer interval 0 60      # remote wakes every 60 s after next hello
gw> set peer adapt 0 1
gw> set peer lowv 0 3.3
```

`selftest` must print `hkdf selftest (RFC 5869 A.1): PASS` before you
trust a pairing that "just doesn't see frames" — a bad secret shows
up as silent JSON parse errors in the gateway log
(`frame json parse error`).

## Bench notes / gotchas

- **Channel.** The gateway's ESP-NOW channel is its AP's channel.
  Pin the site's CAPsMAN APs to ONE channel (HT20) before deploying
  remotes (spec §3.2). At the bench everything rides the bench AP.
- **`peers` is the health view**: last-rx age, tx status, proxy
  up/down, cfg per slot. The same summary ships in every gateway
  state publish under `gw`.
- **RAM.** Each ACTIVE proxy adds a heap WiFiClient + PubSubClient
  (512 B buffer) on top of the static ~58 KB. Eight slots stay well
  inside 320 KB; the measured ceiling on a WROOM is ~a dozen — do
  not raise ESPNOW_MAX_PEERS without re-measuring.
- **DevKitC carrier**: USB power only for the bench; the gateway is
  mains in the field (continuous RX + WiFi STA ≈ 80–120 mA).
- Flash note: the sketch is ~73% on a 4 MB WROOM module at default
  partitioning — no partition table change needed.
