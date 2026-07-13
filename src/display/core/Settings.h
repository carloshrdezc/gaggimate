#pragma once
#ifndef SETTINGS_H
#define SETTINGS_H

#include <Arduino.h>
#include <Preferences.h>
#include <display/core/constants.h>
#include <display/core/utils.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <utility>
#include <vector>

#define PREFERENCES_KEY "controller"

struct AutoWakeupSchedule {
    String time;    // HH:MM format
    bool days[7]{}; // [Mon, Tue, Wed, Thu, Fri, Sat, Sun]

    AutoWakeupSchedule() : time("07:00") {
        // Default to all days enabled
        for (int i = 0; i < 7; i++) {
            days[i] = true;
        }
    }

    explicit AutoWakeupSchedule(const String &timeStr) : time(timeStr) {
        // Default to all days enabled
        for (int i = 0; i < 7; i++) {
            days[i] = true;
        }
    }

    [[nodiscard]] bool isDayEnabled(const int dayOfWeek) const {
        // dayOfWeek: 1=Monday, 2=Tuesday, ..., 7=Sunday
        if (dayOfWeek < 1 || dayOfWeek > 7)
            return false;
        return days[dayOfWeek - 1];
    }

    void setDayEnabled(const int dayOfWeek, const bool enabled) {
        // dayOfWeek: 1=Monday, 2=Tuesday, ..., 7=Sunday
        if (dayOfWeek >= 1 && dayOfWeek <= 7) {
            days[dayOfWeek - 1] = enabled;
        }
    }
};

class Settings;
using SettingsCallback = std::function<void(Settings *)>;

class Settings {
  public:
    Settings();

    // Reads all persisted values from NVS into memory and starts the periodic
    // save task. MUST be called from setup() (after the Arduino core has run
    // nvs_flash_init()), NOT from the constructor: Controller (and therefore
    // this Settings member) is a global constructed during C++ static-init,
    // which runs before nvs_flash_init(). Reading NVS in the constructor races
    // that init and, on Arduino-esp32 3.x / IDF 5.x, fails with
    // "nvs_open failed: NOT_INITIALIZED" so every getX() returns its default
    // (PRO-331).
    void load();

    // Tears down what load() created: deletes the Settings::loop task (if
    // running) and resets taskHandle to nullptr, so a future call to load()
    // sees the PRO-492 guard as unset and can safely re-init. No re-init path
    // calls this yet (PRO-494) -- see the TODO at the call site in
    // Controller.cpp.
    void unload();

    void batchUpdate(const SettingsCallback &callback);
    void save(bool noDelay = false);

