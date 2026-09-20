#include "Protocol.h"

#include <ArduinoJson.h>

#include "BleCentral.h"
#include "Log.h"
#include "RemoteLink.h"
#include "SessionControl.h"
#include "StatusLed.h"

namespace blebridge {
namespace protocol {

namespace {

void sendAndLogOut(const String& line) {
	statusled::pulseBleActivity();
	log::ble("-> %s", line.c_str());
	remotelink::sendText(line);
}

void handleIncomingLine(const String& line) {
	if (line.length() == 0) {
		return;
	}
	statusled::pulseBleActivity();

	std::string controlType = sessioncontrol::typeOf(line.c_str());
	if (controlType == sessioncontrol::kSessionReset) {
		log::ws("session_reset received - resetting all BLE central state");
		blecentral::resetSession();
		remotelink::sendText(String(sessioncontrol::line(sessioncontrol::kSessionReady).c_str()));
		return;
	}

	log::ble("<- %s", line.c_str());

	JsonDocument doc;
	DeserializationError parseError = deserializeJson(doc, line);
	if (parseError != DeserializationError::Ok || !doc["cmd"].is<const char*>()) {
		JsonDocument err;
		err["type"] = "error";
		err["message"] = "bad command: " + String(parseError.c_str());
		String out;
		serializeJson(err, out);
		sendAndLogOut(out);
		return;
	}

	String cmd = doc["cmd"].as<String>();
	String id = doc["id"] | "";
	String response = blecentral::handleCommand(cmd, id, doc.as<JsonObjectConst>());
	if (response.length() > 0) {
		sendAndLogOut(response);
	}
}

}  // namespace

void begin() {
	blecentral::begin();
	remotelink::onText(handleIncomingLine);
}

void loop() {
	blecentral::pollEvents(sendAndLogOut);
}

}  // namespace protocol
}  // namespace blebridge
