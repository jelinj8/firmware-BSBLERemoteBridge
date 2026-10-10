#include "Ota.h"

#include <ArduinoOTA.h>

#include "BleCentral.h"
#include "Config.h"
#include "Log.h"

namespace blebridge {
namespace ota {

namespace {

bool started_ = false;

}  // namespace

void begin(const String& deviceName) {
	if (started_) {
		return;
	}
	String host = config::hostName(deviceName);
	ArduinoOTA.setHostname(host.c_str());
	ArduinoOTA.setPassword(config::macSuffix().c_str());
	ArduinoOTA.onStart([]() {
		log::ota("update started - releasing BLE peripherals");
		// The radio is shared with WiFi; drop BLE links so the upload isn't competing with them.
		blecentral::disconnectAll();
	});
	ArduinoOTA.onProgress([](unsigned int done, unsigned int total) {
		static unsigned int lastPercent = 101;
		unsigned int percent = total ? done * 100 / total : 0;
		if (percent % 10 == 0 && percent != lastPercent) {
			lastPercent = percent;
			log::ota("%u%%", percent);
		}
	});
	ArduinoOTA.onEnd([]() { log::ota("update written - rebooting"); });
	ArduinoOTA.onError([](ota_error_t error) { log::ota("update failed (error %u)", error); });
	ArduinoOTA.begin();
	started_ = true;
	log::ota("listening as '%s' (password: last 6 of %s)", host.c_str(), config::defaultDeviceName().c_str());
}

void loop() {
	if (started_) {
		ArduinoOTA.handle();
	}
}

}  // namespace ota
}  // namespace blebridge
