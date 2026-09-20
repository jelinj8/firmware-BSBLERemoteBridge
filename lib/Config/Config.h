// Persisted device configuration (WiFi credentials, remote-bridge WebSocket URL/channel-name/
// token, TLS-insecure toggle), backed by Preferences (Arduino's NVS wrapper) - same pattern
// firmware-BSHMIEinkDevice uses for its own WiFi-only config, extended with this project's extra
// fields. A persisted value always wins over the compile-time secrets.h fallback (see
// secrets.h.example) - that fallback exists purely for initial dev convenience before the captive
// portal has been used once, and is never re-consulted after a value is persisted.
//
// Provisioning (setting these values in the first place) is via a WiFi AP + captive config page -
// see Config::runProvisioningPortal(), entered when no WiFi config is persisted yet, or when the
// board's BOOT button is held at power-on (see board::kPinButtonBoot).
#pragma once

#include <Arduino.h>

namespace blebridge {
namespace config {

struct Values {
	String wifiSsid;
	String wifiPassword;
	String wsUrl;
	String channelName;
	String bearerToken;
	bool tlsInsecure = false;

	bool hasWifi() const {
		return wifiSsid.length() > 0;
	}
};

// Loads persisted config from NVS, falling back to secrets.h's compile-time constants for any
// field that has never been persisted (checked independently per field, not all-or-nothing).
Values load();

// Persists `values` to NVS; subsequent load() calls return these until changed again.
void save(const Values& values);

// True if kPinButtonBoot is currently held low (active) - sampled once at boot by main.cpp to
// decide whether to force entry into the provisioning portal even if WiFi config already exists.
bool bootButtonHeld();

// Blocks: starts a WiFi AP + a small HTTP form (SSID/password/server URL/channel name/token/TLS-
// insecure checkbox), waits for a submission, persists it via save(), and returns. Intended to be
// called from main.cpp's setup() before the normal WiFi-station/WebSocket bring-up when load()
// returns no WiFi config, or when bootButtonHeld() was true - never re-entered automatically
// afterwards (a re-flash, a NVS erase, or holding the boot button again are the ways back in).
void runProvisioningPortal();

}  // namespace config
}  // namespace blebridge
