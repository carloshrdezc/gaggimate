#include "BLEScalePlugin.h"

#if GAGGIMATE_ENABLE_BLE_SCALE

#include "BLEScaleScanPolicy.h"
#include "remote_scales.h"
#include "remote_scales_plugin_registry.h"
#include <cmath> // For isfinite()
#include <display/core/Controller.h>
#include <scales/acaia.h>
#include <scales/bookoo.h>
#include <scales/decent.h>
#include <scales/difluid.h>
#include <scales/eclair.h>
#include <scales/eureka.h>
#include <scales/felicitaScale.h>
#include <scales/myscale.h>
#include <scales/timemore.h>
#include <scales/varia.h>
#include <scales/weighmybru.h>

void on_ble_measurement(float value) { BLEScales.onMeasurement(value); }

BLEScalePlugin BLEScales;

BLEScalePlugin::BLEScalePlugin() = default;

BLEScalePlugin::~BLEScalePlugin() {
    // Disable active flag first to stop processing
    active = false;

    // Give any running callbacks time to complete
    delay(100);

    // Ensure proper cleanup
    disconnect();

    if (scanner != nullptr) {
        // Stop scanning first
        scanner->stopAsyncScan();
        // Give it time to actually stop
        delay(50);
        delete scanner;
        scanner = nullptr;
    }
}

void BLEScalePlugin::setup(Controller *controller, PluginManager *manager) {
    if (controller == nullptr || manager == nullptr) {
        ESP_LOGE("BLEScalePlugin", "Invalid controller or manager passed to setup");
        return;
    }

    this->controller = controller;
    this->pluginRegistry = RemoteScalesPluginRegistry::getInstance();

    // Seed the previous-mode tracker with the controller's current mode so the
    // very first mode-change transition is classified correctly.
    this->previousMode = controller->getMode();

    // Apply scale plugins with error checking
    AcaiaScalesPlugin::apply();
    BookooScalesPlugin::apply();
    DecentScalesPlugin::apply();
    DifluidScalesPlugin::apply();
    EclairScalesPlugin::apply();
    EurekaScalesPlugin::apply();
    FelicitaScalePlugin::apply();
    TimemoreScalesPlugin::apply();
    VariaScalesPlugin::apply();
    WeighMyBrewScalePlugin::apply();
    myscalePlugin::apply();

    // Initialize scanner with error handling
    this->scanner = new (std::nothrow) RemoteScalesScanner();
    if (this->scanner == nullptr) {
        ESP_LOGE("BLEScalePlugin", "Failed to create RemoteScalesScanner - out of memory");
        return;
    }

    manager->on("controller:bluetooth:connect", [this](Event const &) {
        if (this->controller != nullptr && shouldScanForBleScaleMode(this->controller->getMode())) {
            ESP_LOGI("BLEScalePlugin", "Resuming scanning");
            scan();
            active = true;
        }
    });
    manager->on("controller:bluetooth:disconnect", [this](Event const &) {
        ESP_LOGW("BLEScalePlugin", "Controller disconnected, stopping BLE scan");
        active = false;
        disconnect();
        scanner->stopAsyncScan();
    });
    manager->on("controller:brew:prestart", [this](Event const &) { onProcessStart(); });
    manager->on("controller:grind:start", [this](Event const &) { onProcessStart(); });
    manager->on("controller:mode:change", [this](Event const &event) {
        const int newMode = event.getInt("value");
        const int oldMode = previousMode;
        previousMode = newMode;

        if (shouldScanForBleScaleMode(newMode)) {
            // Entering (or staying in) a scanning mode: resume scanning and
            // cancel any pending steam-grace teardown.
            steamDisconnectPending = false;
            steamGraceDeadline = 0;
            ESP_LOGI("BLEScalePlugin", "Resuming scanning");
            scan();
            active = true;
        } else if (shouldStartSteamScaleGrace(oldMode, newMode)) {
            // Scanning-mode -> STEAM: keep the scale alive for a short grace
            // window so it can capture the last drops, then disconnect in loop().
            steamDisconnectPending = true;
            steamGraceDeadline = millis() + STEAM_SCALE_GRACE_PERIOD_MS;
            ESP_LOGI("BLEScalePlugin", "Entering steam from scanning mode, keeping scale alive for %lums",
                     STEAM_SCALE_GRACE_PERIOD_MS);
        } else {
            // Any other non-scanning mode (standby/water, or steam not reached
            // from a scanning mode): disconnect immediately as before.
            steamDisconnectPending = false;
            steamGraceDeadline = 0;
            active = false;
            disconnect();
            if (scanner != nullptr) {
                scanner->stopAsyncScan();
            }
            ESP_LOGI("BLEScalePlugin", "Stopping scanning, disconnecting");
        }
    });
}

