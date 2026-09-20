// Matches the hex encoding used on the wire by BSToolbox-BLE's ble-bridge sidecar and its
// HexCodec.java: lowercase, no separators. Deliberately byte-for-byte behavior-compatible with
// HexCodec.java, including its leniency on malformed input (see decode() below) - this runs on a
// NimBLE central holding a live WebSocket connection, where crashing the whole session over one
// bad `value_hex` field is a much worse outcome than the JVM sidecar's own "just get a garbage
// byte" behavior. Pure C++17, no Arduino/ESP32 dependency - built and tested by the `native`
// PlatformIO env (`pio test -e native`), same as HexCodec.java is exercised on the JVM side.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace blebridge {
namespace hexcodec {

inline std::string encode(const uint8_t* data, size_t len) {
	static const char kHex[] = "0123456789abcdef";
	std::string out(len * 2, '0');
	for (size_t i = 0; i < len; i++) {
		uint8_t v = data[i];
		out[i * 2] = kHex[v >> 4];
		out[i * 2 + 1] = kHex[v & 0x0F];
	}
	return out;
}

inline std::string encode(const std::vector<uint8_t>& bytes) {
	return encode(bytes.data(), bytes.size());
}

// Returns -1 for a non-hex character, matching Character.digit(c, 16)'s behavior for HexCodec.java
// callers to rely on the same "silently produce a garbage byte, never throw" contract on odd/
// invalid input.
inline int digit(char c) {
	if (c >= '0' && c <= '9') {
		return c - '0';
	}
	if (c >= 'a' && c <= 'f') {
		return c - 'a' + 10;
	}
	if (c >= 'A' && c <= 'F') {
		return c - 'A' + 10;
	}
	return -1;
}

// A trailing odd nibble is silently dropped (floor(len/2) output bytes), and an invalid hex
// character contributes -1 into the shift/OR math below rather than throwing - both exactly
// mirroring HexCodec.java's decode(), so a malformed value_hex from a peer degrades to a garbage
// byte instead of taking down the whole BLE session.
inline std::vector<uint8_t> decode(const std::string& hex) {
	if (hex.empty()) {
		return {};
	}
	std::vector<uint8_t> out(hex.size() / 2);
	for (size_t i = 0; i < out.size(); i++) {
		int hi = digit(hex[i * 2]);
		int lo = digit(hex[i * 2 + 1]);
		out[i] = static_cast<uint8_t>((hi << 4) | lo);
	}
	return out;
}

}  // namespace hexcodec
}  // namespace blebridge
