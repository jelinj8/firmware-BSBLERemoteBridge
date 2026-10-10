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

// Logging a full hex payload on every command/response/event line costs real, blocking Serial
// time at 115200 baud, scaling directly with job length - Log.h's own doc comment describes an
// INFO/VERBOSE split for exactly this reason. This compacts the payload to a byte count at the
// default INFO level (VERBOSE still gets the full raw line, for protocol-mismatch debugging) -
// the full line is always still what's actually sent over the wire, only the *logged* copy is
// shortened.
String compactForLog(const String& line) {
	if (log::currentLevel >= log::Level::kVerbose) {
		return line;
	}
	int key = line.indexOf("\"value_hex\":\"");
	if (key < 0) {
		return line;
	}
	int valueStart = key + 13;  // length of "\"value_hex\":\""
	int valueEnd = line.indexOf('"', valueStart);
	if (valueEnd < 0) {
		return line;
	}
	String placeholder = "<" + String((valueEnd - valueStart) / 2) + " bytes>";
	return line.substring(0, valueStart) + placeholder + line.substring(valueEnd);
}

void sendAndLogOut(const String& line) {
	statusled::pulseBleActivity();
	log::ble("-> %s", compactForLog(line).c_str());
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
		sendAndLogOut(String(sessioncontrol::line(sessioncontrol::kSessionReady).c_str()));
		return;
	}

	log::ble("<- %s", compactForLog(line).c_str());

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

void begin(const String& deviceName) {
	blecentral::begin(deviceName);
	remotelink::onText(handleIncomingLine);
}

void loop() {
	blecentral::pollEvents(sendAndLogOut);
}

}  // namespace protocol
}  // namespace blebridge