    // Getters and setters
    int getTargetSteamTemp() const { return targetSteamTemp; }
    int getTargetWaterTemp() const { return targetWaterTemp; }
    int getTemperatureOffset() const { return temperatureOffset; }
    float getPressureScaling() const { return pressureScaling; }
    double getTargetGrindVolume() const { return targetGrindVolume; }
    int getTargetGrindDuration() const { return targetGrindDuration; }
    int getStartupMode() const { return startupMode; }
    int getStandbyTimeout() const { return standbyTimeout; }
    double getBrewDelay() const { return brewDelay; }
    double getGrindDelay() const { return grindDelay; }
    bool isDelayAdjust() const { return delayAdjust; }
    // PRO-478: the String getters below are declared out-of-line (defined in
    // Settings.cpp) and copy the member under selectedNameMutex, the same shared
    // lock PRO-427 introduced for getSelectedBean()/getSelectedGrinder(). Each of
    // these members is written from the AsyncTCP/WebSocket-handler task while
    // other tasks (display/loop, deferred flush) copy-read them; an inline
    // by-value return would race a concurrent setter's buffer realloc (torn
    // String read).
    String getPid() const;
    String getPumpModelCoeffs() const;
    String getWifiSsid() const;
    String getWifiPassword() const;
    String getMdnsName() const;
    bool isHomekit() const { return homekit; }
    bool isVolumetricTarget() const { return volumetricTarget; }
    bool isAllowYieldOverride() const { return allowYieldOverride; }
    bool isAutoSteamEnabled() const { return autoSteamEnabled; }
    double getDoseGrams() const { return doseGrams; }
    String getOTAChannel() const;
    // PRO-400: the channel whose resolved head is believed to be flashed on the
    // device. Distinct from getOTAChannel() (the SELECTED channel) so a channel
    // switch can force-flash the new channel's head. Empty means "never stored"
    // (pre-migration device); Settings::load() backfills it to otaChannel.
    String getInstalledChannel() const;
    String getSavedScale() const;
    bool isBoilerFillActive() const { return boilerFillActive; }
    int getStartupFillTime() const { return startupFillTime; }
    int getSteamFillTime() const { return steamFillTime; }
    // PRO-266: when true, DiagnosticLogPlugin tees ESP_LOG output over UDP on the
    // LAN for tether-free serial capture. Default false → zero hot-path cost.
    bool getDiagnosticLogEnabled() const { return diagnosticLogEnabled; }
    bool isSmartGrindActive() const { return smartGrindActive; }
    int getSmartGrindMode() const { return smartGrindMode; }
    String getSmartGrindIp() const;
    bool isHomeAssistant() const { return homeAssistant; }
    String getHomeAssistantIP() const;
    String getHomeAssistantUser() const;
    String getHomeAssistantPassword() const;
    int getHomeAssistantPort() const { return homeAssistantPort; }
    String getHomeAssistantTopic() const;
    bool isMomentaryButtons() const { return momentaryButtons; }
    String getTimezone() const;
    bool isClock24hFormat() const { return clock24hFormat; }
    String getSelectedProfile() const;
    // PRO-427: getSelectedBean/getSelectedGrinder copy the String member under
    // selectedNameMutex (see Settings.cpp). Declared out-of-line so the guarded
    // copy is not exposed as inline; the read task and the WebSocket-handler task
    // that mutates these members must serialize to avoid a torn String read.
    String getSelectedBean() const;
    String getSelectedGrinder() const;
    // PRO-481: declared out-of-line (defined in Settings.cpp) and copy the
    // vector under vectorMutex, mirroring the String getters guarded by
    // selectedNameMutex above. favoritedProfiles/profileOrder are written by
    // the WS-handler task's setters below and read here plus imploded in
    // doSave() on the deferred flush task; an inline by-value return would
    // race a concurrent setter's vector mutation (torn iterator/reallocation).
    std::vector<String> getFavoritedProfiles() const;
    std::vector<String> getProfileOrder() const;
    int getMainBrightness() const { return mainBrightness; }
    int getStandbyBrightness() const { return standbyBrightness; }
    int getStandbyBrightnessTimeout() const { return standbyBrightnessTimeout; }
    int getWifiApTimeout() const { return wifiApTimeout; }
    float getSteamPumpPercentage() const { return steamPumpPercentage; }
    float getSteamPumpCutoff() const { return steamPumpCutoff; }
    int getThemeMode() const { return themeMode; }
    int getHistoryIndex() const { return historyIndex; }
    int getSunriseR() const { return sunriseR; }
    int getSunriseG() const { return sunriseG; }
    int getSunriseB() const { return sunriseB; }
    int getSunriseW() const { return sunriseW; }
    int getSunriseExtBrightness() const { return sunriseExtBrightness; }
    int getEmptyTankDistance() const { return emptyTankDistance; }
    int getFullTankDistance() const { return fullTankDistance; }
    int getAltRelayFunction() const { return altRelayFunction; }
    bool isAutoWakeupEnabled() const { return autowakeupEnabled; }
    std::vector<AutoWakeupSchedule> getAutoWakeupSchedules() const { return autowakeupSchedules; }
    int getFlushDuration() const { return flushDuration; }
    String getCloudRelayUrl() const;
    String getCloudRelayToken() const;
    bool isCloudRelayEnabled() const { return cloudRelayEnabled; }
    int getManualTargetType() const { return manualTargetType; }
    float getManualPressure() const { return manualPressure; }
    float getManualFlow() const { return manualFlow; }
    int getManualTemperature() const { return manualTemperature; }
    void setFlushDuration(int flush_duration);
    void setCloudRelayUrl(const String &url);
    void setCloudRelayToken(const String &token);
    void setCloudRelayEnabled(bool enabled);
    void setManualTargetType(int target_type);
    void setManualPressure(float pressure);
    void setManualFlow(float flow);
    void setManualTemperature(int temperature);
    void setTargetSteamTemp(int target_steam_temp);
    void setTargetWaterTemp(int target_water_temp);
    void setTemperatureOffset(int temperature_offset);
    void setPressureScaling(float pressure_scaling);
    void setTargetGrindVolume(double target_grind_volume);
    void setTargetGrindDuration(int target_duration);
    void setStartupMode(int startup_mode);
    void setStandbyTimeout(int standby_timeout);
    void setBrewDelay(double brewDelay);
    void setGrindDelay(double grindDelay);
    void setDelayAdjust(bool delay_adjust);
    void setPid(const String &pid);
    void setPumpModelCoeffs(const String &pumpModelCoeffs);
    void setWifiSsid(const String &wifiSsid);
    void setWifiPassword(const String &wifiPassword);
    void setMdnsName(const String &mdnsName);
    void setHomekit(bool homekit);
    void setVolumetricTarget(bool volumetric_target);
    void setAllowYieldOverride(bool allow_yield_override);
    void setAutoSteamEnabled(bool auto_steam_enabled);
    void setDoseGrams(double dose_grams);
    void setOTAChannel(const String &otaChannel);
    void setInstalledChannel(const String &installedChannel);
    void setSavedScale(const String &savedScale);
    void setBoilerFillActive(bool boiler_fill_active);
    void setStartupFillTime(int startup_fill_time);
    void setSteamFillTime(int steam_fill_time);
    void setSmartGrindActive(bool smart_grind_active);
    void setDiagnosticLogEnabled(bool diagnostic_log_enabled);
    void setSmartGrindIp(String smart_grind_ip);
    void setSmartGrindMode(int smart_grind_mode);
    void setHomeAssistant(bool homeAssistant);
    void setHomeAssistantUser(const String &homeAssistantUser);
    void setHomeAssistantPassword(const String &homeAssistantPassword);
    void setHomeAssistantIP(const String &homeAssistantIP);
    void setHomeAssistantPort(int homeAssistantPort);
    void setHomeAssistantTopic(const String &homeAssistantTopic);
    void setMomentaryButtons(bool momentary_buttons);
    void setTimezone(String timezone);
    void setClockFormat(bool format_24h);
    void setSelectedProfile(String selected_profile);
    void setSelectedBean(String selected_bean);
    void setSelectedGrinder(String selected_grinder);
    void setFavoritedProfiles(std::vector<String> favorited_profiles);
    void addFavoritedProfile(String profile);
    void removeFavoritedProfile(String profile);
    void setProfileOrder(std::vector<String> profile_order);
    void migrateProfileIds(const std::vector<std::pair<String, String>> &migrations);
    void setMainBrightness(int main_brightness);
    void setStandbyBrightness(int standby_brightness);
    void setStandbyBrightnessTimeout(int standby_brightness_timeout);
    void setWifiApTimeout(int timeout);
    void setSteamPumpPercentage(float steam_pump_percentage);
    void setSteamPumpCutoff(float steam_pump_cutoff);
    void setThemeMode(int theme_mode);
    void setHistoryIndex(int history_index);
    void setSunriseR(int sunrise_r);
    void setSunriseG(int sunrise_g);
    void setSunriseB(int sunrise_b);
    void setSunriseW(int sunrise_w);
    void setSunriseExtBrightness(int sunrise_ext_brightness);
    void setEmptyTankDistance(int empty_tank_distance);
    void setFullTankDistance(int full_tank_distance);
    void setAltRelayFunction(int alt_relay_function);
    void setAutoWakeupEnabled(bool enabled);
    void setAutoWakeupSchedules(const std::vector<AutoWakeupSchedule> &schedules);

