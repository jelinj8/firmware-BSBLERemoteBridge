#include "BleCentral.h"

#include <NimBLEDevice.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <algorithm>
#include <map>
#include <vector>

#include "HexCodec.h"
#include "Log.h"

namespace blebridge {
namespace blecentral {

namespace {

NimBLEScan* scan_ = nullptr;
bool initialized_ = false;

// Populated from scan results (address -> the NimBLEAddress NimBLE resolved it with, including
// its address-type bit) - connect() only ever works against an address seen this way, mirroring
// main.rs's get_peripheral contract ("scan for it first on this same adapter/process").
std::map<std::string, NimBLEAddress> knownAddresses_;

// Populated on successful connect, erased on disconnect.
std::map<std::string, NimBLEClient*> clients_;

// The `scan` command's response is deferred until the scan actually ends (matching main.rs, which
// only responds after its own timeout_ms sleep) - there is only ever one scan in flight at a time
// (NimBLE-Arduino has a single shared NimBLEScan), so a single pending id is enough.
std::string currentScanId_;
std::string currentScanFilterUuid_;

struct PendingPair {
	bool active = false;
	std::string id;
	uint32_t pin = 0;
};
PendingPair pendingPair_;

// NimBLE's scan/client/notify callbacks run on its own FreeRTOS host task, not this firmware's
// Arduino main loop - queueEvent/pollEvents is the hand-off point so nothing touches the
// WebSocket link (RemoteLink, driven only from the main loop) from a different task.
SemaphoreHandle_t eventMutex_ = nullptr;
std::vector<String> eventQueue_;

void queueEvent(const String& line) {
	if (eventMutex_ == nullptr) {
		return;
	}
	xSemaphoreTake(eventMutex_, portMAX_DELAY);
	eventQueue_.push_back(line);
	xSemaphoreGive(eventMutex_);
}

String addressToString(const NimBLEAddress& addr) {
	String s(addr.toString().c_str());
	s.toUpperCase();
	return s;
}

// Promotes to the full 128-bit form first so a well-known 16/32-bit UUID (which NimBLE's own
// toString() otherwise renders as a short "0x180d" form) still comes out as the same full
// hyphenated lowercase string main.rs's `uuid::Uuid` always produces - the Java side parses with
// java.util.UUID.fromString, which requires that full form.
String uuidToString(NimBLEUUID uuid) {
	uuid.to128();
	return String(uuid.toString().c_str());
}

String okResponse(const String& id, std::function<void(JsonObject)> fill = nullptr) {
	JsonDocument doc;
	doc["type"] = "response";
	doc["id"] = id;
	doc["ok"] = true;
	if (fill) {
		fill(doc.as<JsonObject>());
	}
	String out;
	serializeJson(doc, out);
	return out;
}

String errResponse(const String& id, const String& message) {
	JsonDocument doc;
	doc["type"] = "response";
	doc["id"] = id;
	doc["ok"] = false;
	doc["error"] = message;
	String out;
	serializeJson(doc, out);
	return out;
}

void emitDeviceFound(const String& address, const String& name, int rssi) {
	JsonDocument doc;
	doc["type"] = "device_found";
	doc["address"] = address;
	doc["name"] = name;
	doc["rssi"] = rssi;
	String out;
	serializeJson(doc, out);
	queueEvent(out);
}

void emitConnected(const String& address) {
	JsonDocument doc;
	doc["type"] = "connected";
	doc["address"] = address;
	String out;
	serializeJson(doc, out);
	queueEvent(out);
}

void emitDisconnected(const String& address, const String& reason) {
	JsonDocument doc;
	doc["type"] = "disconnected";
	doc["address"] = address;
	doc["reason"] = reason;
	String out;
	serializeJson(doc, out);
	queueEvent(out);
}

void emitNotification(const String& address, const String& charUuid, const String& valueHex) {
	JsonDocument doc;
	doc["type"] = "notification";
	doc["address"] = address;
	doc["char_uuid"] = charUuid;
	doc["value_hex"] = valueHex;
	String out;
	serializeJson(doc, out);
	queueEvent(out);
}

NimBLEClient* clientFor(const String& address) {
	auto it = clients_.find(std::string(address.c_str()));
	if (it == clients_.end() || !it->second->isConnected()) {
		return nullptr;
	}
	return it->second;
}

// Shared by "write" and "write_stream": NimBLERemoteValueAttribute::writeValue only takes the
// plain ATT "Write Command" path (a single ble_gattc_write_no_rsp_flat call) when the payload
// fits in this connection's negotiated MTU; anything larger silently falls through to
// ble_gattc_write_long instead - an ATT prepare-write/execute-write sequence - *regardless* of
// withResponse being false. That's a different wire protocol than a real "write without
// response", and one most simple peripherals' write-without-response characteristics (this
// firmware's own target devices among them - see doc/PROTOCOL.md) don't implement correctly, or
// handle with much higher per-write overhead. A caller's chunk size is chosen without knowing
// this link's real negotiated MTU, so fragment here to the real ceiling instead of ever taking
// that fallback - every fragment stays a genuine ATT Write Command.
bool writeRespectingMtu(NimBLEClient* client, NimBLERemoteCharacteristic* ch, const uint8_t* data, size_t len,
		bool withResponse) {
	if (withResponse || len == 0) {
		return ch->writeValue(data, len, withResponse);
	}
	uint16_t mtu = client->getMTU();
	size_t maxChunk = mtu > 3 ? static_cast<size_t>(mtu) - 3 : 20;
	for (size_t offset = 0; offset < len; offset += maxChunk) {
		size_t chunkLen = std::min(maxChunk, len - offset);
		if (!ch->writeValue(data + offset, chunkLen, false)) {
			return false;
		}
	}
	return true;
}

NimBLERemoteCharacteristic* findCharacteristic(NimBLEClient* client, const String& serviceUuid, const String& charUuid,
		String& err) {
	NimBLERemoteService* svc = client->getService(serviceUuid.c_str());
	if (svc == nullptr) {
		// Not yet discovered on this connection - trigger discovery once, then retry, mirroring
		// main.rs's find_characteristic (only re-discovers if the local characteristic cache is
		// still empty, not on every lookup).
		client->getServices(true);
		svc = client->getService(serviceUuid.c_str());
	}
	if (svc == nullptr) {
		err = "service not found: " + serviceUuid;
		return nullptr;
	}
	NimBLERemoteCharacteristic* ch = svc->getCharacteristic(charUuid.c_str());
	if (ch == nullptr) {
		err = "characteristic " + charUuid + " not found on service " + serviceUuid;
		return nullptr;
	}
	return ch;
}

class ScanCallbacks : public NimBLEScanCallbacks {
  public:
	void onResult(const NimBLEAdvertisedDevice* dev) override {
		if (!currentScanFilterUuid_.empty()) {
			NimBLEUUID filterUuid(currentScanFilterUuid_);
			if (!dev->isAdvertisingService(filterUuid)) {
				return;
			}
		}
		String addr = addressToString(dev->getAddress());
		knownAddresses_[std::string(addr.c_str())] = dev->getAddress();
		emitDeviceFound(addr, String(dev->getName().c_str()), dev->getRSSI());
	}