void BLEScalePlugin::loop() {
    if (doConnect && scale == nullptr) {
        establishConnection();
    }
    const unsigned long now = millis();

    // Steam grace window: once the deadline passes (and we're still in steam),
    // perform the same teardown the immediate path does. Done here so timing is
    // non-blocking. Casting the diff to signed handles millis() rollover safely.
    if (steamDisconnectPending && (long)(now - steamGraceDeadline) >= 0) {
        if (controller != nullptr && controller->getMode() == MODE_STEAM) {
            active = false;
            disconnect();
            if (scanner != nullptr) {
                scanner->stopAsyncScan();
            }
            ESP_LOGI("BLEScalePlugin", "Steam grace window elapsed, disconnecting scale");
        }
        steamDisconnectPending = false;
        steamGraceDeadline = 0;
    }

    if (now - lastUpdate > UPDATE_INTERVAL_MS) {
        lastUpdate = now;
        update();
    }
}

void BLEScalePlugin::update() {
    // Graceful failure - if controller is null, just disable ourselves
    if (controller == nullptr) {
        ESP_LOGW("BLEScalePlugin", "Controller is null, disabling BLE scale");
        active = false;
        return;
    }

    // Don't update volumetric override if scale access might fail
    bool hasConnectedScale = false;
    if (scale != nullptr) {
        // Check if scale pointer is valid before accessing
        hasConnectedScale = scale->isConnected();
    }

    if (controller->isVolumetricAvailable())
        controller->setVolumetricOverride(hasConnectedScale);

    if (!active)
        return;

    if (scale != nullptr) {
        // Call scale update with error checking
        scale->update();
        if (!hasConnectedScale) {
            reconnectionTries++;
            if (reconnectionTries > RECONNECTION_TRIES) {
                ESP_LOGW("BLEScalePlugin", "Max reconnection attempts reached, disconnecting");
                disconnect();
                if (scanner != nullptr) {
                    scanner->initializeAsyncScan();
                }
            }
        }
    } else if (controller->getSettings().getSavedScale() != "" && scanner != nullptr) {
        // Protected scanner access with null checks
        auto discoveredScales = scanner->getDiscoveredScales();
        for (const auto &d : discoveredScales) {
            if (d.getAddress().toString() == controller->getSettings().getSavedScale().c_str()) {
                ESP_LOGI("BLEScalePlugin", "Connecting to last known scale");
                connect(d.getAddress().toString());
                break;
            }
        }
    }
}

void BLEScalePlugin::connect(const std::string &uuid) {
    if (uuid.empty()) {
        ESP_LOGE("BLEScalePlugin", "Cannot connect with empty UUID");
        return;
    }
    if (controller == nullptr) {
        ESP_LOGE("BLEScalePlugin", "Controller is null, cannot save scale setting");
        return;
    }

    doConnect = true;
    this->uuid = uuid;
    controller->getSettings().setSavedScale(uuid.data());
}

void BLEScalePlugin::scan() const {
    if (scale != nullptr && scale->isConnected()) {
        return;
    }
    if (scanner == nullptr) {
        ESP_LOGE("BLEScalePlugin", "Scanner not initialized, cannot start scan");
        return;
    }
    scanner->initializeAsyncScan();
}

void BLEScalePlugin::disconnect() {
    if (scale != nullptr) {
        // Add small delay to let any pending callbacks complete
        delay(50);

        // Check if scale is still valid before calling disconnect
        if (scale) {
            scale->disconnect();
        }

        scale = nullptr;
        uuid = "";
        doConnect = false;
        reconnectionTries = 0;
    }
}

