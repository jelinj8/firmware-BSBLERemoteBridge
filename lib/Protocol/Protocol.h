// Top-level wire-protocol glue: wires RemoteLink's incoming-line handler to SessionControl (which
// must be checked first) and BleCentral (which handles everything else), and drains BleCentral's
// queued events/deferred-responses back out over RemoteLink every main-loop tick. See
// lib/BleCentral/BleCentral.h for the actual command translation and lib/SessionControl for the
// session_reset/session_ready sentinel this layer special-cases.
#pragma once

namespace blebridge {
namespace protocol {

// Initializes BleCentral and registers this module's incoming-line handler with RemoteLink. Call
// once, after remotelink::begin().
void begin();

// Drains BleCentral's queued events/deferred responses and sends each over RemoteLink. Call every
// main-loop iteration.
void loop();

}  // namespace protocol
}  // namespace blebridge
