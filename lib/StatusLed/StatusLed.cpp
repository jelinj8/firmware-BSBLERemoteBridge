#include "StatusLed.h"

namespace blebridge {
namespace statusled {

namespace {

constexpr uint32_t kSlowBlinkHalfPeriodMs = 500;   // 1 Hz full cycle
constexpr uint32_t kFastBlinkHalfPeriodMs = 125;   // 4 Hz full cycle
constexpr uint32_t kBleActivityPulseMs = 40;

int pin_ = -1;
bool provisioning_ = false;
WifiState wifiState_ = WifiState::kNotConfigured;
LinkState linkState_ = LinkState::kIdle;
uint32_t blePulseStartedAt_ = 0;
bool blePulseActive_ = false;

struct Color {
	uint8_t r, g, b;
};

constexpr Color kOff{0, 0, 0};
constexpr Color kBlue{0, 0, 40};
constexpr Color kPurple{30, 0, 40};
constexpr Color kAmber{40, 20, 0};
constexpr Color kGreen{0, 20, 0};
constexpr Color kRed{60, 0, 0};
constexpr Color kWhite{255, 255, 255};

void write(const Color& c) {
	rgbLedWrite(pin_, c.r, c.g, c.b);
}

// Solid colors always show steady; blinking colors alternate with kOff at the given half-period.
bool blinkOn(uint32_t halfPeriodMs) {
	return (millis() / halfPeriodMs) % 2 == 0;
}

}  // namespace

void begin(int pin) {
	pin_ = pin;
	if (pin_ >= 0) {
		write(kOff);
	}
}

void setProvisioning(bool active) {
	provisioning_ = active;
}

void setWifiState(WifiState state) {
	wifiState_ = state;
}

void setLinkState(LinkState state) {
	linkState_ = state;
}

void pulseBleActivity() {
	blePulseActive_ = true;
	blePulseStartedAt_ = millis();
}

void loop() {
	if (pin_ < 0) {
		return;
	}

	if (blePulseActive_) {
		if (millis() - blePulseStartedAt_ < kBleActivityPulseMs) {
			write(kWhite);
			return;
		}
		blePulseActive_ = false;
	}

	// Priority order, highest first: provisioning mode, then a terminal auth failure (nothing else
	// matters until reconfigured), then WiFi bring-up, then the WebSocket link, then idle-connected.
	if (provisioning_) {
		write(blinkOn(kSlowBlinkHalfPeriodMs) ? kPurple : kOff);
		return;
	}
	if (linkState_ == LinkState::kAuthRejected) {
		write(kRed);
		return;
	}
	if (wifiState_ == WifiState::kNotConfigured) {
		write(blinkOn(kSlowBlinkHalfPeriodMs) ? kBlue : kOff);
		return;
	}
	if (wifiState_ == WifiState::kConnecting) {
		write(blinkOn(kFastBlinkHalfPeriodMs) ? kBlue : kOff);
		return;
	}
	// WiFi is connected from here on.
	if (linkState_ != LinkState::kConnected) {
		write(blinkOn(kFastBlinkHalfPeriodMs) ? kAmber : kOff);
		return;
	}
	write(kGreen);
}

}  // namespace statusled
}  // namespace blebridge