  private:
    Preferences preferences;
    bool dirty = false;

    String selectedProfile;
    int targetSteamTemp = 155;
    int targetWaterTemp = 80;
    int temperatureOffset = DEFAULT_TEMPERATURE_OFFSET;
    float pressureScaling = DEFAULT_PRESSURE_SCALING;
    double targetGrindVolume = 18;
    int targetGrindDuration = 25000;
    double brewDelay = 1000.0;
    double grindDelay = 1000.0;
    bool delayAdjust = true;
    int startupMode = MODE_STANDBY;
    bool autowakeupEnabled = false;
    std::vector<AutoWakeupSchedule> autowakeupSchedules;
    int standbyTimeout = DEFAULT_STANDBY_TIMEOUT_MS;
    String pid = DEFAULT_PID;
    String pumpModelCoeffs = DEFAULT_PUMP_MODEL_COEFFS;
    String wifiSsid = "";
    String wifiPassword = "";
    String mdnsName = DEFAULT_MDNS_NAME;
    String savedScale = "";
    bool homekit = false;
    bool volumetricTarget = false;
    bool allowYieldOverride = false;
    bool autoSteamEnabled = false;
    double doseGrams = 18.0;
    bool boilerFillActive = false;
    int startupFillTime = 0;
    int steamFillTime = 0;
    bool smartGrindActive = false;
    bool diagnosticLogEnabled = false;
    bool smartGrindToggle = false;
    int smartGrindMode = 0;
    String smartGrindIp = "";
    bool homeAssistant = false;
    String homeAssistantUser = "";
    String homeAssistantPassword = "";
    String homeAssistantIP = "";
    int homeAssistantPort = 1883;
    String homeAssistantTopic = DEFAULT_HOME_ASSISTANT_TOPIC;
    bool momentaryButtons = false;
    String timezone = DEFAULT_TIMEZONE;
    bool clock24hFormat = true;
    String otaChannel = DEFAULT_OTA_CHANNEL;
    // PRO-400: channel whose head is installed. Defaults to DEFAULT_OTA_CHANNEL
    // in memory, but is loaded from NVS key "ic" with an EMPTY default so
    // "never stored" is detectable and backfilled once in load().
    String installedChannel = DEFAULT_OTA_CHANNEL;
    String selectedBean;
    String selectedGrinder;
    // PRO-427/PRO-478: guards cross-task access to every String member that is
    // (a) written by a setter on the AsyncTCP/WebSocket-handler task and (b) read
    // elsewhere (getters, or doSave() on the deferred flush task). A concurrent
    // set reallocates the String buffer under a copy-read on a dual-core ESP32
    // (torn read) without this guard. One shared mutex serializes all such
    // accessors — kept as one lock (not per-member) to keep the flash footprint
    // down; contention is negligible since sets/saves are infrequent relative to
    // the mutex hold time. mutable so the const getters can lock it; lazily
    // created on first use (robust to static-init order); a null handle degrades
    // to lock-free.
    mutable SemaphoreHandle_t selectedNameMutex = nullptr;
    // PRO-481: separate mutex guarding favoritedProfiles/profileOrder vector
    // mutations. NOT reused as selectedNameMutex: doSave() already holds
    // selectedNameMutex across its String snapshot block and additionally
    // calls implode(favoritedProfiles/profileOrder) inside that same
    // critical section (see doSave() below) — nesting a second take of
    // selectedNameMutex there would self-deadlock (FreeRTOS mutexes here are
    // not created recursive). A dedicated vectorMutex, taken only around the
    // vector snapshot/mutation, avoids that path while still serializing
    // every vector accessor against doSave()'s implode() read. mutable so
    // the const getters can lock it. PRO-486: created eagerly in load(),
    // before the loop task (the first concurrent caller) starts -- not
    // lazily on first use; a null handle (creation failed) degrades to
    // lock-free, same as before.
    mutable SemaphoreHandle_t vectorMutex = nullptr;
    std::vector<String> favoritedProfiles;
    std::vector<String> profileOrder; // persisted profile ordering
    float steamPumpPercentage = DEFAULT_STEAM_PUMP_PERCENTAGE;
    float steamPumpCutoff = DEFAULT_STEAM_PUMP_CUTOFF;
    int historyIndex = 0;
    int flushDuration = 5000;
    int manualTargetType = DEFAULT_MANUAL_TARGET_TYPE;
    float manualPressure = DEFAULT_MANUAL_PRESSURE;
    float manualFlow = DEFAULT_MANUAL_FLOW;
    int manualTemperature = DEFAULT_MANUAL_TEMPERATURE;

