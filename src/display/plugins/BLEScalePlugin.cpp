#include "BLEScalePlugin.h"

#if GAGGIMATE_ENABLE_BLE_SCALE

#include "BLEScaleScanPolicy.h"
#include "ShotHistoryPlugin.h"
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
    // Disable active flag first so onMeasurement() short-circuits immediately.
    active = false;

    // PRO-351: tear down in the right order with a real handshake instead of a
    // blind delay(). Stop async scanning FIRST so the scanner (a NimBLEScanCallbacks)
    // stops dispatching onResult callbacks, then disconnect() — which drops the
    // scale connection and bounded-waits for any in-flight weight callback to
    // drain before freeing the scale object. A fixed sleep is a timing
    // assumption, not a lifetime guarantee; the drain wait observes the actual
    // "no callback in flight" condition (see waitForCallbacksToDrain).
    if (scanner != nullptr) {
        scanner->stopAsyncScan();
    }

    disconnect();

    if (scanner != nullptr) {
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
        // Clear any in-flight steam grace so a BLE disconnect during the window
        // doesn't leave the pending flag dangling until the deadline.
        steamDisconnectPending = false;
        steamGraceDeadline = 0;
        active = false;
        disconnect();
        // PRO-351: scanner can be null on the std::nothrow OOM path (setup at
        // :76-78 logs-but-does-not-abort), so guard like the sibling call sites.
        if (scanner != nullptr) {
            scanner->stopAsyncScan();
        }
    });
    manager->on("controller:brew:prestart", [this](Event const &) { onProcessStart(); });
    manager->on("controller:grind:start", [this](Event const &) { onProcessStart(); });
    manager->on("controller:mode:change", [this](Event const &event) {
        const int newMode = event.getInt("value");
        const int oldMode = previousMode;
        previousMode = newMode;

        // A same-mode re-fire (e.g. WebUIPlugin re-sending req:change-mode with
        // the current mode) is a no-op: bail before any scan/teardown logic so a
        // redundant STEAM->STEAM event can't collapse an in-flight grace window
        // into an immediate disconnect.
        if (isRedundantModeChange(oldMode, newMode)) {
            return;
        }

        if (shouldScanForBleScaleMode(newMode)) {
            // Entering (or staying in) a scanning mode: resume scanning and
            // cancel any pending steam-grace teardown.
            steamDisconnectPending = false;
            steamGraceDeadline = 0;
            ESP_LOGI("BLEScalePlugin", "Resuming scanning");
            scan();
            active = true;
        } else if (shouldStartSteamScaleGrace(oldMode, newMode)) {
            // Scanning-mode -> STEAM: arm the grace window. On the normal auto-steam
            // path the drips were already captured during the MODE_BREW hold, so
            // loop() will tear the scale down promptly once isExtendedRecording() is
            // false; the deadline below is only the hard-cap fallback.
            steamDisconnectPending = true;
            steamGraceDeadline = millis() + STEAM_SCALE_GRACE_PERIOD_MS;
            ESP_LOGI("BLEScalePlugin", "Entering steam from scanning mode, keeping scale alive for %lums",
                     STEAM_SCALE_GRACE_PERIOD_MS);
        } else {
            // Any other non-scanning mode (standby/water, or steam not reached
            // from a scanning mode): disconnect immediately as before.
            steamDisconnectPending = false;
            steamGraceDeadline = 0;
            tearDownScale();
            ESP_LOGI("BLEScalePlugin", "Stopping scanning, disconnecting");
        }
    });
}

void BLEScalePlugin::loop() {
    if (doConnect && scale == nullptr) {
        establishConnection();
    }
    const unsigned long now = millis();

    // PRO-248: Steam grace window teardown. The actual drip capture does NOT happen
    // here — it happens earlier, while the machine is still in MODE_BREW and the
    // BLUETOOTH source is latched (see DefaultUI's pendingAutoSteam hold, which waits
    // for ShotHistory.isExtendedRecording() to go false before switching to STEAM).
    // By the time STEAM is entered and steamDisconnectPending is armed, that recording
    // window is normally already closed, so the check below fires on the next tick and
    // we tear the scale down PROMPTLY rather than idling for the full grace.
    //
    // recordingWindowClosed (!isExtendedRecording) is the normal trigger; steamGraceDeadline
    // (millis() + POST_STOP_GRACE_DURATION_MS, the shared cap) is a hard-cap safety net for
    // the unlikely case that STEAM is entered with a recording window still open — we still
    // tear down at the cap rather than holding the scale forever. Casting the diff to signed
    // handles millis() rollover safely.
    if (steamDisconnectPending) {
        const bool capElapsed = (long)(now - steamGraceDeadline) >= 0;
        const bool recordingWindowClosed = !ShotHistory.isExtendedRecording();
        if (capElapsed || recordingWindowClosed) {
            if (controller != nullptr && controller->getMode() == MODE_STEAM) {
                tearDownScale();
                ESP_LOGI("BLEScalePlugin", "Steam grace window %s, disconnecting scale",
                         capElapsed ? "hit hard cap" : "closed (extended recording done)");
            }
            steamDisconnectPending = false;
            steamGraceDeadline = 0;
        }
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
        // PRO-351: order matters for the cross-task lifetime handshake.
        // 1. active=false was already set by the teardown caller, so any callback
        //    that started before the flag flipped is the only one that can still
        //    be touching `scale`. Wait (bounded) for it to drain.
        waitForCallbacksToDrain();

        // 2. Now that no weight callback is in flight, it is safe to disconnect
        //    and free the scale.
        if (scale) {
            scale->disconnect();
        }

        // 3. `scale` is a std::unique_ptr<RemoteScales> (see header): assigning
        //    nullptr runs the deleter, which calls ~RemoteScales() ->
        //    clientCleanup() and frees the object. The plugin OWNS the scale
        //    (RemoteScalesFactory::create() returns a unique_ptr by value, moved
        //    into this member in establishConnection()), so no explicit delete is
        //    needed and adding one would be a double-free. Resetting the
        //    smart pointer here IS the deallocation.
        scale = nullptr;
        uuid = "";
        doConnect = false;
        reconnectionTries = 0;
    }
}

