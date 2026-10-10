#include "Config.h"

#include <DNSServer.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_mac.h>

#include "BoardConfig.h"
#include "Log.h"
#include "StatusLed.h"

#if __has_include("secrets.h")
#include "secrets.h"
#define BLEBRIDGE_HAS_SECRETS 1
#else
#define BLEBRIDGE_HAS_SECRETS 0
#endif

namespace blebridge {
namespace config {

namespace {
constexpr const char* kNamespace = "blebridge";
constexpr int kDnsPort = 53;
}  // namespace

String macSuffix() {
	uint8_t mac[6];
	esp_read_mac(mac, ESP_MAC_WIFI_STA);
	char buf[7];
	snprintf(buf, sizeof(buf), "%02X%02X%02X", mac[3], mac[4], mac[5]);
	return String(buf);
}

String defaultDeviceName() {
	return String(board::kNamePrefix) + macSuffix();
}

String hostName(const String& deviceName) {
	String host;
	for (size_t i = 0; i < deviceName.length(); i++) {
		char c = deviceName[i];
		host += isalnum(static_cast<unsigned char>(c)) ? c : '-';
	}
	return host;
}

String Values::effectiveDeviceName() const {
	return deviceName.length() > 0 ? deviceName : defaultDeviceName();
}

Values load() {
	Values v;
	Preferences prefs;
	prefs.begin(kNamespace, true);
	v.wifiSsid = prefs.getString("wifi_ssid", "");
	v.wifiPassword = prefs.getString("wifi_pass", "");
	v.wsUrl = prefs.getString("ws_url", "");
	v.channelName = prefs.getString("channel_name", "");
	v.bearerToken = prefs.getString("bearer_token", "");
	bool hasTlsKey = prefs.isKey("tls_insecure");
	v.tlsInsecure = prefs.getBool("tls_insecure", false);
	v.deviceName = prefs.getString("device_name", "");
	prefs.end();

#if BLEBRIDGE_HAS_SECRETS
	// Compile-time fallback for any field never persisted yet - see secrets.h.example. Checked
	// independently per field (not all-or-nothing) so a partially-provisioned device still gets
	// sane defaults for whatever hasn't been set via the portal yet.
	if (v.wifiSsid.length() == 0) {
		v.wifiSsid = kWifiSsid;
	}
	if (v.wifiPassword.length() == 0) {
		v.wifiPassword = kWifiPassword;
	}
	if (v.wsUrl.length() == 0) {
		v.wsUrl = kWsUrl;
	}
	if (v.channelName.length() == 0) {
		v.channelName = kChannelName;
	}
	if (v.bearerToken.length() == 0) {
		v.bearerToken = kBearerToken;
	}
	if (!hasTlsKey) {
		v.tlsInsecure = kTlsInsecure;
	}
#endif
	return v;
}

void save(const Values& v) {
	Preferences prefs;
	prefs.begin(kNamespace, false);
	prefs.putString("wifi_ssid", v.wifiSsid);
	prefs.putString("wifi_pass", v.wifiPassword);
	prefs.putString("ws_url", v.wsUrl);
	prefs.putString("channel_name", v.channelName);
	prefs.putString("bearer_token", v.bearerToken);
	prefs.putBool("tls_insecure", v.tlsInsecure);
	prefs.putString("device_name", v.deviceName);
	prefs.end();
}

namespace {

WebServer* server_ = nullptr;
bool submitted_ = false;
Values pending_;

String htmlEscape(const String& in) {
	String out = in;
	out.replace("&", "&amp;");
	out.replace("\"", "&quot;");
	out.replace("<", "&lt;");
	out.replace(">", "&gt;");
	return out;
}

void handleRoot() {
	Values current = load();
	String page =
			"<!DOCTYPE html><html><head><meta name=viewport content='width=device-width,initial-scale=1'>"
			"<title>BLE Bridge setup</title></head><body>"
			"<h1>BLE Bridge setup</h1><form method=POST action='/save'>"
			"<label>Device name (blank = default)<br><input name=device_name value='" +
			htmlEscape(current.deviceName) + "' placeholder='" + htmlEscape(defaultDeviceName()) +
			"'></label><br>"
			"<label>WiFi SSID<br><input name=wifi_ssid value='" +
			htmlEscape(current.wifiSsid) +
			"'></label><br>"
			"<label>WiFi password (leave blank to keep current)<br><input name=wifi_pass type=password></label><br>"
			"<label>WebSocket URL<br><input name=ws_url value='" +
			htmlEscape(current.wsUrl) +
			"' size=48></label><br>"
			"<label>Channel name<br><input name=channel_name value='" +
			htmlEscape(current.channelName) +
			"'></label><br>"
			"<label>Bearer token (leave blank to keep current)<br><input name=bearer_token type=password></label><br>"
			"<label><input type=checkbox name=tls_insecure value=1" +
			String(current.tlsInsecure ? " checked" : "") +
			"> Skip TLS certificate verification (local network only)</label><br><br>"
			"<button type=submit>Save &amp; reboot</button></form></body></html>";
	server_->send(200, "text/html", page);
}

// Every OS's captive-portal detector probes a known connectivity-check URL on its own real
// domain (Windows: www.msftconnecttest.com; Android: connectivitycheck.gstatic.com; Apple:
// captive.apple.com) expecting a specific fixed response - our DNS hijack (dns.start(..., "*",
// apIp) below) sends all of those here instead of the real internet. Serving the portal page's
// actual content directly for those paths (as onNotFound used to) still shows correctly, but the
// OS's embedded captive-portal browser keeps the original probe URL in its address bar - since
// that's a real Microsoft/Google/Apple domain rather than this device's own IP, that's a
// legitimate point of confusion, and doesn't match Windows's own documented NCSI flow, which
// expects a redirect. Redirecting every non-root request to "/" makes the embedded browser
// actually navigate there, landing on this device's own IP in the address bar, matching that.
void redirectToRoot() {
	IPAddress apIp = WiFi.softAPIP();
	server_->sendHeader("Location", "http://" + apIp.toString() + "/", true);
	server_->send(302, "text/plain", "");
}

void handleSave() {
	Values existing = load();
	pending_.wifiSsid = server_->arg("wifi_ssid");
	String pass = server_->arg("wifi_pass");
	pending_.wifiPassword = pass.length() > 0 ? pass : existing.wifiPassword;
	pending_.wsUrl = server_->arg("ws_url");
	pending_.channelName = server_->arg("channel_name");
	String token = server_->arg("bearer_token");
	pending_.bearerToken = token.length() > 0 ? token : existing.bearerToken;
	pending_.tlsInsecure = server_->hasArg("tls_insecure");
	pending_.deviceName = server_->arg("device_name");
	pending_.deviceName.trim();
	server_->send(200, "text/html", "<html><body><h1>Saved. Rebooting...</h1></body></html>");
	submitted_ = true;
}

}  // namespace

void runProvisioningPortal() {
	const char* apSsid = "BLE-Bridge-Setup";
	WiFi.mode(WIFI_AP);
	WiFi.softAP(apSsid);
	IPAddress apIp = WiFi.softAPIP();
	log::cfg("provisioning portal up: connect to WiFi '%s' and open http://%s/", apSsid, apIp.toString().c_str());

	DNSServer dns;
	dns.start(kDnsPort, "*", apIp);  // answers every hostname with our own IP, for captive-portal auto-popup

	WebServer server(80);
	server_ = &server;
	submitted_ = false;
	server.on("/", handleRoot);
	server.on("/save", HTTP_POST, handleSave);
	server.onNotFound(redirectToRoot);  // captive-portal detection probes get redirected to "/"
	server.begin();

	while (!submitted_) {
		dns.processNextRequest();
		server.handleClient();
		// This loop blocks main.cpp's own loop() entirely while the portal is open - without this,
		// the LED never actually redraws (statusled::setProvisioning(true) only sets a flag; the
		// real rgbLedWrite() happens in statusled::loop(), which nothing else calls while we're
		// stuck here), so the purple provisioning indicator would silently never appear.
		statusled::loop();
		delay(2);
	}
	// Give the "Saved..." response a moment to actually flush to the client before persisting +
	// rebooting out from under the connection.
	delay(500);
	save(pending_);
	server.stop();
	dns.stop();
	ESP.restart();
}

}  // namespace config
}  // namespace blebridge
