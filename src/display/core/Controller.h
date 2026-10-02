#ifndef CONTROLLER_H
#define CONTROLLER_H

#include "ControllerLinkPolicy.h"
#include "GaggiMateClient.h"
#include "PluginManager.h"
#include "Settings.h"
#include "SystemInfo.h"
#include "VolumetricCoalescer.h"
#include "VolumetricMeasurementSource.h"
#include <WiFi.h>
#include <atomic>
#include <display/core/BeanManager.h>
#include <display/core/GrinderManager.h>
#include <display/core/ProfileManager.h>
#include <display/core/process/Process.h>
#include <freertos/semphr.h>

// Thread-safe snapshot of process state for UI/plugins
struct ProcessSnapshot {
    bool exists = false;
    bool isActive = false;
    bool isComplete = false;
    int type = -1;
    unsigned long started = 0;
    unsigned long finished = 0;

    // Brew-specific fields
    bool isBrew = false;
    uint8_t phaseIndex = 0;
    String phaseName;
    int phaseType = 0; // PhaseType enum value
    unsigned long currentPhaseStarted = 0;
    float currentVolume = 0.0f;
    ProcessTarget target = ProcessTarget::TIME;
    bool hasVolumetricTarget = false;
    float volumetricTargetValue = 0.0f;
    unsigned long phaseDuration = 0;
    size_t phaseCount = 0;
    unsigned long totalDuration = 0;
    float brewVolume = 0.0f;
    bool isAdvancedPump = false;
    float pumpPressure = 0.0f;

    // Grind-specific fields
    bool isGrind = false;
    float grindVolume = 0.0f;
    unsigned long grindTime = 0;

    // Manual-specific fields
    bool isManual = false;
    int manualTargetType = DEFAULT_MANUAL_TARGET_TYPE;
    float manualPressure = 0.0f;
    float manualFlow = 0.0f;
    int manualTemperature = DEFAULT_MANUAL_TEMPERATURE;
};

struct EffectiveBrewTemperatureOverride {
    float temperature = 0.0f;
    bool enabled = false;
};
#ifndef GAGGIMATE_HEADLESS
#include <display/drivers/Driver.h>
#include <display/ui/default/DefaultUI.h>
#endif

const IPAddress WIFI_AP_IP(4, 4, 4, 1); // the IP address the web server, Samsung requires the IP to be in public space
const IPAddress WIFI_SUBNET_MASK(255, 255, 255, 0); // no need to change: https://avinetworks.com/glossary/subnet-mask/

// Mutex timeout for UI/event loop methods to prevent deadlocks
// Chosen to prevent UI freezes while allowing reasonable wait for mutex
constexpr TickType_t UI_MUTEX_TIMEOUT_MS = 100;

// PRO-608: Controller is a single non-polymorphic global. It has no base and no subclass, is
// never deleted through a base pointer, and is never destroyed at all on the device (it lives for
// the lifetime of the process). Adding a virtual destructor would put a vtable on the largest
// object in the firmware for zero benefit. Tracked in PRO-612.
// NOLINTNEXTLINE(cppcoreguidelines-virtual-class-destructor)
class Controller {
  public:
    Controller() = default;

    void setup();
    void connect();
    void loop();
    void loopControl();

    // PRO-670: returns false when refused. The gate runs before side effects, so
    // callers may safely ignore the result; leaving standby is refused while
    // control is inhibited (controller_link::modeChangeAllowed), entering standby never is.
    bool setMode(int newMode);
    // PRO-674: setMode() with an explicit mismatchForcedStandby update (see
    // controller_link::MismatchFlagUpdate). Internal link-state callers only.
    bool setMode(int newMode, controller_link::MismatchFlagUpdate flagUpdate, bool mismatch = false);
    void setTargetTemp(float temperature);
    bool setBrewTemperatureOverride(float temperature);
    void setPressureScale();
    void setPumpModelCoeffs();
    void setTargetGrindDuration(int duration);
    void setTargetGrindVolume(double volume);
    void updateManualTargets(int targetType, float pressure, float flow, int temperature);

    int getMode() const;

