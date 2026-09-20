// Pure human-readable serial debug/analyzer logging - deliberately NOT a binary transport (this
// project's serial port is only ever a human interface, unlike the sibling firmware-BSHMIEinkDevice
// repo, which shares its one UART with a binary command protocol). Every WiFi/WebSocket connection
// event and every BLE command/response/event gets a line here, so the serial console doubles as a
// live protocol analyzer during development.
//
// Deliberately minimal (thin Serial.printf wrappers, not a generic logging framework) - firmware-
// only, not part of the `native` test env.
#pragma once

#include <Arduino.h>

namespace blebridge {
namespace log {

enum class Level : uint8_t { kError = 0, kInfo = 1, kVerbose = 2 };

// Default INFO: every command/event gets one line; VERBOSE additionally dumps raw JSON payloads
// (useful when debugging a protocol mismatch against the Java side); ERROR is failures only.
inline Level currentLevel = Level::kInfo;

inline void wifi(const char* fmt, ...) {
	Serial.printf("[%10lu][WIFI] ", millis());
	va_list args;
	va_start(args, fmt);
	Serial.vprintf(fmt, args);
	va_end(args);
	Serial.println();
}

inline void ws(const char* fmt, ...) {
	Serial.printf("[%10lu][WS] ", millis());
	va_list args;
	va_start(args, fmt);
	Serial.vprintf(fmt, args);
	va_end(args);
	Serial.println();
}

inline void ble(const char* fmt, ...) {
	Serial.printf("[%10lu][BLE] ", millis());
	va_list args;
	va_start(args, fmt);
	Serial.vprintf(fmt, args);
	va_end(args);
	Serial.println();
}

inline void cfg(const char* fmt, ...) {
	Serial.printf("[%10lu][CFG] ", millis());
	va_list args;
	va_start(args, fmt);
	Serial.vprintf(fmt, args);
	va_end(args);
	Serial.println();
}

}  // namespace log
}  // namespace blebridge
