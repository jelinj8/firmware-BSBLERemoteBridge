// BSBleRemoteBridge firmware entry point. See doc/PROTOCOL.md for the wire-protocol deltas this
// firmware adds on top of BSToolbox-BLE's ble-bridge NDJSON protocol, and CLAUDE.md for the
// project's phased build order and known risks.
//
// Bring-up status: Phase 0 (board/LED bring-up) and Phase 1 (WiFi + WebSocket link against the
// real, already-deployed server) - BLE central command handling (lib/BleCentral, lib/Protocol)
// lands in later phases; incoming lines are only logged for now, not acted on.
#include <Arduino.h>
#include <WiFi.h>

#include "BoardConfig.h"
#include "Config.h"
#include "Log.h"
#include "RemoteLink.h"
#include "SessionControl.h"
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

// Placeholder for the real BLE-command dispatch (lib/Protocol, not built yet) - handles only the
// session-control sentinel so a server-initiated session_reset gets a session_ready ack, and logs
// everything else as a live protocol analyzer per the original brief.
void handleIncomingLine(const String& line) {
	statusled::pulseBleActivity();
	std::string type = sessioncontrol::typeOf(line.c_str());
	if (type == sessioncontrol::kSessionReset) {
		log::ws("session_reset received - no BLE state to tear down yet (Protocol layer not built), "
				"acking session_ready");
		remotelink::sendText(sessioncontrol::line(sessioncontrol::kSessionReady).c_str());
		return;
	}
	log::ble("<- %s", line.c_str());
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
	if (!cfg.hasWifi() || config::bootButtonHeld()) {
		log::cfg("entering provisioning portal (%s)", cfg.hasWifi() ? "boot button held" : "no WiFi config yet");
		statusled::setProvisioning(true);
		config::runProvisioningPortal();  // never returns - reboots once submitted
	}

	connectWifi();
	remotelink::onText(handleIncomingLine);
	remotelink::begin(cfg.wsUrl, cfg.channelName, cfg.bearerToken, cfg.tlsInsecure);
}

void loop() {
	statusled::loop();
	if (WiFi.status() != WL_CONNECTED) {
		statusled::setWifiState(statusled::WifiState::kConnecting);
		connectWifi();
		return;
	}
	remotelink::loop();
}
