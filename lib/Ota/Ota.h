// Over-the-air firmware updates via ArduinoOTA (PlatformIO's `espota` upload protocol - see the
// `nanoesp32-c6-ota` env in platformio.ini). The board's default_16MB partition table already has
// two 6.4MB app slots (ota_0/ota_1), so an update is written to the inactive slot and the board
// reboots into it; a failed or interrupted upload leaves the running firmware untouched.
//
// Advertised (mDNS) under the device name set in the provisioning portal, or - when none is set -
// the default one, board::kNamePrefix + the last 6 hex digits of its WiFi MAC (e.g.
// `ble-bridge-3851DC`). The OTA password is always those 6 MAC characters, renamed or not: that
// keeps an upload from landing on the wrong bridge, and for an unrenamed bridge it can be read
// straight off the name. It is not a secret - the MAC is visible to anyone on the same WiFi.
#pragma once

#include <Arduino.h>

namespace blebridge {
namespace ota {

// Starts the OTA listener (UDP 3232, mDNS name `deviceName` with anything but letters, digits and
// '-' replaced by '-'). Call once WiFi is connected.
void begin(const String& deviceName);

// Call every main-loop iteration while WiFi is up.
void loop();

}  // namespace ota
}  // namespace blebridge
