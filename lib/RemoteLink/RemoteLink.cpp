#include "RemoteLink.h"

#include <WebSocketsClient.h>

#include "Log.h"
#include "StatusLed.h"

namespace blebridge {
namespace remotelink {

namespace {

// ISRG Root X1 (Let's Encrypt) - a reasonable default for a real public host. Swap this for a
// deployment whose server uses a different CA; the tlsInsecure toggle is the documented escape
// hatch when validating a custom/self-signed CA isn't worth the trouble (see Config.h).
const char kDefaultCaCertPem[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw
TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh
cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4
WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu
ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY
MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc
h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+
0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U
A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW
T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH
B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC
B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv
KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn
OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn
jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw
qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI
rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV
HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq
hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL
ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ
3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK
NFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5
ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7Ur
TkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwdC
jNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZAJzVc
oyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq
4RgqsahDYVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPA
mRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57d
emyPxgcYxn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b21/+jh5Xos1AnX5iItreGCc=
-----END CERTIFICATE-----
)EOF";

constexpr uint32_t kInitialBackoffMs = 1000;
constexpr uint32_t kMaxBackoffMs = 30000;
constexpr uint32_t kHeartbeatIntervalMs = 25000;
constexpr uint32_t kHeartbeatPongTimeoutMs = 10000;
constexpr uint8_t kHeartbeatDisconnectAfterMissed = 2;

WebSocketsClient ws_;
TextHandler textHandler_;
bool connected_ = false;
bool authRejected_ = false;
// Accumulates a fragmented incoming TEXT message across WStype_FRAGMENT_TEXT_START/FRAGMENT/
// FRAGMENT_FIN - see handleEvent's own comment on why this exists at all.
String fragmentBuffer_;
uint32_t backoffMs_ = kInitialBackoffMs;

struct ParsedUrl {
	bool tls = false;
	String host;
	uint16_t port = 0;
	String path = "/";
};

// Parses "ws://host[:port]/path" / "wss://host[:port]/path" - just enough for what this firmware
// needs (no userinfo/query/fragment support), since WebSocketsClient's begin()/beginSSL() take
// host/port/path as separate arguments rather than a full URL.
ParsedUrl parseUrl(const String& url) {
	ParsedUrl out;
	String rest = url;
	if (rest.startsWith("wss://")) {
		out.tls = true;
		out.port = 443;
		rest = rest.substring(6);
	} else if (rest.startsWith("ws://")) {
		out.tls = false;
		out.port = 80;
		rest = rest.substring(5);
	}

	int pathStart = rest.indexOf('/');
	String hostPort = pathStart >= 0 ? rest.substring(0, pathStart) : rest;
	out.path = pathStart >= 0 ? rest.substring(pathStart) : "/";

	int colon = hostPort.indexOf(':');
	if (colon >= 0) {
		out.host = hostPort.substring(0, colon);
		out.port = static_cast<uint16_t>(hostPort.substring(colon + 1).toInt());
	} else {
		out.host = hostPort;
	}
	return out;
}

void handleEvent(WStype_t type, uint8_t* payload, size_t length) {
	switch (type) {
		case WStype_CONNECTED:
			connected_ = true;
			backoffMs_ = kInitialBackoffMs;
			statusled::setLinkState(statusled::LinkState::kConnected);
			log::ws("connected");
			break;

		case WStype_DISCONNECTED: {
			connected_ = false;
			// payload is NULL for a plain/graceful close (WebSocketsClient::clientDisconnect's
			// common case - e.g. the peer just closing the TCP connection) and only non-null for
			// a handshake-rejection reason like "HTTP 401" - constructing a String from a NULL
			// pointer here leaves its internal buffer NULL too (Arduino-ESP32's String::init()
			// never allocates one), and String::c_str() on THAT then hands printf's %s a NULL
			// pointer - newlib's strlen doesn't special-case NULL, so this previously took the
			// whole firmware down with a Guru Meditation Error on every ordinary disconnect.
			String reason = (payload != nullptr && length > 0) ? String(reinterpret_cast<char*>(payload), length) : String();
			if (reason == "HTTP 401") {
				authRejected_ = true;
				statusled::setLinkState(statusled::LinkState::kAuthRejected);
				log::ws("rejected: access denied (HTTP 401) - check the bearer token; not retrying, this "
						"cannot succeed without reconfiguring");
			} else {
				statusled::setLinkState(statusled::LinkState::kConnecting);
				log::ws("disconnected (%s), retrying in %ums", reason.length() > 0 ? reason.c_str() : "connection closed",
						backoffMs_);
				backoffMs_ = min(backoffMs_ * 2, kMaxBackoffMs);
				ws_.setReconnectInterval(backoffMs_);
			}
			break;
		}

		case WStype_TEXT:
			if (textHandler_) {
				// Defensive: same NULL-payload hazard as WStype_DISCONNECTED above, even though a
				// genuine received text frame isn't known to ever hand back a NULL payload.
				String text = (payload != nullptr && length > 0) ? String(reinterpret_cast<char*>(payload), length) : String();
				textHandler_(text);
			}
			break;

		// This library does NOT reassemble a fragmented TEXT message into one final WStype_TEXT
		// event - each fragment arrives as its own event, carrying only that fragment's own
		// payload (see WebSocketsClient::messageReceived), and the caller is responsible for
		// concatenating them. A message large enough to cross the sending WebSocket client's own
		// fragmentation threshold (e.g. a write_stream command carrying a long raster payload)
		// would otherwise arrive here in pieces with no case to handle them, and be silently lost.
		case WStype_FRAGMENT_TEXT_START:
			fragmentBuffer_ = "";
			if (payload != nullptr && length > 0) {
				fragmentBuffer_ += String(reinterpret_cast<char*>(payload), length);
			}
			break;

		case WStype_FRAGMENT:
			if (payload != nullptr && length > 0) {
				fragmentBuffer_ += String(reinterpret_cast<char*>(payload), length);
			}
			break;

		case WStype_FRAGMENT_FIN:
			if (payload != nullptr && length > 0) {
				fragmentBuffer_ += String(reinterpret_cast<char*>(payload), length);
			}
			if (textHandler_) {
				textHandler_(fragmentBuffer_);
			}
			fragmentBuffer_ = "";
			break;

		case WStype_PING:
			log::ws("ping received");
			break;
		case WStype_PONG:
			log::ws("pong received");
			break;

		case WStype_ERROR:
			log::ws("error event (length=%u)", static_cast<unsigned>(length));
			break;

		default:
			break;
	}
}

}  // namespace

void begin(const String& wsUrl, const String& channelName, const String& bearerToken, bool tlsInsecure) {
	connected_ = false;
	authRejected_ = false;
	backoffMs_ = kInitialBackoffMs;

	ParsedUrl parsed = parseUrl(wsUrl);
	log::ws("connecting to %s://%s:%u%s as channel '%s' (tls=%s, insecure=%s)", parsed.tls ? "wss" : "ws",
			parsed.host.c_str(), parsed.port, parsed.path.c_str(), channelName.c_str(), parsed.tls ? "yes" : "no",
			tlsInsecure ? "yes" : "no");

	String headers = "X-BLE-Remote-Name: " + channelName;
	if (bearerToken.length() > 0) {
		headers += "\r\nAuthorization: Bearer " + bearerToken;
	}
	ws_.setExtraHeaders(headers.c_str());

	if (parsed.tls) {
		if (tlsInsecure) {
			// No fingerprint/CA/bundle argument at all - relies on whichever default the active
			// beginSSL() overload uses (its const-char*-vs-const-uint8_t* fingerprint parameter
			// type differs by SSL backend), which in both cases means "no pinned fingerprint, no
			// CA material" and therefore falls through to WebSocketsClient's own setInsecure().
			ws_.beginSSL(parsed.host.c_str(), parsed.port, parsed.path.c_str());
		} else {
			ws_.beginSslWithCA(parsed.host.c_str(), parsed.port, parsed.path.c_str(), kDefaultCaCertPem);
		}
	} else {
		ws_.begin(parsed.host.c_str(), parsed.port, parsed.path.c_str());
	}

	ws_.onEvent(handleEvent);
	ws_.enableHeartbeat(kHeartbeatIntervalMs, kHeartbeatPongTimeoutMs, kHeartbeatDisconnectAfterMissed);
	ws_.setReconnectInterval(backoffMs_);

	statusled::setLinkState(statusled::LinkState::kConnecting);
}

void onText(TextHandler handler) {
	textHandler_ = handler;
}

void sendText(const String& text) {
	if (!connected_) {
		log::ws("dropped outgoing line, not connected: %s", text.c_str());
		return;
	}
	ws_.sendTXT(text.c_str());
}

void loop() {
	if (authRejected_) {
		return;
	}
	ws_.loop();
}

bool isConnected() {
	return connected_;
}

bool isAuthRejected() {
	return authRejected_;
}

}  // namespace remotelink
}  // namespace blebridge
