// Polls the board's BOOT button (board::kPinButtonBoot) for two distinct gestures: a short press
// (release all currently-connected BLE peripherals - a physical "let go of everything" control,
// handy when a peripheral or the remote link gets stuck) and a long hold (force the WiFi/server
// captive-config portal open again, even though it's already configured).
//
// Deliberately only ever sampled during already-running normal operation, never at boot/reset
// time: GPIO9 (kPinButtonBoot on the nanoESP32-C6) is this chip's own UART-download-mode strapping
// pin - holding it during ANY kind of chip reset (power-on, EN-pin/hardware reset, or a software-
// triggered one) makes the ROM bootloader stay in download mode instead of ever running this
// firmware's app code, so a check reached only right after such a reset (an earlier version of
// this firmware did exactly this) could never actually fire. Once the app is already running,
// GPIO9 is a perfectly ordinary, safely-readable GPIO - the same "boot button doubles as a user
// button once running" pattern firmware-BSHMIEinkDevice uses for its own GPIO0.
#pragma once

namespace blebridge {
namespace bootbutton {

enum class Event { kNone, kShortPress, kLongHold };

// Call every main-loop iteration. Returns kLongHold once, the moment the button has been held
// continuously for ~3s (fires while still held, so a long-hold action can start immediately, e.g.
// before the LED needs to show it). Returns kShortPress once, on release, if it was held for at
// least a brief debounce period but never reached the long-hold threshold. Otherwise kNone.
Event poll();

}  // namespace bootbutton
}  // namespace blebridge
