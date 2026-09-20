// Template for a new board/hardware definition.
//
// This firmware is meant to be generic across any ESP32 (or similar) module with WiFi + BLE, not
// tied to the nanoESP32-C6 specifically. All board-specific facts - product naming and pin
// assignments - live in one header like this one per board, selected at build time via a `BOARD_*`
// preprocessor define in platformio.ini (see BoardConfig.h). Application code should never
// hardcode a pin number or product name directly - it should read `board::kWhatever` from
// whichever header BoardConfig.h selected.
//
// To add a new board:
//   1. Copy this file to board_<yourboard>.h and fill in every constant below for real.
//   2. Add an `#elif defined(BOARD_<YOURBOARD>) #include "boards/board_<yourboard>.h"` branch to
//      BoardConfig.h.
//   3. Add a new `[env:...]` in platformio.ini with `-DBOARD_<YOURBOARD>` in build_flags, plus a
//      matching `boards/<yourboard>.json` PlatformIO board profile if the platform's registry has
//      no suitable entry already (see the nanoesp32-c6 env for what that board needed and why).
//
// This file is never itself compiled in (no BOARD_TEMPLATE define exists) - it's documentation via
// example, not a fallback.
#pragma once

namespace blebridge {
namespace board {

// ---- Identity --------------------------------------------------------------------------------

constexpr const char* kDeviceModel = "YourBoard-Model-Name";

// Prefix for the default channel name if none is configured, e.g. "YourBoard-" + last 3 MAC bytes.
constexpr const char* kNamePrefix = "YourBoard-";

// ---- Onboard status LED, see lib/StatusLed ------------------------------------------------------
//
// Modeled as a single WS2812-style addressable RGB pixel (driven via Arduino-ESP32's built-in
// neopixelWrite() helper) rather than a plain on/off GPIO LED - this is what most modern ESP32-C-
// series dev boards actually have onboard. A board with a plain two-state LED instead should set
// kPinStatusLedIsAddressable to false and ignore color entirely in StatusLed (not yet implemented -
// no board needing that variant exists yet).

constexpr int kPinStatusLed = -1;                 // TODO; -1 if this board has no onboard LED
constexpr bool kPinStatusLedIsAddressable = true;  // TODO; true = WS2812-style, false = plain GPIO

// ---- Buttons -------------------------------------------------------------------------------------
//
// kPinButtonBoot is read only once the app is already running (see lib/BootButton) - a long hold
// forces the config/provisioning portal open, a short press disconnects all BLE peripherals. If
// this pin doubles as a boot-mode/download-mode strapping pin on your chip (as it does on the
// ESP32 family generally), it must NEVER be sampled at boot/reset time - see lib/BootButton's own
// doc comment for why. -1 if this board exposes no such button.

constexpr int kPinButtonBoot = -1;  // TODO

}  // namespace board
}  // namespace blebridge
