// Board definition: nanoESP32-C6 v1.0 (MuseLab, https://github.com/wuxx/nanoESP32-C6).
// See board_template.h for what every field means and how to add a different board.
//
// kPinStatusLed confirmed on the real board (2026-09-20): a rgbLedWrite(8, ...) smoke test blinks
// slow blue as expected. kPinButtonBoot is still only from third-party sources, not yet exercised.
#pragma once

namespace blebridge {
namespace board {

// ---- Identity --------------------------------------------------------------------------------

constexpr const char* kDeviceModel = "nanoESP32-C6-v1.0";
constexpr const char* kNamePrefix = "ble-bridge-";

// ---- Onboard status LED --------------------------------------------------------------------------
//
// One onboard WS2812 RGB pixel on GPIO8 - corroborated by multiple independent third-party
// pinout/community sources for this exact board, not yet confirmed on the physical unit.

constexpr int kPinStatusLed = 8;
constexpr bool kPinStatusLedIsAddressable = true;

// ---- Buttons -------------------------------------------------------------------------------------
//
// GPIO9 is this chip family's standard strapping/BOOT pin, reported as the BOOT button on this
// board by the same third-party sources - like any ESP32 BOOT button, only safely readable once
// the firmware is running (it's sampled for boot-mode selection before that). Doubles as this
// firmware's "hold at boot to force provisioning/AP mode" input - see lib/Config.

constexpr int kPinButtonBoot = 9;

}  // namespace board
}  // namespace blebridge
