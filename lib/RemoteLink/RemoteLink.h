// WebSocket client mirroring BSToolbox-BLE's RemoteAdapterClient.java: dials out to a
// remote-BLE-adapter server (e.g. wss://storage.bliksoft.cz/ble-remote), authenticating via the
// same two handshake headers (X-BLE-Remote-Name, Authorization: Bearer <token>) so the server's
// existing RemoteAdapterRegistry needs zero changes to accept this firmware as a peer. See
// RemoteAdapterClient.java for the exact behavior this reproduces: 15s connect timeout, a 25s
// ping heartbeat, exponential backoff (1s doubling to a 30s cap, reset on success), and a
// terminal (non-retrying) "auth rejected" state specifically on an HTTP 401 handshake response -
// any other rejection or I/O failure keeps retrying with backoff.
//
// One full JSON object is sent/received per WebSocket TEXT frame, matching ble-bridge's NDJSON
// protocol carried this way instead of over stdin/stdout lines - see lib/Protocol and
// lib/SessionControl for what rides on top of this transport.
#pragma once

#include <Arduino.h>

#include <functional>

namespace blebridge {
namespace remotelink {

using TextHandler = std::function<void(const String&)>;

// wsUrl is a full "ws://host[:port]/path" or "wss://host[:port]/path" URL. channelName is sent as
// X-BLE-Remote-Name; bearerToken (may be empty) is sent as "Authorization: Bearer <token>".
// tlsInsecure true skips certificate verification entirely (WebSocketsClient::beginSSL with no CA
// material - see RemoteLink.cpp); false verifies against a compiled-in root CA (currently ISRG
// Root X1 / Let's Encrypt - swap kDefaultCaCertPem in RemoteLink.cpp if a deployment's server uses
// a different CA).
void begin(const String& wsUrl, const String& channelName, const String& bearerToken, bool tlsInsecure);

// Replaces any previously registered handler. Called once per complete incoming ble-bridge NDJSON
// line/session-control line (framing already stripped) - never called for the library's own
// connection-lifecycle events, those only drive the LED/log state internally.
void onText(TextHandler handler);

// Sends one complete line as a single WebSocket TEXT frame. No-op (silently dropped, logged) if
// not currently connected - matching "there is nowhere to send this" rather than queuing.
void sendText(const String& text);

// Drives the connection state machine (handshake, heartbeat, backoff/reconnect) - call every
// main-loop iteration. Does nothing once isAuthRejected() is true (see begin()'s doc comment).
void loop();

bool isConnected();

// True once the server has rejected our handshake with HTTP 401 - terminal per
// RemoteAdapterClient.java's contract: the name/token are fixed at begin() time, so retrying the
// same connection could never succeed. Only cleared by calling begin() again (e.g. after the user
// reconfigures the token via the captive portal and reboots).
bool isAuthRejected();

}  // namespace remotelink
}  // namespace blebridge
