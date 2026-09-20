# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

ESP32-C6 firmware (PlatformIO, Arduino framework) that turns a physical board into a **remote BLE
adapter** for `cz.bliksoft.java:common-java-utils-ble` (BSToolbox-BLE): it dials out over WebSocket
to that library's existing "remote BLE adapter" server-side feature (`RemoteAdapterRegistry`,
already deployed and unchanged by this project - see `StorageManagerServer`'s `/ble-remote`
endpoint), authenticates with the same handshake headers `RemoteAdapterClient.java` sends, and then
acts as a BLE **central** driven entirely by the same NDJSON command/response/event vocabulary the
local Rust `ble-bridge` sidecar speaks over stdin/stdout - just carried one-JSON-object-per-
WebSocket-TEXT-frame instead. No changes to BSToolbox-BLE or the server were needed or made; this
is purely a new client speaking an existing protocol.

The wire protocol itself is **not specified in this repo** - `BSToolbox-BLE`'s `CLAUDE.md` and
`ble-bridge/src/main.rs` are the canonical source of truth for every command/response/event field
name. `doc/PROTOCOL.md` here is a thin delta document covering only what this firmware adds on top
(the `pair` command, the `__ble_remote_control__` session-control sentinel, LED semantics, config
keys) - duplicating the full spec here would create exactly the two-source-of-truth drift risk a
single canonical spec is meant to prevent.

Target hardware: nanoESP32-C6 v1.0 (MuseLab). Meant to be generic across similar ESP32-C6 boards in
principle (see `include/boards/board_template.h`), but this is the only board it's run on so far.

## Repository layout

- `lib/BleCentral/` — the actual NimBLE-Arduino central-role driver: scan/connect/disconnect/
  discover_services/read/write/subscribe/unsubscribe/pair, plus the diagnostic group
  (adapter_state/read_rssi/get_mtu/get_connection_parameters/request_connection_parameters).
  `handleCommand(cmd, id, params)` is the single entry point - see its own doc comment for the
  synchronous-vs-deferred-response distinction (`scan`/`pair` respond later via `pollEvents`,
  because their real answer isn't known until a NimBLE callback fires on NimBLE's own FreeRTOS
  host task, not this firmware's main loop).
- `lib/Protocol/` — thin glue: checks `__ble_remote_control__` first (session_reset ->
  `BleCentral::resetSession()` + session_ready ack), otherwise parses the command/id and calls
  `BleCentral::handleCommand`, and drains `BleCentral::pollEvents` every main-loop tick back out
  over `RemoteLink`.
- `lib/RemoteLink/` — the WebSocket client, deliberately mirroring `RemoteAdapterClient.java`'s
  behavior exactly: same handshake headers, 15s connect timeout, 25s ping heartbeat, exponential
  backoff (1s doubling to a 30s cap, reset on success), and a terminal (non-retrying) state
  specifically on HTTP 401 (matching `BleRemoteAuthException`'s contract on the Java side).
- `lib/SessionControl/` — the `__ble_remote_control__` sentinel line format, mirroring
  `SessionControl.java` byte-for-byte.
- `lib/HexCodec/` — hex encode/decode matching `HexCodec.java`'s wire format and leniency
  contract; portable, exercised by the `native` test env (see below).
- `lib/Config/` — persisted config (WiFi/server URL/channel name/token/TLS-insecure toggle) via
  `Preferences`, with a captive WiFi-AP + HTTP-form provisioning portal
  (`runProvisioningPortal()`), entered when no WiFi config is persisted yet or the board's BOOT
  button (GPIO9) is held at power-on.
- `lib/StatusLed/` — the onboard WS2812 LED's non-blocking state machine (WiFi/link connection
  state as a base color+blink pattern, a brief white flash overlaid for every BLE
  command/response/event).
- `lib/Log/` — thin `Serial.printf`-based human-readable logging (`[WIFI]`/`[WS]`/`[BLE]`/`[CFG]`
  tags) - this firmware's serial port is purely a human/debug interface, never a binary transport.
- `include/boards/` — hardware facts (LED pin, boot-button pin) externalized per board, selected
  at build time via `BoardConfig.h` + a `-DBOARD_*` flag, same pattern as the sibling
  `firmware-BSHMIEinkDevice` repo. `boards/nanoesp32_c6.json` is the PlatformIO board *profile*
  (MCU/flash/upload metadata only, no pins - written from scratch, no such profile existed
  upstream).
- `src/main.cpp` — brings up WiFi (or the provisioning portal), then `protocol::begin()` +
  `remotelink::begin()`.

### Build & test

```bash
# Full firmware build (ESP32-C6 target). PlatformIO Core must be >=6.2.0 (see "Known risks" -
# upgraded from the VS Code extension's bundled 6.1.19 for this project). Its CLI lives at
# %USERPROFILE%\.platformio\penv\Scripts\pio.exe if not on PATH.
pio run -e nanoesp32-c6

# Protocol-logic unit tests (HexCodec, SessionControl), host-native, no ESP32 toolchain -
# requires a system C++ compiler (MSVC/LLVM/MinGW); none was available in this project's dev
# environment so far, so this has only been verified to build correctly, not actually run.
pio test -e native

# Flash + watch boot output on real hardware. Must run in a real interactive terminal (PowerShell/
# cmd, not a piped/scripted shell) - `pio device monitor` refuses non-interactive stdin.
pio run -e nanoesp32-c6 -t upload
pio device monitor -e nanoesp32-c6
```

## Known risks / deliberate deviations

- **`pioarduino/platform-espressif32` fork dependency.** The official `platformio/platform-
  espressif32` registry has no Arduino-framework board manifest for ESP32-C6 at all (open upstream
  gap). This is a deliberate, user-approved deviation, not an oversight.
- **PlatformIO Core version.** The pioarduino platform requires PlatformIO Core >=6.2.0; the VS
  Code extension's bundled install was 6.1.19 and was upgraded in place for this project.
- **A real esptool/esp_pylib version-skew bug** in this pioarduino release broke `-t upload`
  (`EsptoolLogger._get_progress_print_file` referenced but never defined - a mismatch between the
  vendored `tool-esptoolpy` and the installed `esp_pylib` package). Worked around by patching that
  one method directly into `tool-esptoolpy`'s own `logger.py`
  (`~/.platformio/packages/tool-esptoolpy/esptool/logger.py`) to return `sys.stdout`. This lives
  outside the repo (in the shared PlatformIO package cache) - if a fresh machine hits the same
  crash on upload, re-apply the same one-method patch.
- **nanoESP32-C6 pin mapping** (LED = GPIO8, BOOT/config-portal-force button = GPIO9) is
  confirmed on the real board (LED) or still only from third-party sources (BOOT button) - see
  `include/boards/board_nanoesp32_c6.h`.
- **GPIO9 (the BOOT button) must never be sampled at boot/reset time.** It's this chip's own
  UART-download-mode strapping pin - holding it during any kind of chip reset (power-on, EN-pin
  reset, or a software-triggered one) makes the ROM bootloader stay in download mode instead of
  ever running this firmware's app code, so a check reached only right after such a reset (an
  earlier version of this firmware did exactly this, to force re-entry into the provisioning
  portal) could never actually fire. `Config::consumeForcePortalHold()` samples it only during
  already-running normal operation (held ~3s), which is safe.