	void onScanEnd(const NimBLEScanResults& /*results*/, int /*reason*/) override {
		if (!currentScanId_.empty()) {
			queueEvent(okResponse(String(currentScanId_.c_str())));
			currentScanId_.clear();
		}
	}
};
ScanCallbacks scanCallbacks_;

class ClientCallbacks : public NimBLEClientCallbacks {
  public:
	void onConnect(NimBLEClient* client) override {
		emitConnected(addressToString(client->getPeerAddress()));
	}

	void onDisconnect(NimBLEClient* client, int reason) override {
		String addr = addressToString(client->getPeerAddress());
		clients_.erase(std::string(addr.c_str()));
		emitDisconnected(addr, "code_" + String(reason));
	}

	// The peer is asking us to supply a passkey - answer with whatever PIN the current `pair`
	// command carried, instead of the usual "prompt a human" flow. This is the crux of
	// programmatic pairing (see CLAUDE.md's "Planned: programmatic pairing" note this firmware is
	// the first implementation of).
	void onPassKeyEntry(NimBLEConnInfo& connInfo) override {
		if (pendingPair_.active) {
			log::ble("pair: supplying passkey for pending pair request");
			NimBLEDevice::injectPassKey(connInfo, pendingPair_.pin);
		}
	}

	void onAuthenticationComplete(NimBLEConnInfo& connInfo) override {
		if (!pendingPair_.active) {
			return;
		}
		String id(pendingPair_.id.c_str());
		if (connInfo.isEncrypted() && connInfo.isAuthenticated()) {
			queueEvent(okResponse(id));
		} else {
			queueEvent(errResponse(id, "pairing/authentication failed"));
		}
		pendingPair_.active = false;
	}
};
ClientCallbacks clientCallbacks_;

}  // namespace

void begin(const String& deviceName) {
	eventMutex_ = xSemaphoreCreateMutex();
	NimBLEDevice::init(deviceName.c_str());
	// Bonding + MITM protection + secure connections, keyboard-only IO capability so a peer's
	// passkey request is answered programmatically (onPassKeyEntry/injectPassKey below) instead of
	// prompting - no OS-level or physical UI involved, per the `pair` command's whole point.
	NimBLEDevice::setSecurityAuth(true, true, true);
	NimBLEDevice::setSecurityIOCap(BLE_HS_IO_KEYBOARD_ONLY);

	scan_ = NimBLEDevice::getScan();
	scan_->setScanCallbacks(&scanCallbacks_);
	scan_->setActiveScan(true);
	initialized_ = true;
}

void pollEvents(EventEmitter emit) {
	if (eventMutex_ == nullptr) {
		return;
	}
	std::vector<String> drained;
	xSemaphoreTake(eventMutex_, portMAX_DELAY);
	drained.swap(eventQueue_);
	xSemaphoreGive(eventMutex_);
	for (const auto& line : drained) {
		emit(line);
	}
}

void resetSession() {
	if (scan_ != nullptr) {
		scan_->stop();
	}
	for (auto& kv : clients_) {
		if (kv.second->isConnected()) {
			kv.second->disconnect();
		}
		NimBLEDevice::deleteClient(kv.second);
	}
	clients_.clear();
	knownAddresses_.clear();
	pendingPair_.active = false;
	currentScanId_.clear();
	if (eventMutex_ != nullptr) {
		xSemaphoreTake(eventMutex_, portMAX_DELAY);
		eventQueue_.clear();
		xSemaphoreGive(eventMutex_);
	}
}

void disconnectAll() {
	// Snapshot first rather than disconnecting while iterating clients_ directly: onDisconnect
	// (ClientCallbacks above) erases from that same map, and doing so mid-iteration here would be
	// asking for trouble regardless of which task it actually fires from.
	std::vector<NimBLEClient*> connected;
	for (auto& kv : clients_) {
		if (kv.second->isConnected()) {
			connected.push_back(kv.second);
		}
	}
	for (auto* client : connected) {
		client->disconnect();
	}
}

String handleCommand(const String& cmd, const String& id, JsonObjectConst params) {
	String address = params["address"] | "";

	if (cmd == "scan") {
		String filterUuid = params["filter_service_uuid"] | "";
		uint32_t timeoutMs = params["timeout_ms"] | 10000;
		currentScanFilterUuid_ = std::string(filterUuid.c_str());
		currentScanId_ = std::string(id.c_str());
		if (!scan_->start(timeoutMs, false, true)) {
			currentScanId_.clear();
			return errResponse(id, "failed to start scan");
		}
		return "";  // deferred - ScanCallbacks::onScanEnd emits the real response
	}

	if (cmd == "stop_scan") {
		scan_->stop();
		return okResponse(id);
	}

	if (cmd == "connect") {
		std::string key(address.c_str());
		auto known = knownAddresses_.find(key);
		if (known == knownAddresses_.end()) {
			return errResponse(id,
					"unknown peripheral address " + address
							+ " - scan for it first (each session's scan cache is independent)");
		}
		NimBLEClient* client = NimBLEDevice::getClientByPeerAddress(known->second);
		if (client == nullptr) {
			client = NimBLEDevice::createClient(known->second);
			client->setClientCallbacks(&clientCallbacks_, false);
			client->setConnectTimeout(5000);
		}
		if (client->isConnected()) {
			clients_[key] = client;
			return okResponse(id);
		}
		if (!client->connect(known->second)) {
			return errResponse(id, "connect failed");
		}
		clients_[key] = client;
		// NimBLE's default connection interval (measured 50ms against a real peripheral) is far
		// looser than what real OS BLE stacks commonly negotiate unprompted (~15ms or less) -
		// request the same tight interval `main.rs`'s `request_connection_parameters
		// "throughput_optimized"` preset uses, for every connection, for the lower latency and
		// link headroom.
		client->updateConnParams(6, 12, 0, 400);
		log::ble("connect: interval=%ums mtu=%u", static_cast<unsigned>(client->getConnInfo().getConnInterval()) * 5 / 4,
				static_cast<unsigned>(client->getMTU()));
		return okResponse(id);
	}

	if (cmd == "disconnect") {
		NimBLEClient* client = clientFor(address);
		if (client == nullptr) {
			return okResponse(id);  // already disconnected - harmless no-op, matches main.rs idempotence
		}
		return client->disconnect() ? okResponse(id) : errResponse(id, "disconnect failed");
	}

	if (cmd == "discover_services") {
		NimBLEClient* client = clientFor(address);
		if (client == nullptr) {
			return errResponse(id, "not connected: " + address);
		}
		const auto& services = client->getServices(true);
		return okResponse(id, [&](JsonObject o) {
			JsonArray svcArr = o["services"].to<JsonArray>();
			for (auto* svc : services) {
				JsonObject so = svcArr.add<JsonObject>();
				so["uuid"] = uuidToString(svc->getUUID());
				JsonArray chArr = so["characteristics"].to<JsonArray>();
				for (auto* ch : svc->getCharacteristics(true)) {
					JsonObject co = chArr.add<JsonObject>();
					co["uuid"] = uuidToString(ch->getUUID());
					JsonArray props = co["properties"].to<JsonArray>();
					if (ch->canRead()) {
						props.add("READ");
					}
					if (ch->canWrite()) {
						props.add("WRITE");
					}
					if (ch->canWriteNoResponse()) {
						props.add("WRITE_WITHOUT_RESPONSE");
					}
					if (ch->canNotify()) {
						props.add("NOTIFY");
					}
					if (ch->canIndicate()) {
						props.add("INDICATE");
					}
				}
			}
		});
	}

	if (cmd == "read") {
		NimBLEClient* client = clientFor(address);
		if (client == nullptr) {
			return errResponse(id, "not connected: " + address);
		}
		String serviceUuid = params["service_uuid"] | "";
		String charUuid = params["char_uuid"] | "";
		String err;
		NimBLERemoteCharacteristic* ch = findCharacteristic(client, serviceUuid, charUuid, err);
		if (ch == nullptr) {
			return errResponse(id, err);
		}
		NimBLEAttValue val = ch->readValue();
		String hex(hexcodec::encode(val.data(), val.length()).c_str());
		return okResponse(id, [&](JsonObject o) { o["value_hex"] = hex; });
	}

	if (cmd == "write") {
		NimBLEClient* client = clientFor(address);
		if (client == nullptr) {
			return errResponse(id, "not connected: " + address);
		}
		String serviceUuid = params["service_uuid"] | "";
		String charUuid = params["char_uuid"] | "";
		String valueHex = params["value_hex"] | "";
		bool withResponse = params["with_response"] | false;

		std::string hexStd(valueHex.c_str());
		if (hexStd.length() % 2 != 0) {
			return errResponse(id, "odd-length hex string");
		}
		for (char c : hexStd) {
			if (hexcodec::digit(c) < 0) {
				return errResponse(id, "invalid hex digit");
			}
		}
		String err;
		NimBLERemoteCharacteristic* ch = findCharacteristic(client, serviceUuid, charUuid, err);
		if (ch == nullptr) {
			return errResponse(id, err);
		}
		auto bytes = hexcodec::decode(hexStd);
		bool ok = writeRespectingMtu(client, ch, bytes.data(), bytes.size(), withResponse);
		return ok ? okResponse(id) : errResponse(id, "write failed");
	}

	if (cmd == "write_stream") {
		NimBLEClient* client = clientFor(address);
		if (client == nullptr) {
			return errResponse(id, "not connected: " + address);
		}
		String serviceUuid = params["service_uuid"] | "";
		String charUuid = params["char_uuid"] | "";
		String valueHex = params["value_hex"] | "";
		bool withResponse = params["with_response"] | false;
		size_t chunkSize = params["chunk_size"] | 128;
		uint32_t chunkDelayMs = params["chunk_delay_ms"] | 0;
		if (chunkSize == 0) {
			chunkSize = 128;
		}

		std::string hexStd(valueHex.c_str());
		if (hexStd.length() % 2 != 0) {
			return errResponse(id, "odd-length hex string");
		}
		for (char c : hexStd) {
			if (hexcodec::digit(c) < 0) {
				return errResponse(id, "invalid hex digit");
			}
		}
		String err;
		NimBLERemoteCharacteristic* ch = findCharacteristic(client, serviceUuid, charUuid, err);
		if (ch == nullptr) {
			return errResponse(id, err);
		}
		// Splits into chunkSize-byte pieces and writes each in turn, sleeping chunkDelayMs between
		// writes - all within this one command's handling (a plain sequential loop, no
		// concurrency), so a caller pays one round trip for the whole transfer instead of one per
		// chunk (see BlePeripheral.writeCharacteristicStream's doc for why that matters over a
		// higher-latency link). delay() yields to the WiFi/BLE FreeRTOS tasks (unlike a busy-wait),
		// so blocking the main loop for one command's duration is safe as long as the transfer
		// stays well under this firmware's WebSocket heartbeat tolerance.
		auto bytes = hexcodec::decode(hexStd);
		bool ok = true;
		for (size_t offset = 0; offset < bytes.size() && ok; offset += chunkSize) {
			if (offset > 0 && chunkDelayMs > 0) {
				delay(chunkDelayMs);
			}
			size_t len = std::min(chunkSize, bytes.size() - offset);
			ok = writeRespectingMtu(client, ch, bytes.data() + offset, len, withResponse);
		}
		return ok ? okResponse(id) : errResponse(id, "write failed");
	}

	if (cmd == "subscribe" || cmd == "unsubscribe") {
		NimBLEClient* client = clientFor(address);
		if (client == nullptr) {
			return errResponse(id, "not connected: " + address);
		}
		String serviceUuid = params["service_uuid"] | "";
		String charUuid = params["char_uuid"] | "";
		String err;
		NimBLERemoteCharacteristic* ch = findCharacteristic(client, serviceUuid, charUuid, err);
		if (ch == nullptr) {
			return errResponse(id, err);
		}
		if (cmd == "unsubscribe") {
			return ch->unsubscribe() ? okResponse(id) : errResponse(id, "unsubscribe failed");
		}
		String addrCopy = address;
		String charCopy = charUuid;
		bool ok = ch->subscribe(true, [addrCopy, charCopy](NimBLERemoteCharacteristic*, uint8_t* data, size_t len, bool) {
			emitNotification(addrCopy, charCopy, String(hexcodec::encode(data, len).c_str()));
		});
		return ok ? okResponse(id) : errResponse(id, "subscribe failed");
	}

	if (cmd == "pair") {
		NimBLEClient* client = clientFor(address);
		if (client == nullptr) {
			return errResponse(id, "not connected: " + address);
		}
		String pin = params["pin"] | "";
		pendingPair_.active = true;
		pendingPair_.id = std::string(id.c_str());
		pendingPair_.pin = static_cast<uint32_t>(pin.toInt());
		if (!client->secureConnection()) {
			pendingPair_.active = false;
			return errResponse(id, "failed to start pairing");
		}
		return "";  // deferred - ClientCallbacks::onAuthenticationComplete emits the real response
	}

	if (cmd == "adapter_state") {
		// NimBLE has no OS-level "radio power" concept the way btleplug's Central::adapter_state()
		// reflects one - "powered_on" once NimBLEDevice::init() has succeeded is the closest
		// equivalent this firmware can report.
		return okResponse(id, [&](JsonObject o) { o["state"] = initialized_ ? "powered_on" : "powered_off"; });
	}

	if (cmd == "read_rssi") {
		NimBLEClient* client = clientFor(address);
		if (client == nullptr) {
			return errResponse(id, "not connected: " + address);
		}
		return okResponse(id, [&](JsonObject o) { o["rssi"] = client->getRssi(); });
	}

	if (cmd == "get_mtu") {
		NimBLEClient* client = clientFor(address);
		if (client == nullptr) {
			return errResponse(id, "not connected: " + address);
		}
		return okResponse(id, [&](JsonObject o) { o["mtu"] = client->getMTU(); });
	}

	if (cmd == "get_connection_parameters") {
		NimBLEClient* client = clientFor(address);
		if (client == nullptr) {
			return errResponse(id, "not connected: " + address);
		}
		NimBLEConnInfo info = client->getConnInfo();
		return okResponse(id, [&](JsonObject o) {
			// getConnInterval()/getConnTimeout() are in 1.25ms/10ms units respectively (BLE spec
			// convention) - converted to microseconds here to match main.rs's cp.interval_us/
			// cp.supervision_timeout_us fields exactly; getConnLatency() is already a plain
			// interval-count, matching cp.latency as-is.
			o["interval_us"] = static_cast<uint32_t>(info.getConnInterval()) * 1250;
			o["latency"] = info.getConnLatency();
			o["supervision_timeout_us"] = static_cast<uint32_t>(info.getConnTimeout()) * 10000;
		});
	}

	if (cmd == "request_connection_parameters") {
		NimBLEClient* client = clientFor(address);
		if (client == nullptr) {
			return errResponse(id, "not connected: " + address);
		}
		String preset = params["preset"] | "balanced";
		// Illustrative BLE parameter presets (1.25ms/10ms units) - not expected to numerically
		// match whatever btleplug's own ConnectionParameterPreset resolves to on a given OS/
		// backend, only to be a reasonable directional match for the same three named presets.
		uint16_t minInterval, maxInterval, latency, timeout;
		if (preset == "throughput_optimized") {
			minInterval = 6;
			maxInterval = 12;
			latency = 0;
			timeout = 400;
		} else if (preset == "power_optimized") {
			minInterval = 400;
			maxInterval = 800;
			latency = 4;
			timeout = 600;
		} else {
			minInterval = 24;
			maxInterval = 40;
			latency = 0;
			timeout = 400;
		}
		bool ok = client->updateConnParams(minInterval, maxInterval, latency, timeout);
		return ok ? okResponse(id) : errResponse(id, "failed to request connection parameters");
	}

	return errResponse(id, "unknown command: " + cmd);
}

}  // namespace blecentral
}  // namespace blebridge