    float getTargetTemp() const;
    float getBrewTemperatureOverrideTarget() const;
    bool isBrewTemperatureOverrideEnabled() const;
    EffectiveBrewTemperatureOverride getEffectiveBrewTemperatureOverride() const;
    int getTargetGrindDuration() const;
    int getManualTargetType() const { return settings.getManualTargetType(); }
    float getManualPressure() const { return settings.getManualPressure(); }
    float getManualFlow() const { return settings.getManualFlow(); }
    int getManualTemperature() const { return settings.getManualTemperature(); }
    virtual float getCurrentTemp() const { return currentTemp; }
    bool isActive() const;
    bool isActiveSafe() const;
    bool canRestartDisplay() const;
    bool restartDisplayIfSafe();
    bool isGrindActive() const;
    bool isGrindAvailable() const;
    bool isManualAvailable() const;
    bool isUpdating() const;
    bool isAutotuning() const;
    bool isReady() const;
    bool isVolumetricAvailable() const;
    // True when the active shot's volumetric source is still delivering usable
    // measurements (CAR-367 duration-cap suppression gate). See definition.
    bool isActiveVolumetricSourceLive() const;
    bool isSDCard() const { return sdcard; }
    virtual float getTargetPressure() const;
    virtual float getTargetFlow() const;
    // True when a target pressure/flow is actually applicable for the current
    // mode. In standby this is false for simple-pump profiles, empty profiles,
    // and "hold current value" (-1) phases — letting the API send null rather
    // than a misleading 0. Always true in active modes (the member field holds
    // the live target).
    bool hasTargetPressure() const;
    bool hasTargetFlow() const;
    virtual float getCurrentPressure() const { return pressure; }
    virtual float getCurrentPuckFlow() const { return currentPuckFlow; }
    virtual float getCurrentPumpFlow() const { return currentPumpFlow; }

    void autotune(int testTime, int samples);
    void startProcess(Process *process);

    // DEPRECATED: Direct pointer access is unsafe due to race conditions.
    // Use getProcessSnapshot() or other thread-safe accessor methods instead.
    // This method will be removed in a future version.
    [[deprecated("Use getProcessSnapshot() or thread-safe accessor methods instead")]]
    Process *getProcess() const {
        return currentProcess;
    }

    // DEPRECATED: Direct pointer access is unsafe due to race conditions.
    // Use getProcessSnapshot() or other thread-safe accessor methods instead.
    // This method will be removed in a future version.
    [[deprecated("Use getProcessSnapshot() or thread-safe accessor methods instead")]]
    Process *getLastProcess() const {
        return lastProcess;
    }

    // Thread-safe methods to get process info without exposing raw pointer
    int getProcessType() const;
    uint8_t getBrewProcessPhaseIndex() const;
    bool isBrewProcessVolumetric() const;
    bool isBrewProcessUtility() const;

    // Thread-safe snapshot of current process state
    ProcessSnapshot getProcessSnapshot() const;
    Settings &getSettings() { return settings; }
    BeanManager *getBeanManager() { return beanManager; }
    GrinderManager *getGrinderManager() { return grinderManager; }
    ProfileManager *getProfileManager() { return profileManager; }
#ifndef GAGGIMATE_HEADLESS
    DefaultUI *getUI() const { return ui; }
#endif
    bool isErrorState() const { return error > 0; }
    int getError() const { return error; }

    // Event callback methods
    void updateLastAction();
    void raiseTemp();
    void lowerTemp();
    void raiseBrewTarget();
    void lowerBrewTarget();
    void setBrewTarget(float value);
    void raiseGrindTarget();
    void lowerGrindTarget();
    // PRO-655 B-P2-2: return false when refused (control inhibited: no verified
    // controller link / protocol mismatch, see isControlAllowed()).
    bool activate();
    void deactivate();
    void clear();
    bool activateGrind();
    void deactivateGrind();
    void activateStandby();
    bool deactivateStandby();
    void onOTAUpdate();
    void onScreenReady();
    void onTargetToggle();
    void onTargetChange(ProcessTarget target);
    void onProfileSave() const;
    void onProfileSaveAsNew();
    void onVolumetricMeasurement(double measurement, VolumetricMeasurementSource source);
    void setVolumetricOverride(bool override) { volumetricOverride.store(override, std::memory_order_release); }
    bool isBluetoothScaleHealthy() const;
    bool onFlush(); // false = refused (B-P2-2)
    int getWaterLevel() const {
        float reversedLevel = static_cast<float>(settings.getEmptyTankDistance()) -
                              static_cast<float>(std::min(settings.getEmptyTankDistance(), tofDistance));
        return static_cast<int>((reversedLevel - settings.getFullTankDistance()) /
                                static_cast<float>(settings.getEmptyTankDistance() - settings.getFullTankDistance()) * 100.0f);
    };

