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
// One onboard WS2812 RGB pixel on GPIO8 - confirmed on the real board (2026-09-20).

constexpr int kPinStatusLed = 8;
constexpr bool kPinStatusLedIsAddressable = true;

// ---- Buttons -------------------------------------------------------------------------------------
//
// GPIO9 is this chip family's standard UART-download-mode strapping pin, reported as the BOOT
// button on this board by third-party sources - held LOW during any chip reset, it keeps the ROM
// bootloader in download mode instead of running this firmware at all, so it must only ever be
// sampled once the app is already running (never at boot/reset time) - see lib/BootButton, which
// owns both of this button's gestures (long hold: force the config portal open; short press:
// disconnect all BLE peripherals).

constexpr int kPinButtonBoot = 9;

}  // namespace board
}  // namespace blebridge