    // Display settings
    int mainBrightness = 16;
    int standbyBrightness = 8;
    int standbyBrightnessTimeout = 60000; // 60 seconds default
    int wifiApTimeout = DEFAULT_WIFI_AP_TIMEOUT_MS;
    int themeMode = 0;

    // Sunrise settings
    int sunriseR = 0;
    int sunriseG = 0;
    int sunriseB = 255;
    int sunriseW = 50;
    int sunriseExtBrightness = 255;
    int emptyTankDistance = 200;
    int fullTankDistance = 50;
    int altRelayFunction = ALT_RELAY_NONE; // Disabled unless explicitly configured
    bool altRelayConfigured = false;
    String cloudRelayUrl = "";
    String cloudRelayToken = "";
    bool cloudRelayEnabled = false;

    void doSave();
    // PRO-427: lazily create selectedNameMutex on first use and return it (may be
    // null if creation fails, in which case callers degrade to lock-free access).
    // const because the const selected-name getters need to lock; the handle is
    // mutable. Not itself thread-safe against a truly simultaneous first call from
    // two tasks, but the first accesses happen during single-threaded setup.
    SemaphoreHandle_t ensureSelectedNameMutex() const;
    // PRO-427/PRO-478: shared take/copy/give and take/assign/give helpers so every
    // guarded String accessor doesn't each open-code the mutex machinery (keeps the
    // guard's flash footprint down; a null handle degrades to lock-free). noexcept:
    // Arduino String uses a no-throw allocation model (a failed grow invalidates the
    // String and yields empty, it never throws), so these can't propagate — marking
    // them lets the compiler drop the String-copy exception unwind tables, keeping the
    // guard's flash footprint minimal.
    String copyUnderSelectedNameLock(const String &member) const noexcept;
    void assignUnderSelectedNameLock(String &member, String &&value) noexcept;
    // PRO-486: vectorMutex is created eagerly in Settings::load(); this just
    // returns the handle (may be null if creation there failed, in which case
    // callers degrade to lock-free access, same as before PRO-486).
    SemaphoreHandle_t ensureVectorMutex() const;
    // PRO-481: shared take/copy/give and take/assign/give helpers for the
    // vector<String> members, mirroring copyUnderSelectedNameLock /
    // assignUnderSelectedNameLock so the guard logic isn't duplicated per
    // accessor (keeps flash footprint down). A null handle degrades to a
    // lock-free copy/assign.
    std::vector<String> copyUnderVectorLock(const std::vector<String> &member) const noexcept;
    void assignUnderVectorLock(std::vector<String> &member, std::vector<String> &&value) noexcept;
    xTaskHandle taskHandle = nullptr;
    [[noreturn]] static void loopTask(void *arg);
};

#endif // SETTINGS_H
