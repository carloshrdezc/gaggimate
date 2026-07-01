#ifndef BLESCALEPLUGIN_H
#define BLESCALEPLUGIN_H
#include "../config/features.h"
#include "../core/Plugin.h"
#include "../core/constants.h"
#include "PostStopGracePolicy.h"

#if GAGGIMATE_ENABLE_BLE_SCALE

#include "remote_scales.h"
#include "remote_scales_plugin_registry.h"
#include <atomic>

void on_ble_measurement(float value);

constexpr unsigned long UPDATE_INTERVAL_MS = 1000;
constexpr unsigned int RECONNECTION_TRIES = 15;

// PRO-248: Hard-cap for the steam scale-alive grace window. NOTE: this window is
// NOT where the last drips are actually captured — that happens earlier, while
// the machine is still in MODE_BREW. On the normal auto-steam path DefaultUI::loop()
// holds `pendingAutoSteam` until ShotHistory.isExtendedRecording() is already
// false, and only THEN calls setMode(MODE_STEAM). The MODE_BREW scanning mode
// keeps the scale connected and the BLUETOOTH source latched, so the final drops
// land in the shot yield during that hold. By the time STEAM is entered and the
// grace window below is armed, the recording window is already closed.
//
// Given that, the grace window here behaves as a prompt-teardown safety net:
// loop() tears the scale down as soon as isExtendedRecording() reads false (which
// is normally immediate at STEAM entry) instead of waiting the full grace, and
// only falls back to this hard cap if some path enters STEAM with a recording
// window still open. Deriving from POST_STOP_GRACE_DURATION_MS keeps that cap in
// step with the extended-recording cap. All other transitions out of a scanning
// mode disconnect immediately (no grace).
constexpr unsigned long STEAM_SCALE_GRACE_PERIOD_MS = POST_STOP_GRACE_DURATION_MS;

class BLEScalePlugin : public Plugin {
  public:
    BLEScalePlugin();
    ~BLEScalePlugin();

    void setup(Controller *controller, PluginManager *pluginManager) override;
    void loop() override;
    ;

    void connect(const std::string &uuid);
    void scan() const;
    void disconnect();
    void onMeasurement(float value) const;
    bool isConnected() { return scale != nullptr && scale->isConnected(); };
    std::string getName() {
        if (scale != nullptr && scale->isConnected()) {
            return scale->getDeviceName();
        }
        return "";
    };
    std::string getUUID() {
        if (scale != nullptr && scale->isConnected()) {
            return scale->getDeviceAddress();
        }
        return "";
    };
    int getRSSI() {
        if (scale != nullptr && scale->isConnected()) {
            return scale->getRSSI();
        }
        return 0;
    };

    std::vector<DiscoveredDevice> getDiscoveredScales() const;
    void tare() const;
    float getLastWeight() const { return lastWeight; }

  private:
    void update();
    void onProcessStart() const;

    // PRO-351: cross-task teardown handshake. NimBLE notify callbacks dispatch on
    // the NimBLE host task while teardown runs on the event-handler / AsyncTCP
    // tasks. This flag is raised for the duration of the weight-updated callback
    // body and cleared on exit, giving teardown a real "no callback in flight"
    // observation instead of a blind delay(). waitForCallbacksToDrain() spins on
    // it with a hard timeout so teardown can never hang on a stuck callback.
    //
    // PRO-353: this flag fences ONLY the onMeasurement leg, not driver-frame
    // state a scale driver touches after RemoteScales::setWeight() returns in
    // the same notify frame. It narrows — does not close — the teardown UAF
    // window; the residual tail is backstopped by NimBLE's connection drain on
    // client deletion. See the callback lambda in establishConnection() for the
    // full rationale and the RAII guard that clears this on every exit path.
    void markCallbackInFlight(bool inFlight) { callbackInFlight.store(inFlight, std::memory_order_release); }
    void waitForCallbacksToDrain();

    // Teardown shared by the immediate mode-change disconnect path and the
    // steam grace-window expiry in loop(): stop processing, drop the scale
    // connection and halt async scanning. Call sites keep their own ESP_LOGI
    // context so the reason for the teardown stays distinct in the logs.
    void tearDownScale();

    void establishConnection();

    bool active = false;
    bool doConnect = false;
    std::string uuid;

    unsigned long lastUpdate = 0;
    unsigned int reconnectionTries = 0;

    // Previous controller mode, tracked so the mode-change handler (which only
    // receives the NEW mode) can detect a scanning-mode -> STEAM transition.
    int previousMode = MODE_STANDBY;
    // STEAM grace window: when set, a teardown of the scale is pending until
    // steamGraceDeadline (millis()) elapses, giving the scale time to catch the
    // final drips after a shot before disconnecting.
    bool steamDisconnectPending = false;
    unsigned long steamGraceDeadline = 0;

    // Rate limiting for callbacks
    mutable unsigned long lastMeasurementTime = 0;
    static constexpr unsigned long MIN_MEASUREMENT_INTERVAL_MS = 10; // Max 100 measurements per second
    mutable float lastWeight = 0.0f;

    Controller *controller = nullptr;
    RemoteScalesPluginRegistry *pluginRegistry = nullptr;
    RemoteScalesScanner *scanner = nullptr;
    std::unique_ptr<RemoteScales> scale = nullptr;

    // PRO-351: set true while a NimBLE weight-updated callback is executing on
    // the NimBLE host task; teardown waits for this to clear before freeing the
    // scale. atomic so the set (host task) and the read (teardown task) are a
    // well-defined cross-task happens-before, not a torn read.
    std::atomic<bool> callbackInFlight{false};
};

#else // GAGGIMATE_ENABLE_BLE_SCALE

#include <string>

// BLE scale compiled out (CAR-382). We deliberately avoid including the
// `esp-arduino-ble-scales` headers (remote_scales.h, scale drivers) so the
// PlatformIO Library Dependency Finder drops that whole library from the build
// — that is where the flash savings come from.
//
// A lightweight stub `BLEScalePlugin` keeps the public symbols that code OUTSIDE
// this plugin references (e.g. `BLEScales.tare()` in the SquareLine-generated
// `ui_events.cpp`, which must not be edited) linkable as no-ops. Methods whose
// signatures depend on RemoteScales types (`getDiscoveredScales()`,
// `connect()`, `scan()`) are intentionally omitted here; their only callers
// live in WebUIPlugin.cpp and are guarded with the same flag.
//
// Volumetric measurement is unaffected: it is fed through
// Controller::onVolumetricMeasurement() and its flow-estimation source
// (VolumetricMeasurementSource::FLOW_ESTIMATION, wired in Controller.cpp), which
// never reference this plugin. With the BLE scale gated out, the scale simply
// never pushes a BLUETOOTH-source measurement and the existing flow-estimation
// fallback / BLUETOOTH_GRACE_PERIOD_MS source-switching keeps working.

class BLEScalePlugin : public Plugin {
  public:
    void setup(Controller * /*controller*/, PluginManager * /*pluginManager*/) override {}
    void loop() override {}

    void disconnect() {}
    bool isConnected() { return false; }
    std::string getName() { return ""; }
    std::string getUUID() { return ""; }
    int getRSSI() { return 0; }
    void tare() const {}
    float getLastWeight() const { return 0.0f; }
};

#endif // GAGGIMATE_ENABLE_BLE_SCALE

extern BLEScalePlugin BLEScales;

#endif // BLESCALEPLUGIN_H
