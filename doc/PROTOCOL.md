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

One WS2812 pixel; color encodes two independent things — a steady base color+blink cadence for
WiFi/WebSocket connection state, with a brief flash overlaid on top for every BLE command/
response/event (always wins over the base state for its ~40ms duration).

| State | Pattern |
|---|---|
| Provisioning / AP mode active | slow purple blink |
| Boot / WiFi not configured | slow blue blink |
| WiFi connecting | fast blue blink |
| WiFi up, WebSocket connecting/reconnecting | fast amber blink |
| WebSocket connected, idle | solid dim green |
| Auth rejected (terminal HTTP 401) | solid red |
| **Overlay:** any BLE command/response/event | brief blue flash |

## Config keys (NVS, `Preferences` namespace `"blebridge"`)

`wifi_ssid`, `wifi_pass`, `ws_url`, `channel_name`, `bearer_token`, `tls_insecure` (bool) — set via
the captive-config portal (`lib/Config::runProvisioningPortal()`) or the compile-time
`include/secrets.h` fallback (see `secrets.h.example`).
