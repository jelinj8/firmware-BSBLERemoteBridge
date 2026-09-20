// Non-blocking status-LED state machine for a single onboard WS2812-style addressable RGB pixel
// (driven via Arduino-ESP32's built-in neopixelWrite() helper - no NeoPixel/FastLED library needed
// for one pixel). Since there's only one LED and two independent things need indicating (WiFi/
// WebSocket link state, and momentary BLE traffic activity), color separates the two axes: a
// steady base color+blink cadence for connection state, with a brief blue flash overlaid on top
// for every BLE command/response/event - see doc/PROTOCOL.md's LED table for the full state chart.
//
// Call begin() once at boot, the setXState()/setProvisioning() setters whenever the corresponding
// subsystem's state changes, pulseBleActivity() on every BLE command/response/event, and loop()
// every main-loop iteration (drives timing off millis(), no delay() calls).
#pragma once

#include <Arduino.h>

namespace blebridge {
namespace statusled {

enum class WifiState { kNotConfigured, kConnecting, kConnected };
enum class LinkState { kIdle, kConnecting, kConnected, kAuthRejected };

void begin(int pin);
void setProvisioning(bool active);
void setWifiState(WifiState state);
void setLinkState(LinkState state);

// Triggers a brief (~40ms) blue flash over whatever base state is currently showing - call this
// on every scan result, connect/disconnect, and GATT read/write/notify. Always wins over the base
// state for its short duration, including over a solid auth-rejected red - a 40ms interruption
// doesn't meaningfully hide a steady/slow-blink base state, and BLE-activity visibility during
// development is the more time-sensitive signal.
void pulseBleActivity();

void loop();

}  // namespace statusled
}  // namespace blebridge