void BLEScalePlugin::waitForCallbacksToDrain() {
    // PRO-351: bounded wait for any in-flight NimBLE weight callback to finish.
    // Replaces the old blind delay(50): a fixed sleep is a timing assumption,
    // this observes the actual "no callback in flight" flag. The hard cap
    // guarantees teardown can never hang on a wedged callback task — if the flag
    // is still set at the deadline we proceed anyway (no worse than the old
    // unconditional sleep, and the active=false short-circuit limits the window).
    constexpr unsigned long CALLBACK_DRAIN_TIMEOUT_MS = 100;
    const unsigned long deadline = millis() + CALLBACK_DRAIN_TIMEOUT_MS;
    while (callbackInFlight.load(std::memory_order_acquire)) {
        if ((long)(millis() - deadline) >= 0) {
            ESP_LOGW("BLEScalePlugin", "Callback drain timed out after %lums, proceeding with teardown",
                     CALLBACK_DRAIN_TIMEOUT_MS);
            break;
        }
        delay(1);
    }
}

void BLEScalePlugin::tearDownScale() {
    active = false;
    disconnect();
    if (scanner != nullptr) {
        scanner->stopAsyncScan();
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
                // Skip measurement from ISR context to avoid FreeRTOS deadlocks.
                // Kept ABOVE the in-flight flag so an ISR-context call bails
                // without ever raising it.
                if (xPortInIsrContext()) {
                    return;
                }
                // PRO-351: mark the callback in flight for the duration of the
                // measurement so a concurrent teardown (waitForCallbacksToDrain)
                // doesn't free the scale out from under this NimBLE-host-task
                // callback. The flag is on the global BLEScales instance because
                // this is a plain function-pointer callback (cannot capture this).
                //
                // PRO-353: clear the flag via an RAII guard so it is cleared on
                // EVERY exit path. onMeasurement() routes into
                // Controller::onVolumetricMeasurement() -> PluginManager::trigger()
                // which invokes arbitrary registered handlers; if any of them (or
                // onMeasurement itself) throws or early-returns, the guard's
                // destructor still clears the flag, so teardown can never wedge on
                // a stuck callbackInFlight.
                //
                // RESIDUAL-WINDOW CONSTRAINT (PRO-353): this flag fences ONLY the
                // onMeasurement leg. It does NOT cover driver-frame state the scale
                // driver may touch AFTER RemoteScales::setWeight() returns but still
                // inside the same NimBLE-host-task notify frame (e.g. acaia's
                // dataBuffer.erase(...) runs after the weight dispatch, outside this
                // flagged window). A future scale driver whose notify handler reads
                // or mutates more object state after setWeight() returns would grow
                // that unprotected post-dispatch tail. The flag NARROWS the
                // use-after-free window to the measurement leg; it does not close it.
                //
                // BACKSTOP (PRO-353, item 1): the residual post-setWeight tail is
                // backstopped by NimBLE's own single-threaded host task on client
                // teardown. BLEScalePlugin::disconnect() calls
                // RemoteScales::disconnect() -> clientCleanup() ->
                // NimBLEDevice::deleteClient(). With NimBLE-Arduino 2.x that sets
                // deleteOnDisconnect and terminates the link; the client is not
                // freed inline but from the BLE_GAP_EVENT_DISCONNECT handler, which
                // runs ON the NimBLE host task — the SAME task that dispatches these
                // notify/weight callbacks. So the free is serialized after any
                // in-flight notify frame on that task returns; it cannot interleave
                // with the driver's post-setWeight tail. We rely on that
                // single-threaded-host-task ordering — NOT a full RemoteScales
                // driver-dispatch-boundary deregistration handshake (scoped out of
                // PR #337 as disproportionate for this plugin and high-regression on
                // the vendored driver layer) — to bound the residual tail.
                struct CallbackInFlightGuard {
                    ~CallbackInFlightGuard() { BLEScales.markCallbackInFlight(false); }
                };
                BLEScales.markCallbackInFlight(true);
                CallbackInFlightGuard guard;
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
