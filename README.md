# BSBleRemoteBridge

ESP32-C6 firmware (PlatformIO, Arduino framework) that turns a physical board into a **remote BLE
adapter** for [`cz.bliksoft.java:common-java-utils-ble`](https://github.com/jelinj8/BSToolbox-BLE)
(BSToolbox-BLE) — driven over WiFi, exposing any BLE peripheral it can see to a PC-side Java client
exactly as if it were a locally-attached Bluetooth adapter.

## Project goal

BSToolbox-BLE already has a "remote BLE adapter" feature: the machine holding the real Bluetooth
hardware runs a WebSocket client (`RemoteAdapterClient`) that dials out to a server hosting a
`RemoteAdapterRegistry`, so a PC-side Java app can drive that remote hardware via
`registry.adapterFor(name)` exactly as it would a local adapter. This firmware **is** that remote
adapter: it authenticates with the same handshake headers, speaks the same NDJSON command/
response/event protocol the local Rust `ble-bridge` sidecar speaks (one JSON object per WebSocket
TEXT frame instead of a stdin/stdout line), and drives a real BLE **central** stack (NimBLE) on the
ESP32-C6's own radio. No server-side or Java-library changes are needed — this firmware plugs into
the exact same already-deployed protocol.

It also doubles as a live BLE traffic analyzer: every command, response, and event is logged
human-readably over serial, and an onboard LED shows connection state (WiFi/WebSocket) plus a
flash on every BLE command/response/event.

## Supported boards

- **nanoESP32-C6 v1.0** (MuseLab) — the only board this has actually run on real hardware. ESP32-C6,
  CH343 USB-serial + native USB, one onboard WS2812 RGB LED (GPIO8).

Adding a new board means copying `include/boards/board_template.h` to a new `board_<name>.h`,
adding it to `include/BoardConfig.h`'s `#if` chain, and adding a new `platformio.ini` `[env:...]`
with its own `-DBOARD_*` flag (plus a custom `boards/<name>.json` PlatformIO board profile if the
platform's registry has no suitable entry) — not forking the firmware. See `CLAUDE.md` for the
full repository layout.

## Building

```bash
pio run -e nanoesp32-c6                 # build
pio run -e nanoesp32-c6 -t upload       # flash real hardware
pio device monitor -e nanoesp32-c6      # watch boot/serial output (needs a real interactive terminal)
pio test -e native                      # host-native protocol-logic unit tests
```

PlatformIO Core must be >=6.2.0 (see `CLAUDE.md`'s "Known risks"). Real WiFi/server/token config
can be set at compile time via a gitignored `include/secrets.h` (copy `include/secrets.h.example`)
for initial dev convenience, but the normal path is the onboard captive-config portal: it opens
automatically on first boot (no WiFi config yet), or hold the BOOT button for ~3 seconds while the
board is already running normally to force it open again later. **Do not hold BOOT while
resetting/powering on** - that's this chip's own UART-download-mode strap combination, not a way
to reach application code with the button held (see `CLAUDE.md`'s "Known risks"). Once the portal
is open, connect to the `BLE-Bridge-Setup` WiFi network and fill in the form at
`http://192.168.4.1/`.

## Wire protocol

The wire protocol itself — every command/response/event field name — is specified in
[BSToolbox-BLE](https://github.com/jelinj8/BSToolbox-BLE)'s `CLAUDE.md` and
`ble-bridge/src/main.rs`, not duplicated here. `doc/PROTOCOL.md` in this repository covers only the
small set of deltas this firmware adds on top (the `pair` command, the session-control sentinel,
LED semantics, config keys).

## Status

Verified end-to-end on real hardware: board/LED bring-up, captive-portal provisioning, a real WiFi
+ WebSocket link (TLS with real CA validation, or an insecure fallback) against the actual,
already-deployed `wss://storage.bliksoft.cz/ble-remote` server with zero server-side changes, the
full BLE central command set (scan/connect/disconnect/discover_services/read/write/subscribe/
unsubscribe plus the diagnostic group) against a real peripheral, and programmatic pairing
(`pair`, no OS prompt) against a real PIN-protected peripheral. See `CLAUDE.md`'s "Status" section
for the full detail and what's not yet built (CI, LED activity-overlay wiring into every command
call site, and `native` test execution — no system C++ compiler was available to actually run
them, only build them, in this project's dev environment so far).

## License

MIT — see `LICENSE`.