void BLEScalePlugin::onProcessStart() const {
    if (scale != nullptr && scale->isConnected()) {
        // Double tare with validation
        scale->tare();
        delay(50);

        // Check if scale is still connected before second tare
        if (scale != nullptr && scale->isConnected()) {
            scale->tare();
        }
    }
}

void BLEScalePlugin::tare() const { onProcessStart(); }

void BLEScalePlugin::establishConnection() {
    if (uuid.empty()) {
        ESP_LOGE("BLEScalePlugin", "Cannot establish connection with empty UUID");
        return;
    }

    ESP_LOGI("BLEScalePlugin", "Connecting to %s", uuid.c_str());
    if (scanner == nullptr) {
        ESP_LOGE("BLEScalePlugin", "Scanner not initialized, cannot establish connection");
        return;
    }

    scanner->stopAsyncScan();

    auto discoveredScales = scanner->getDiscoveredScales();
    bool deviceFound = false;

    for (const auto &d : discoveredScales) {
        if (d.getAddress().toString() == uuid) {
            deviceFound = true;
            reconnectionTries = 0;

            auto factory = RemoteScalesFactory::getInstance();
            if (factory == nullptr) {
                ESP_LOGE("BLEScalePlugin", "RemoteScalesFactory instance is null");
                return;
            }

            scale = factory->create(d);
            if (!scale) {
                ESP_LOGE("BLEScalePlugin", "Connection to device %s failed", d.getName().c_str());
                return;
            }

            scale->setLogCallback([](std::string message) {
                if (!message.empty()) {
                    Serial.print(message.c_str());
                }
            });

            scale->setWeightUpdatedCallback([](float weight) {
                // Skip measurement from ISR context to avoid FreeRTOS deadlocks
                if (xPortInIsrContext()) {
                    return;
                }
                BLEScales.onMeasurement(weight);
            });

            bool connectResult = scale->connect();
            if (!connectResult) {
                ESP_LOGW("BLEScalePlugin", "Failed to connect to scale, retrying scan");
                disconnect();
                if (scanner != nullptr) {
                    scanner->initializeAsyncScan();
                }
            }
            break;
        }
    }

    if (!deviceFound) {
        ESP_LOGW("BLEScalePlugin", "Device %s not found in discovered scales", uuid.c_str());
        if (scanner != nullptr) {
            scanner->initializeAsyncScan();
        }
    }
}

void BLEScalePlugin::onMeasurement(float value) const {
    lastWeight = value;
    // Rate limiting to prevent callback flooding
    unsigned long now = millis();
    if (now - lastMeasurementTime < MIN_MEASUREMENT_INTERVAL_MS) {
        return; // Drop measurement to prevent flooding
    }
    lastMeasurementTime = now;

    // Multiple safety checks to prevent crashes
    if (controller == nullptr) {
        return; // Silently ignore if controller is null
    }

    // Check if we're being destroyed or in an unsafe state
    if (!active) {
        return; // Don't process measurements when not active
    }

    // Validate the measurement value
    if (!isfinite(value) || value < -1000.0f || value > 10000.0f) {
        ESP_LOGW("BLEScalePlugin", "Invalid measurement value: %f, ignoring", value);
        return;
    }

    // Safe to call controller method
    controller->onVolumetricMeasurement(value, VolumetricMeasurementSource::BLUETOOTH);
}

std::vector<DiscoveredDevice> BLEScalePlugin::getDiscoveredScales() const {
    if (scanner == nullptr) {
        ESP_LOGW("BLEScalePlugin", "Scanner not initialized, returning empty device list");
        return std::vector<DiscoveredDevice>();
    }
    return scanner->getDiscoveredScales();
}

#else // GAGGIMATE_ENABLE_BLE_SCALE

// BLE scale compiled out (CAR-382): the heavy implementation above (and the
// entire esp-arduino-ble-scales library it pulls in) is excluded from the
// build. Only the global instance — referenced by address in Controller.cpp
// and by name in the SquareLine-generated ui_events.cpp — is defined here, as
// the lightweight no-op stub declared in the header.
BLEScalePlugin BLEScales;

#endif // GAGGIMATE_ENABLE_BLE_SCALE
