#include "BootButton.h"

#include <Arduino.h>

#include "BoardConfig.h"

namespace blebridge {
namespace bootbutton {

namespace {
constexpr uint32_t kDebounceMs = 50;
constexpr uint32_t kLongHoldMs = 3000;

bool pinReady_ = false;
uint32_t heldSince_ = 0;
bool longFired_ = false;
}  // namespace

Event poll() {
	if (board::kPinButtonBoot < 0) {
		return Event::kNone;
	}
	if (!pinReady_) {
		pinMode(board::kPinButtonBoot, INPUT_PULLUP);
		pinReady_ = true;
	}

	bool held = digitalRead(board::kPinButtonBoot) == LOW;
	if (held) {
		if (heldSince_ == 0) {
			heldSince_ = millis();
			return Event::kNone;
		}
		if (!longFired_ && millis() - heldSince_ >= kLongHoldMs) {
			longFired_ = true;
			return Event::kLongHold;
		}
		return Event::kNone;
	}

	// Released.
	if (heldSince_ == 0) {
		return Event::kNone;
	}
	uint32_t heldFor = millis() - heldSince_;
	bool wasLongHold = longFired_;
	heldSince_ = 0;
	longFired_ = false;
	if (!wasLongHold && heldFor >= kDebounceMs) {
		return Event::kShortPress;
	}
	return Event::kNone;
}

}  // namespace bootbutton
}  // namespace blebridge
