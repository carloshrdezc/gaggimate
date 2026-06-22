#ifndef BLESCALEPLUGIN_H
#define BLESCALEPLUGIN_H
#include "../core/Plugin.h"
#include "../core/constants.h"
#include "remote_scales.h"
#include "remote_scales_plugin_registry.h"

void on_ble_measurement(float value);

constexpr unsigned long UPDATE_INTERVAL_MS = 1000;
constexpr unsigned int RECONNECTION_TRIES = 15;

// When the machine transitions from a scanning mode (brew/grind/manual) directly
// into STEAM, keep the BLE scale connected and reporting for this long before
// tearing it down. This grace window lets the scale capture the last drops that
// fall after the shot ends instead of disconnecting the instant steam starts.
// All other transitions out of a scanning mode disconnect immediately (no grace).
constexpr unsigned long STEAM_SCALE_GRACE_PERIOD_MS = 5000;

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
};

extern BLEScalePlugin BLEScales;

#endif // BLESCALEPLUGIN_H