    void onVolumetricDelete();
    bool isLowWaterLevel() const { return getWaterLevel() < 20; };

    // PRO-674: copied under modeMutex; the Strings are rewritten on the BLE dispatch task.
    SystemInfo getSystemInfo() const;

    // PRO-655: the display-side NanoPbComm facade (was NimBLEClientController).
    GaggiMateClient *getClientController() { return &comms; }
    // PRO-655 R3/R4b: true while the connected controller speaks a different (or no)
    // framed protocol version: control inhibited, controller OTA only.
    //
    // B-P3-3: this flag is deliberately NOT cleared on disconnect (DefaultUI clears its
    // own display copy on CONTROLLER_BLUETOOTH_DISCONNECT). That is fail-safe: control
    // is gated by isControlAllowed(), which also requires connected + a SystemInfo
    // from THIS link (systemInfoReceived is cleared on disconnect), so a stale `true`
    // can only inhibit, never permit. The next SystemInfo overwrites it. Do not
    // "fix" this by clearing it on disconnect and gating on the flag alone.
    bool isProtocolMismatch() const { return systemInfo.protocolMismatch; }
    // PRO-655: connected && SystemInfo received on this link && protocol matches.
    // The single gate for every display -> controller control/actuation frame.
    bool isControlAllowed() const;
    // B-P2-4: the only path to the controller's Tare frame; gated like all control.
    // Returns false (and sends nothing) when control is inhibited.
    bool tareControllerScale();

  private:
    // Initialization methods
#ifndef GAGGIMATE_HEADLESS
    void setupPanel();
#endif
    void setupBluetooth();
    void onSystemInfo(const char *hardware, const char *version, uint32_t protocolVersion, bool dimming, bool pressure,
                      bool ledControl, bool tof, bool dualBoiler, const std::vector<uint32_t> &addons);
    void onIncompatibleController(const String &info);
    void setPidSettings();
    void setupWifi();

    // Functional methods
    void updateControl();
    // Whether the active process drives a real pump pressure/flow target
    // (advanced-pump brew / manual / steam). False for simple-pump brew, water,
    // grind, and when inactive. Non-standby helper for hasTargetPressure/Flow.
    bool hasPumpTarget() const;

    // Event handlers
    void onTempRead(float temperature);

    // brew button
    void handleBrewButton(int brewButtonStatus);

    // steam button
    void handleSteamButton(int steamButtonStatus);
    // PRO-391: previous steam-button level seen by handleSteamButton(). Used to
    // detect the rising edge of a non-momentary (latching) switch so a sustained
    // latched-high level does not re-assert MODE_STEAM after an explicit Standby.
    // 0 = not pressed (initial state).
    int previousSteamButtonStatus = 0;
    void handleProfileUpdate();

    // Private Attributes
#ifndef GAGGIMATE_HEADLESS
    DefaultUI *ui = nullptr;
    Driver *driver = nullptr;
#endif
    GaggiMateClient comms;
    Settings settings;
    PluginManager *pluginManager{};
    BeanManager *beanManager{};
    GrinderManager *grinderManager{};
    ProfileManager *profileManager{};

    // PRO-674: read lock-free from every task (atomic, never torn); every WRITE goes
    // through setMode() under modeMutex, so a read-decide-write sequence that holds
    // modeMutex (onSystemInfo, link drop) cannot interleave with another task's
    // setMode() (WebUI/relay on AsyncTCP, buttons, standby timeout).
    std::atomic<int> mode{MODE_BREW};
    // PRO-674: serializes mode transitions + mismatchForcedStandby + systemInfo.
    // RECURSIVE: setMode() fires CONTROLLER_MODE_CHANGE, whose handlers (and
    // activateStandby() inside an onSystemInfo decision) may re-enter setMode().
    // Lock order: modeMutex may be held while processMutex is taken, never the
    // reverse (no processMutex holder calls setMode()/getSystemInfo()).
    SemaphoreHandle_t modeMutex = nullptr;
    // setMode() body; caller holds modeMutex.
    bool setModeLocked(int newMode, controller_link::MismatchFlagUpdate flagUpdate, bool mismatch);
    float currentTemp = 0;
    float pressure = 0.0f;
    float targetPressure = 0.0f;
    float currentPuckFlow = 0.0f;
    float currentPumpFlow = 0.0f;
    float targetFlow = 0.0f;
    int tofDistance = 0;

