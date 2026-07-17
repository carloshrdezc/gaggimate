#include "BoilerFillPlugin.h"
#include <WiFi.h>
#include <display/core/Controller.h>
#include <display/core/Event.h>
#include <display/core/EventIds.h>
#include <display/core/process/PumpProcess.h>

void BoilerFillPlugin::setup(Controller *controller, PluginManager *pluginManager) {
    this->controller = controller;
    pluginManager->on(EventIds::CONTROLLER_READY, [this](Event const &) {
        this->controller->startProcess(new PumpProcess(this->controller->getSettings().getStartupFillTime()));
    });
    pluginManager->on(EventIds::CONTROLLER_MODE_CHANGE, [this](Event const &event) {
        int newMode = event.getInt("value");
        if (newMode == MODE_BREW && this->controller->getMode() == MODE_STEAM) {
            this->controller->startProcess(new PumpProcess(this->controller->getSettings().getSteamFillTime()));
        }
    });
}
