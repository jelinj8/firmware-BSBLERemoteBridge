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
- **Long Phomemo `d-series` prints (PTLabelPrint, `DSeriesPrinter`) silently failed through this
  bridge past roughly 28-40mm - root cause found and fixed, 2026-09-20 (full investigation,
  several real bugs found and fixed along the way, until this bridge's working length matched a
  real OS BLE stack's own ~80mm ceiling exactly).** The fixes below (each a genuine, separate bug,
  not just one thing) in the order they were found:
  - `NimBLERemoteValueAttribute::writeValue()` only takes the plain ATT "Write Command" path
    (`ble_gattc_write_no_rsp_flat`, a single unacknowledged packet) when the payload fits within
    the connection's negotiated MTU minus 3 bytes; anything larger silently falls through to
    `ble_gattc_write_long` - an ATT prepare-write/execute-write sequence - **regardless of the
    caller's `with_response=false`**. A real OS BLE stack negotiates a large enough MTU that
    PTLabelPrint's fixed 128-byte chunks never hit this; this NimBLE central isn't guaranteed to.
    Fixed in `writeRespectingMtu()` (`BleCentral.cpp`, shared by `write`/`write_stream`): a
    `with_response=false` write now fragments to `client->getMTU() - 3` itself, every fragment a
    genuine ATT Write Command, never letting `writeValue()` silently swap protocols underneath it.
  - Every command/response/event line was logged in full, hex payload and all - at 115200 baud
    that's ~55-60ms of *blocking* Serial time per write chunk alone, scaling directly with job
    length. Log.h's own doc comment already described an INFO/VERBOSE split for exactly this
    reason, but nothing actually implemented it. Fixed in `Protocol.cpp`'s `compactForLog()| -
    payload hex is compacted to a byte count at the default INFO level (VERBOSE still gets the
    full raw line); the full line is still always what's actually sent over the wire.
  - Added a `write_stream` command (`{"cmd":"write_stream",...,"value_hex":<whole payload>,
    "chunk_size":128,"chunk_delay_ms":20}`, see `BlePeripheral.writeCharacteristicStream`'s doc in
    BSToolbox-BLE) so a one-way, ack-free protocol like this one pays one WebSocket round trip for
    the *entire* transfer instead of one per chunk - measured 601ms for a 40mm/3840-byte print,
    down from 2.2-3.2s of per-chunk round trips. Deliberately *not* built by pipelining repeated
    `write` commands without waiting for each response: `ble-bridge/src/main.rs`'s command loop
    spawns each incoming line as an independent concurrent task with no ordering guarantee between
    them, so pipelined writes could reach the peripheral out of order and corrupt the raster stream
    - `write_stream` keeps the whole chunk-by-chunk sequence inside one command's own sequential
    handling instead, on both the Rust sidecar and this firmware.
  - Also requests a tight BLE connection interval on every connect (`updateConnParams(6,12,0,400)`
    - NimBLE's own default measured 50ms against a real peripheral, dropped to 15ms once requested)
    and bumped NimBLE's internal `msys_1` mbuf pool 4x (`CONFIG_BT_NIMBLE_MSYS1_BLOCK_COUNT`,
    `platformio.ini`'s `build_flags`) - both worth keeping as general link-quality improvements, but
    neither turned out to be the actual remaining cause below (confirmed by testing each in
    isolation - the failure point didn't move either time).
  - **The actual remaining cause, found after `write_stream` moved the symptom from "silently
    doesn't print" to "the whole bridge hangs - nothing responds, not even WiFi/WebSocket - until a
    hard restart"**: `links2004/WebSockets` (`RemoteLink.cpp`'s `ws_`) does **not** reassemble a
    fragmented WebSocket TEXT message into one final `WStype_TEXT` event - each fragment arrives as
    its own event (`WStype_FRAGMENT_TEXT_START`/`WStype_FRAGMENT`/`WStype_FRAGMENT_FIN`, see
    `WebSocketsClient::messageReceived`), carrying only that fragment's own payload; the caller is
    responsible for concatenating them itself. `handleEvent` had no case for any of these three -
    they fell through to `default: break` and were silently dropped, one piece at a time, while the
    underlying WebSocket/TCP connection stayed perfectly healthy throughout (confirmed on real
    hardware: heartbeat pings/pongs kept flowing normally during a "hang" - the connection was never
    actually stuck, the *message* just vanished before ever reaching `handleIncomingLine`).
    `write_stream`'s whole-payload-in-one-command design (itself a correct fix for the *separate*,
    earlier per-chunk-round-trip-latency bug - see above) sends one message far bigger than any
    individual per-chunk write ever was; once a message crosses whatever fragmentation threshold the
    *sending* WebSocket client uses, it arrives here in pieces that this bridge was silently eating.
    A short enough label's `write_stream` command stays under that threshold and arrives as a single
    `WStype_TEXT` event (works even before this fix); a longer one doesn't. Fixed by accumulating
    `WStype_FRAGMENT_TEXT_START`/`WStype_FRAGMENT` into a buffer and treating `WStype_FRAGMENT_FIN`
    like a completed `WStype_TEXT` event, once the whole message is reassembled.

  **Verified end-to-end on real hardware**: 50mm and 80mm Phomemo prints through this bridge both
  now print correctly (previously: silent failure/hang from roughly 28-40mm on). 80mm is also this
  bridge's practical ceiling now - matching, not falling short of, what a real OS BLE stack (local
  or PC-hosted-remote) already tops out at for the same printer, confirming that remaining limit is
  the Q30/Phomemo `d-series` protocol's own, unrelated to this bridge.

  **`write_stream`'s whole-transfer blocking call still has no timeout** - a genuine stall inside it
  (not the fragment-drop bug above, which is now fixed, but some *other* future problem) would still
  hang the entire bridge same as before. Worth adding a bounded deadline around it at some point, but
  deliberately not done blindly here now that the actual cause of the hangs seen during this
  investigation is understood and fixed.
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
- Long Phomemo `d-series` label prints (PTLabelPrint, via `write_stream`) against a real Phomemo
  Q30, up to 80mm - see "Known risks" above for the investigation that got this working (was
  silently failing/hanging from ~28-40mm on before the fixes described there).

The LED BLE-activity overlay (`StatusLed::pulseBleActivity()`) is wired centrally in
`Protocol.cpp`'s `handleIncomingLine`/`sendAndLogOut`, which every incoming command line, outgoing
response, and drained event (`device_found`/`connected`/`disconnected`/`notification`, plus the
deferred `scan`/`pair` responses) all pass through - so it already covers every `BleCentral`
command without needing a pulse call at each individual call site. See `doc/PROTOCOL.md`'s LED
table for the exact colors/timings.

Not yet built: CI/release scaffolding (GitHub+Forgejo workflows), and the `native` Unity tests have
only been confirmed to *build*, never actually *run* (no system C++ compiler in this project's dev
environment so far).
