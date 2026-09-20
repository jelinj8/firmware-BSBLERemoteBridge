# Wire protocol — deltas only

This is **not** a full protocol specification. The canonical source of truth for every command,
response, and event field name is [BSToolbox-BLE](https://github.com/jelinj8/BSToolbox-BLE)'s
`CLAUDE.md` ("Architecture: the wire protocol") and `ble-bridge/src/main.rs` (the `Command` enum
and response/event shapes). This firmware implements that exact protocol — read those first.
This document covers only what's specific to carrying that protocol over a WebSocket to this
firmware, plus the one command this firmware adds that doesn't exist anywhere else yet.

## Framing

One JSON object per WebSocket TEXT frame, in both directions — not one line of an NDJSON stream.
Otherwise identical: `{"cmd":..., "id":...}` requests, `{"type":"response","id":...,"ok":bool,...}`
responses, `{"type":"device_found"|"connected"|"disconnected"|"notification", ...}` unsolicited
events. Binary values are lowercase hex, no separators, same as `HexCodec.java`/`main.rs`.

`scan` and `pair` are the two commands whose real response doesn't arrive immediately — `scan`'s
response only comes once its `timeout_ms` window actually ends (matching `main.rs`'s own delayed-
response behavior for scan), and `pair`'s only once the bonding handshake completes.

## Session control

A reserved field, `"__ble_remote_control__"`, multiplexed onto the same channel — checked *before*
any `cmd`/`type` dispatch. Values: `"session_reset"` (server → firmware: discard all BLE central
state — disconnect every peripheral, stop any scan, clear cached addresses/pending pairs — this
firmware's equivalent of the local sidecar being killed and respawned) and `"session_ready"`
(firmware → server: ack, sent once `session_reset`'s teardown is complete). Mirrors
BSToolbox-BLE's `SessionControl.java`/`ControlAwarePipe.java` exactly.

## `pair` (new — not part of BSToolbox-BLE yet)

```json
{"cmd": "pair", "id": "...", "address": "AA:BB:CC:DD:EE:FF", "pin": "123456"}
```

Programmatic BLE bonding: supplies `pin` as the passkey automatically when the peripheral requests
one, with no OS-level or physical UI prompt. Response is `{"type":"response","id":...,"ok":true}`
once pairing/authentication completes, or `ok:false` with an `error` message otherwise. The
peripheral must already be connected (via a prior `connect`) before `pair` is sent.

This command's wire shape was defined fresh for this firmware — BSToolbox-BLE's `CLAUDE.md` lists
programmatic pairing as planned-but-not-implemented for the Rust sidecar (see its "Planned:
programmatic pairing" section). Worth a short confirmation pass with that repo before/alongside a
future shared (Rust sidecar + Java `BlePeripheral.pair(...)`) implementation, so field names don't
have to be reverse-engineered out of this firmware's source.

## Onboard LED

One WS2812 pixel (`lib/StatusLed`); color encodes two independent things — a steady base
color+blink cadence for WiFi/WebSocket connection state, with a brief flash overlaid on top for
every BLE command/response/event (always wins over the base state, including over solid red, for
its short duration - a ~40ms interruption doesn't meaningfully hide a steady/slow-blink base
state). "Blink" alternates the listed color with off; "fast" is a 4Hz full cycle (125ms half-
period), "slow" is 1Hz (500ms half-period).

| State | RGB (0-255) | Pattern |
|---|---|---|
| Provisioning / AP mode active | `(30, 0, 40)` purple | slow blink |
| Boot / WiFi not configured | `(0, 0, 40)` dim blue | slow blink |
| WiFi connecting | `(0, 0, 40)` dim blue | fast blink |
| WiFi up, WebSocket connecting/reconnecting | `(40, 20, 0)` amber | fast blink |
| WebSocket connected, idle | `(0, 20, 0)` dim green | solid |
| Auth rejected (terminal HTTP 401) | `(60, 0, 0)` red | solid |
| **Overlay:** any BLE command/response/event | `(0, 60, 220)` blue | ~40ms flash |

The overlay is wired once, centrally, in `Protocol.cpp` (every incoming command line, outgoing
response, and drained event - `device_found`/`connected`/`disconnected`/`notification`, plus the
deferred `scan`/`pair` responses - all pass through the same two functions) rather than at each
individual `BleCentral` command's call site, so a new command can't forget to trigger it.

## BOOT button (`lib/BootButton`)

Two gestures, both sampled only **while the board is already running normally** - never while
resetting/powering it on, which instead triggers this chip's UART-download-mode strap (see the
firmware's own `CLAUDE.md` for why):

- **Short press** (release before ~3s): disconnects every currently-connected BLE peripheral -
  a physical "let go of everything" control.
- **Long hold** (~3s or more): re-opens the provisioning portal (see "Config keys" below) even
  though WiFi/server config already exists.

## Config keys (NVS, `Preferences` namespace `"blebridge"`)

`wifi_ssid`, `wifi_pass`, `ws_url`, `channel_name`, `bearer_token`, `tls_insecure` (bool) — set via
the captive-config portal (`lib/Config::runProvisioningPortal()`) or the compile-time
`include/secrets.h` fallback (see `secrets.h.example`).
