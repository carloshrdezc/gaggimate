#include "LedControlPlugin.h"
#include <display/core/Controller.h>
#include <display/core/Event.h>

void LedControlPlugin::setup(Controller *controller, PluginManager *pluginManager) {
    this->controller = controller;
    pluginManager->on(EventIds::CONTROLLER_READY, [this](Event const) { initialized = true; });
    // `controller:ready` is one-shot (gated by Controller::loaded), so it does not
    // re-fire on a BLE reconnect. `controller:bluetooth:connect` fires on every
    // successful connectToServer(), including reconnects, so re-arm a full resend
    // there: sendLedControl is a no-op while disconnected, and the last_* cache may
    // still match the desired state, leaving controller-side LEDs stale otherwise.
    pluginManager->on(EventIds::CONTROLLER_BLUETOOTH_CONNECT, [this](Event const &) { firstSend = true; });
}

void LedControlPlugin::loop() {
    if (!initialized) {
        return;
    }
    if (millis() - lastUpdate >= UPDATE_INTERVAL) {
        lastUpdate = millis();
        updateControl();
    }
}

void LedControlPlugin::updateControl() {
    Settings settings = this->controller->getSettings();
    int mode = this->controller->getMode();
    ProcessSnapshot processSnapshot = this->controller->getProcessSnapshot();
    if (mode == MODE_STANDBY) {
        sendControl(0, 0, 0, 0, 0);
        return;
    }
    if (this->controller->isActiveSafe() && mode == MODE_BREW) {
        sendControl(0, 0, 255, 20, settings.getSunriseExtBrightness());
        return;
    }
    if (processSnapshot.exists && !processSnapshot.isActive && processSnapshot.type == MODE_BREW && mode == MODE_BREW) {
        sendControl(0, 255, 0, 20, settings.getSunriseExtBrightness());
        return;
    }
    if (this->controller->isLowWaterLevel()) {
        sendControl(255, 0, 0, 20, settings.getSunriseExtBrightness());
        return;
    }
    sendControl(settings.getSunriseR(), settings.getSunriseG(), settings.getSunriseB(), settings.getSunriseW(),
                settings.getSunriseExtBrightness());
}

void LedControlPlugin::sendControl(uint8_t r, uint8_t g, uint8_t b, uint8_t w, uint8_t ext) {
    if (firstSend || r != last_r)
        this->controller->getClientController()->sendLedControl(0, r);
    if (firstSend || g != last_g)
        this->controller->getClientController()->sendLedControl(1, g);
    if (firstSend || b != last_b)
        this->controller->getClientController()->sendLedControl(2, b);
    if (firstSend || w != last_w)
        this->controller->getClientController()->sendLedControl(3, w);
    if (firstSend || ext != last_ext) {
        this->controller->getClientController()->sendLedControl(4, 255 - ext);
        this->controller->getClientController()->sendLedControl(5, 255 - ext);
        this->controller->getClientController()->sendLedControl(6, 255 - ext);
        this->controller->getClientController()->sendLedControl(7, 255 - ext);
    }
    last_r = r;
    last_g = g;
    last_b = b;
    last_w = w;
    last_ext = ext;
    firstSend = false;
}
