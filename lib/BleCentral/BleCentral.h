// BLE central-role driver, translating ble-bridge's JSON command vocabulary (see
// BSToolbox-BLE/ble-bridge/src/main.rs - the canonical source of truth for every field name and
// response/event shape below) onto NimBLE-Arduino's central APIs (NimBLEScan/NimBLEClient/
// NimBLERemoteService/NimBLERemoteCharacteristic).
//
// Commands are handled synchronously (handleCommand blocks on the underlying NimBLE calls, called
// from the same main-loop context that owns the WebSocket link) EXCEPT `scan` and `pair`, whose
// real response only becomes known later (scan: when the timeout elapses; pair: when the bonding
// handshake completes) - both return an empty string from handleCommand and instead queue their
// eventual response the same way unsolicited events (device_found/connected/disconnected/
// notification) are queued, since NimBLE's own callbacks for these fire from its own FreeRTOS host
// task, not this firmware's main loop. pollEvents() drains that queue - call it every main-loop
// iteration alongside handleCommand.
#pragma once

#include <Arduino.h>

#include <ArduinoJson.h>

#include <functional>

namespace blebridge {
namespace blecentral {

using EventEmitter = std::function<void(const String&)>;

// Initializes the NimBLE stack in central role and configures pairing (bonding + MITM + secure
// connections, keyboard-only IO capability so a `pair` command's PIN is supplied programmatically
// via NimBLEDevice::injectPassKey rather than surfacing any prompt). Call once at boot.
void begin();

// Drains events queued from NimBLE's own host-task callbacks (device_found, connected,
// disconnected, notification, plus the deferred scan/pair responses) and calls `emit` once per
// queued line. Call every main-loop iteration.
void pollEvents(EventEmitter emit);

// Tears down all BLE central state (stops any scan, disconnects + deletes every client, clears the
// known-address cache and any pending pair) - mirrors what a server-requested session_reset means
// for the local ble-bridge sidecar (a fresh process), just as an in-place state reset here instead.
void resetSession();

// Dispatches one already-parsed command. `cmd` is the command name (e.g. "scan", "connect"),
// `id` its correlation id, `params` its other fields. Returns the complete serialized JSON
// response line ready to send as-is, or an empty string if the response will come later via
// pollEvents (scan, pair only).
String handleCommand(const String& cmd, const String& id, JsonObjectConst params);

}  // namespace blecentral
}  // namespace blebridge
