#include "mDNSPlugin.h"
#include "../config/features.h"
#include "../core/Controller.h"
#include "../core/Event.h"
#include "../core/EventIds.h"
#include <ESPmDNS.h>
#include <WiFi.h>
#include <esp_log.h>
#include <version.h>

static constexpr char LOG_TAG[] = "mDNSPlugin";

void mDNSPlugin::setup(Controller *controller, PluginManager *pluginManager) {
    this->controller = controller;
    pluginManager->on(EventIds::CONTROLLER_WIFI_CONNECT, [this](Event const &event) { start(event); });
}
void mDNSPlugin::start(Event const &event) const {
    const int apMode = event.getInt("AP");
    if (apMode)
        return;
    if (!MDNS.begin(controller->getSettings().getMdnsName().c_str())) {
        ESP_LOGE(LOG_TAG, "Error setting up mDNS responder");
        return;
    }

#if GAGGIMATE_ENABLE_WEBUI
    // Advertise HTTP service for web interface. All of these services point at
    // port 80, which is owned exclusively by WebUIPlugin's server. When the
    // WebUI is compiled out (GAGGIMATE_ENABLE_WEBUI=0) nothing listens on 80,
    // so advertising these would direct mDNS / Home Assistant / browser
    // discovery to a dead service (CAR-383). MDNS.begin() above stays
    // unconditional so the device remains resolvable by name.
    MDNS.addService("http", "tcp", 80);

    // Advertise custom gaggimate service for Home Assistant discovery
    MDNS.addService("gaggimate", "tcp", 80);

    // Add service metadata as TXT records
    MDNS.addServiceTxt("gaggimate", "tcp", "version", BUILD_GIT_VERSION);
    MDNS.addServiceTxt("gaggimate", "tcp", "type", "espresso_machine");
#endif

    ESP_LOGI(LOG_TAG, "mDNS responder started with service advertisement");
}
