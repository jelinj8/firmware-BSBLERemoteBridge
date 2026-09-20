// Mirrors BSToolbox-BLE's SessionControl.java: a single physical WebSocket connection carries both
// real ble-bridge NDJSON lines and session-control actions (currently just "discard all BLE
// central state and start fresh", see RemoteAdapterRegistry.requestNewSession on the Java side).
// Control lines use a reserved field no ble-bridge command/response/event shape uses, so both ends
// can tell them apart from real protocol lines on the same channel - this must be checked BEFORE
// any `cmd`/`type` dispatch (see lib/Protocol).
//
// Pure C++17 + ArduinoJson (no Arduino/ESP32/NimBLE dependency) - built and tested by the `native`
// PlatformIO env.
#pragma once

#include <ArduinoJson.h>

#include <string>

namespace blebridge {
namespace sessioncontrol {

constexpr const char* kControlField = "__ble_remote_control__";
constexpr const char* kSessionReset = "session_reset";  // server -> client only
constexpr const char* kSessionReady = "session_ready";  // client -> server only

// Builds a complete control line, e.g. line(kSessionReady) -> {"__ble_remote_control__":"session_ready"}
inline std::string line(const char* type) {
	JsonDocument doc;
	doc[kControlField] = type;
	std::string out;
	serializeJson(doc, out);
	return out;
}

// Returns the control type of `text` (kSessionReset/kSessionReady), or an empty string if it isn't
// a control line (including if it's not valid JSON at all) - mirrors SessionControl.java's typeOf,
// which likewise treats a parse failure as "not a control line" rather than propagating the error.
inline std::string typeOf(const std::string& text) {
	JsonDocument doc;
	if (deserializeJson(doc, text) != DeserializationError::Ok) {
		return "";
	}
	JsonVariant control = doc[kControlField];
	if (control.isNull() || !control.is<const char*>()) {
		return "";
	}
	return std::string(control.as<const char*>());
}

}  // namespace sessioncontrol
}  // namespace blebridge
