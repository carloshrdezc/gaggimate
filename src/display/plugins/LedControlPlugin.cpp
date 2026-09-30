#include "LedControlPlugin.h"
#include <display/core/Controller.h>
#include <display/core/Event.h>
#include <display/core/EventIds.h>

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
    // PRO-655 R3 / B-P2-3: full link policy (connected + SystemInfo from THIS link +
    // no mismatch), not just the mismatch flag: after a reconnect the flag still
    // holds the previous link's value until the new SystemInfo arrives. firstSend
    // is left untouched, so the first allowed frame is still sent.
    if (!this->controller->isControlAllowed()) {
        return;
    }
    if (!firstSend && r == last_r && g == last_g && b == last_b && w == last_w && ext == last_ext) {
        return;
    }
    // PRO-655: one LedControl snapshot (NanoPbComm coalesces per payload type, so
    // per-channel sends would collapse to the last channel). Same channel map as before.
    const uint8_t extInv = 255 - ext;
    const LedChannelCommand channels[] = {
        {0, r}, {1, g}, {2, b}, {3, w}, {4, extInv}, {5, extInv}, {6, extInv}, {7, extInv},
    };
    this->controller->getClientController()->sendLedControl(channels, sizeof(channels) / sizeof(channels[0]));
    last_r = r;
    last_g = g;
    last_b = b;
    last_w = w;
    last_ext = ext;
    firstSend = false;
}