- **WiFi/BLE radio coexistence**: no problems observed in practice (simultaneous WebSocket +
  active BLE central scan/connect/GATT traffic ran for extended periods without any WiFi drop or
  BLE stall during Phase 1/2 verification), but this hasn't been stress-tested under heavy
  simultaneous load on both radios.
- **A real crash was found and fixed during Phase 2 verification**: `WebSocketsClient` delivers a
  NULL `payload` for a plain/graceful WebSocket disconnect (not just handshake rejections), and
  Arduino-ESP32's `String` never allocates a buffer for a NULL-constructed string, so
  `String::c_str()` returned NULL straight into a `printf("%s", ...)` call - newlib's `strlen`
  doesn't guard against that, taking the whole firmware down with a Guru Meditation Error on
  essentially every ordinary reconnect. Fixed in `RemoteLink.cpp`'s `WStype_DISCONNECTED`/
  `WStype_TEXT` handlers. If a similar crash reoccurs (`MEPC` decodes to `strlen`, `RA` to
  `_svfprintf_r` via `riscv32-esp-elf-addr2line` against `tool-esp-rom-elfs`' ROM ELF), suspect a
  NULL `%s` argument the same way.
- **`pair`'s wire shape** (`{"cmd":"pair","id":...,"address":...,"pin":...}`) was defined fresh for
  this firmware, with no Rust-sidecar or Java-side counterpart yet (see BSToolbox-BLE's CLAUDE.md,
  "Planned: programmatic pairing") - worth a short confirmation pass with that repo's maintainer
  before/alongside a future shared implementation.

## Status (2026-09-20)

Verified end-to-end on real hardware:
- Board/LED bring-up (WS2812 on GPIO8).
- Captive-portal provisioning (WiFi AP + HTTP form), including the "keep existing password/token
  if left blank" precedence.
- WiFi station + WebSocket (wss://, real CA validation via a bundled ISRG Root X1 cert, plus a
  tested insecure/ws:// fallback path) against the **real, already-deployed**
  `wss://storage.bliksoft.cz/ble-remote` server - handshake headers, heartbeat, and reconnect all
  confirmed working with zero server-side changes.
- The full BLE central command set - `scan`/`connect`/`disconnect`/`discover_services`/`read`/
  `write`/`subscribe`/`unsubscribe` plus the entire diagnostic group - against a real CrowPanel
  eink device (`firmware-BSHMIEinkDevice`), including exact GATT structure/UUID-format/hex-payload
  round-tripping (a `read` of the standard Device Name characteristic correctly decoded to
  "CrowPanel-3851DC").
- `pair` (programmatic bonding, no OS prompt) against a real MeshCore radio requiring a PIN -
  succeeded on the first real attempt.

The LED BLE-activity overlay (`StatusLed::pulseBleActivity()`) is wired centrally in
`Protocol.cpp`'s `handleIncomingLine`/`sendAndLogOut`, which every incoming command line, outgoing
response, and drained event (`device_found`/`connected`/`disconnected`/`notification`, plus the
deferred `scan`/`pair` responses) all pass through - so it already covers every `BleCentral`
command without needing a pulse call at each individual call site. See `doc/PROTOCOL.md`'s LED
table for the exact colors/timings.

Not yet built: CI/release scaffolding (GitHub+Forgejo workflows), and the `native` Unity tests have
only been confirmed to *build*, never actually *run* (no system C++ compiler in this project's dev
environment so far).
