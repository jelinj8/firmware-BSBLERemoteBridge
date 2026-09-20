// Native (no-ESP32-toolchain) unit tests for the transport-independent protocol logic. Run with:
//   pio test -e native
//
// Mirrors BSToolbox-BLE's HexCodecTest so both implementations are checked against the same
// expectations, and exercises SessionControl's line-detection/building against the exact sentinel
// shape SessionControl.java/ControlAwarePipe.java use on the Java side.
#include <unity.h>

#include <string>

#include "HexCodec.h"
#include "SessionControl.h"

using namespace blebridge;

// ---- HexCodec -------------------------------------------------------------------------------

void test_hex_encode_lowercase_no_separators(void) {
	const uint8_t data[] = {0x00, 0xAB, 0xFF, 0x01};
	TEST_ASSERT_EQUAL_STRING("00abff01", hexcodec::encode(data, sizeof(data)).c_str());
}

void test_hex_encode_empty(void) {
	TEST_ASSERT_EQUAL_STRING("", hexcodec::encode(nullptr, 0).c_str());
}

void test_hex_decode_round_trip(void) {
	auto bytes = hexcodec::decode("00abff01");
	const uint8_t expected[] = {0x00, 0xAB, 0xFF, 0x01};
	TEST_ASSERT_EQUAL(sizeof(expected), bytes.size());
	TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, bytes.data(), bytes.size());
}

void test_hex_decode_empty_is_empty(void) {
	TEST_ASSERT_EQUAL(0, static_cast<int>(hexcodec::decode("").size()));
}

void test_hex_decode_odd_length_drops_trailing_nibble(void) {
	// "abc" -> floor(3/2) = 1 output byte, matching HexCodec.java's decode() exactly.
	auto bytes = hexcodec::decode("abc");
	TEST_ASSERT_EQUAL(1, static_cast<int>(bytes.size()));
	TEST_ASSERT_EQUAL_HEX8(0xAB, bytes[0]);
}

void test_hex_decode_invalid_char_is_lenient_not_a_crash(void) {
	// Must not throw/crash - a malformed value_hex from a peer degrades to a garbage byte instead
	// of taking down the whole BLE session (see HexCodec.h's doc comment).
	auto bytes = hexcodec::decode("zz");
	TEST_ASSERT_EQUAL(1, static_cast<int>(bytes.size()));
}

// ---- SessionControl ---------------------------------------------------------------------------

void test_session_control_line_shape(void) {
	std::string line = sessioncontrol::line(sessioncontrol::kSessionReady);
	TEST_ASSERT_EQUAL_STRING("{\"__ble_remote_control__\":\"session_ready\"}", line.c_str());
}

void test_session_control_type_of_detects_reset(void) {
	std::string type = sessioncontrol::typeOf("{\"__ble_remote_control__\":\"session_reset\"}");
	TEST_ASSERT_EQUAL_STRING(sessioncontrol::kSessionReset, type.c_str());
}

void test_session_control_type_of_ignores_real_protocol_lines(void) {
	// A real ble-bridge command/response/event line never uses this field - must not be
	// misdetected as a control line.
	std::string type = sessioncontrol::typeOf("{\"cmd\":\"scan\",\"id\":\"1\"}");
	TEST_ASSERT_TRUE(type.empty());
}

void test_session_control_type_of_ignores_malformed_json(void) {
	std::string type = sessioncontrol::typeOf("not json at all");
	TEST_ASSERT_TRUE(type.empty());
}

int main(int argc, char** argv) {
	UNITY_BEGIN();
	RUN_TEST(test_hex_encode_lowercase_no_separators);
	RUN_TEST(test_hex_encode_empty);
	RUN_TEST(test_hex_decode_round_trip);
	RUN_TEST(test_hex_decode_empty_is_empty);
	RUN_TEST(test_hex_decode_odd_length_drops_trailing_nibble);
	RUN_TEST(test_hex_decode_invalid_char_is_lenient_not_a_crash);
	RUN_TEST(test_session_control_line_shape);
	RUN_TEST(test_session_control_type_of_detects_reset);
	RUN_TEST(test_session_control_type_of_ignores_real_protocol_lines);
	RUN_TEST(test_session_control_type_of_ignores_malformed_json);
	return UNITY_END();
}
