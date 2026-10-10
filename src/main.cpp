// BSBleRemoteBridge firmware entry point. See doc/PROTOCOL.md for the wire-protocol deltas this
// firmware adds on top of BSToolbox-BLE's ble-bridge NDJSON protocol, and CLAUDE.md for the
// project's phased build order and known risks.
#include <Arduino.h>
#include <WiFi.h>

#include "BleCentral.h"
#include "BoardConfig.h"
#include "BootButton.h"
#include "Config.h"
#include "Log.h"
#include "Protocol.h"
#include "RemoteLink.h"
#include "StatusLed.h"

using namespace blebridge;

namespace {

config::Values cfg;

// A fresh WiFi.begin() after this long without a connection. Arduino-ESP32 (3.3) retries on its own
// only once after a failure, then only for the disconnect reasons it deems transient - after e.g.
// AUTH_FAIL (an AP that briefly rejects while restarting) it never tries again, and a single
// begin() then waits forever: a bridge that had run for a long time was found stuck that way
// (fast blue, never reconnecting until power-cycled).
constexpr uint32_t kWifiRetryMs = 30000;
// Last resort: reboot after this long without WiFi, in case the stack itself is wedged.
constexpr uint32_t kWifiRestartMs = 10UL * 60UL * 1000UL;

void beginWifi() {
	WiFi.mode(WIFI_STA);
	WiFi.begin(cfg.wifiSsid.c_str(), cfg.wifiPassword.c_str());
}

void connectWifi() {
	statusled::setWifiState(statusled::WifiState::kConnecting);
	log::wifi("connecting to '%s'...", cfg.wifiSsid.c_str());
	beginWifi();

	const uint32_t start = millis();
	uint32_t attemptStart = start;
	while (WiFi.status() != WL_CONNECTED) {
		statusled::loop();
		// Still honoured while WiFi is down - the portal is how changed WiFi credentials get fixed.
		if (bootbutton::poll() == bootbutton::Event::kLongHold) {
			log::cfg("BOOT button held ~3s - entering provisioning portal");
			statusled::setProvisioning(true);
			config::runProvisioningPortal();  // never returns - reboots once submitted
		}
		delay(10);
		if (millis() - start > kWifiRestartMs) {
			log::wifi("no connection for %lus (status=%d) - restarting", (unsigned long)(kWifiRestartMs / 1000),
					  WiFi.status());
			delay(100);
			ESP.restart();
		}
		if (millis() - attemptStart > kWifiRetryMs) {
			log::wifi("not connected after %lus (status=%d) - starting a new attempt",
					  (unsigned long)(kWifiRetryMs / 1000), WiFi.status());
			WiFi.disconnect();
			beginWifi();
			attemptStart = millis();
		}
	}
	log::wifi("connected, IP=%s", WiFi.localIP().toString().c_str());
	statusled::setWifiState(statusled::WifiState::kConnected);
}

}  // namespace

void setup() {
	Serial.begin(115200);
	delay(200);  // let the USB-serial link settle before the first log line
	Serial.println();
	Serial.printf("[BOOT] %s starting\n", board::kDeviceModel);

	statusled::begin(board::kPinStatusLed);
	statusled::setWifiState(statusled::WifiState::kNotConfigured);

	cfg = config::load();
	if (!cfg.hasWifi()) {
		log::cfg("entering provisioning portal (no WiFi config yet)");
		statusled::setProvisioning(true);
		config::runProvisioningPortal();  // never returns - reboots once submitted
	}

	connectWifi();
	protocol::begin();
	remotelink::begin(cfg.wsUrl, cfg.channelName, cfg.bearerToken, cfg.tlsInsecure);
}

void loop() {
	statusled::loop();
	switch (bootbutton::poll()) {
		case bootbutton::Event::kLongHold:
			log::cfg("BOOT button held ~3s - entering provisioning portal");
			statusled::setProvisioning(true);
			config::runProvisioningPortal();  // never returns - reboots once submitted
			break;
		case bootbutton::Event::kShortPress:
			log::ble("BOOT button pressed - disconnecting all BLE peripherals");
			blecentral::disconnectAll();
			break;
		case bootbutton::Event::kNone:
			break;
	}
	if (WiFi.status() != WL_CONNECTED) {
		statusled::setWifiState(statusled::WifiState::kConnecting);
		connectWifi();
		return;
	}
	remotelink::loop();
	protocol::loop();
}
