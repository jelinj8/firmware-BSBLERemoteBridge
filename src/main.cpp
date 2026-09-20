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

void connectWifi() {
	statusled::setWifiState(statusled::WifiState::kConnecting);
	log::wifi("connecting to '%s'...", cfg.wifiSsid.c_str());
	WiFi.mode(WIFI_STA);
	WiFi.begin(cfg.wifiSsid.c_str(), cfg.wifiPassword.c_str());

	uint32_t start = millis();
	while (WiFi.status() != WL_CONNECTED) {
		statusled::loop();
		delay(10);
		if (millis() - start > 20000) {
			log::wifi("still connecting after 20s (status=%d)...", WiFi.status());
			start = millis();
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