    SystemInfo systemInfo{};
    // PRO-655: set once a SystemInfo arrived on the current link (cleared on disconnect).
    std::atomic<bool> systemInfoReceived{false};
    // PRO-671/673: the current STANDBY was forced by a protocol mismatch (set in
    // onSystemInfo). Every setMode() clears it (restore, user stop/wake, error,
    // timeout); only the disconnect standby in onConnectionChanged(false) preserves
    // it (PRO-674: MismatchFlagUpdate::Preserve under modeMutex, no write), so
    // mismatch -> link drop -> matching reconnect can still restore the
    // startup mode while an ordinary reconnect in STANDBY never does.
    std::atomic<bool> mismatchForcedStandby{false};
    // B-P3-2: last full control frame sent (loopControl task only) + a resend
    // request raised from the BLE/loop tasks on every new link / SystemInfo.
    BoilerCommand lastSentBoiler;
    PumpCommand lastSentPump;
    RelayCommand lastSentRelay;
    bool lastSentAlt = false;
    uint32_t lastControlSendMs = 0;
    std::atomic<bool> controlResendRequested{true};

    Process *currentProcess = nullptr;
    Process *lastProcess = nullptr;
    SemaphoreHandle_t processMutex = nullptr;

    unsigned long grindActiveUntil = 0;
    unsigned long lastPing = 0;
    unsigned long lastProgress = 0;
    unsigned long lastAction = 0;
    bool loaded = false;
    bool updating = false;
    bool autotuning = false;
    bool isApConnection = false;
    bool initialized = false;
    bool screenReady = false;
    bool waitingForController = false;
    unsigned long connectStartTime = 0;
    std::atomic<bool> volumetricOverride{false};
    bool processCompleted = false;
    bool steamReady = false;
    bool sdcard = false;
    int error = 0;

    // Bluetooth scale connection monitoring
    // PRO-375: written on the UI/control task, read on the BLE/measurement task
    // (onVolumetricMeasurement); atomic with acquire/release to synchronize the
    // cross-task accesses (see BLEScalePlugin.h callbackInFlight precedent).
    std::atomic<VolumetricMeasurementSource> currentVolumetricSource{VolumetricMeasurementSource::INACTIVE};
    std::atomic<unsigned long> lastBluetoothMeasurement{0};

    // PRO-367: coalesce-latest holder for the stop-critical volumetric update.
    // onVolumetricMeasurement() takes processMutex with a 10 ms fail-fast timeout;
    // under diagnostic-log contention that take can fail. Rather than DROP the
    // measurement (which stalls currentVolume and loses the yield stop), we latch
    // the freshest value here and apply it on the next successful take. The scale
    // weight is monotonic cumulative, so the newest value subsumes every earlier
    // one and applying it loses no yield. Only measurements whose source matches
    // the latched currentVolumetricSource ever reach here (source mismatch is
    // rejected earlier), so a single coalescer for the active source is correct.
    volumetric::Coalescer volumetricCoalescer{};
    static const unsigned long BLUETOOTH_GRACE_PERIOD_MS = 1500; // 1.5 second grace period
    static const unsigned long CONTROLLER_WAITING_TIMEOUT_MS = 10000;
    // PRO-655: keepalive ping cadence (upstream PING_INTERVAL); completes the
    // server handshake and feeds the controller watchdog between control frames.
    static const unsigned long PING_INTERVAL_MS = 2000;
    // B-P3-2: full control-state keepalive when nothing changed.
    static const uint32_t CONTROL_KEEPALIVE_MS = 1000;
    // PRO-655 (upstream v1.9): re-send the connect-time config burst for a short window.
    static const unsigned long CONFIG_RESEND_WINDOW_MS = 8000;
    static const unsigned long CONFIG_RESEND_INTERVAL_MS = 1000;
    unsigned long configResendUntil = 0;
    unsigned long lastConfigResend = 0;

    xTaskHandle taskHandle = nullptr;

    static void loopTask(void *arg);
};

#endif // CONTROLLER_H
