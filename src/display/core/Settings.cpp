#include "Settings.h"
#include <display/core/MdnsNamePolicy.h>
#include <display/core/SettingsPersistenceMutexInitialization.h>

#include <algorithm>
#include <utility>

namespace {
class ScopedRecursiveSemaphore {
  public:
    explicit ScopedRecursiveSemaphore(SemaphoreHandle_t semaphore) : semaphore(semaphore) {
        if (semaphore != nullptr) {
            xSemaphoreTakeRecursive(semaphore, portMAX_DELAY);
        }
    }
    ~ScopedRecursiveSemaphore() {
        if (semaphore != nullptr) {
            xSemaphoreGiveRecursive(semaphore);
        }
    }
    // PRO-608: scope-bound lock guard — copying or moving it would give the
    // recursive semaphore back twice (or from the wrong scope). Never stored,
    // never returned; deleting all four satisfies the rule-of-5.
    ScopedRecursiveSemaphore(const ScopedRecursiveSemaphore &) = delete;
    ScopedRecursiveSemaphore &operator=(const ScopedRecursiveSemaphore &) = delete;
    ScopedRecursiveSemaphore(ScopedRecursiveSemaphore &&) = delete;
    ScopedRecursiveSemaphore &operator=(ScopedRecursiveSemaphore &&) = delete;

  private:
    SemaphoreHandle_t semaphore;
};

String remapProfileId(const String &id, const std::vector<std::pair<String, String>> &migrations) {
    for (const auto &migration : migrations) {
        if (id == migration.first) {
            return migration.second;
        }
    }
    return id;
}

std::vector<String> remapProfileIds(std::vector<String> ids, const std::vector<std::pair<String, String>> &migrations) {
    for (auto &id : ids) {
        id = remapProfileId(id, migrations);
    }
    return ids;
}

std::vector<String> cleanProfileIds(std::vector<String> ids, const char *context) {
    std::vector<String> cleaned;
    cleaned.reserve(ids.size());

    for (auto &id : ids) {
        if (!isSafeId(id)) {
            ESP_LOGW("Settings", "Dropping unsafe persisted %s id: %s", context ? context : "profile", id.c_str());
            continue;
        }
        if (std::find(cleaned.begin(), cleaned.end(), id) == cleaned.end()) {
            cleaned.emplace_back(std::move(id));
        }
    }

    return cleaned;
}
} // namespace

Settings::Settings() = default;

void Settings::load() {
    // PRO-331: open the namespace READ-WRITE (readOnly=false), not read-only.
    // On Arduino-esp32 3.x / IDF 5.x a read-only begin() FAILS (returns false)
    // when the namespace does not exist yet, and a failed begin() makes every
    // getX() silently return the supplied default. Opening read-write creates
    // the namespace on first boot instead of failing, so subsequent reads see
    // persisted values. We also check the return value and log a failure rather
    // than swallowing it. load() runs from Controller::setup() (after the
    // Arduino core has initialized NVS), so this can no longer race nvs init.
    if (!preferences.begin(PREFERENCES_KEY, false)) {
        ESP_LOGE("Settings", "Failed to open NVS namespace '%s' for read; settings will use defaults this boot", PREFERENCES_KEY);
    }
    startupMode = preferences.getInt("sm", MODE_STANDBY);
    targetSteamTemp = preferences.getInt("ts", 145);
    targetWaterTemp = preferences.getInt("tw", 80);
    targetGrindVolume = preferences.getDouble("tgv", 18.0);
    targetGrindDuration = preferences.getInt("tgd", 25000);
    brewDelay = preferences.getDouble("del_br", 800.0);
    grindDelay = preferences.getDouble("del_gd", 1000.0);
    delayAdjust = preferences.getBool("del_ad", true);
    temperatureOffset = preferences.getInt("to", DEFAULT_TEMPERATURE_OFFSET);
    pressureScaling = preferences.getFloat("ps", DEFAULT_PRESSURE_SCALING);
    pid = preferences.getString("pid", DEFAULT_PID);
    pumpModelCoeffs = preferences.getString("pmc", DEFAULT_PUMP_MODEL_COEFFS);
    wifiSsid = preferences.getString("ws", "");
    wifiPassword = preferences.getString("wp", "");
    mdnsName = preferences.getString("mn", DEFAULT_MDNS_NAME);
    if (!isValidMdnsName(mdnsName.c_str(), mdnsName.length())) {
        ESP_LOGW("Settings", "Ignoring invalid persisted mDNS name");
        mdnsName = DEFAULT_MDNS_NAME;
        preferences.putString("mn", mdnsName);
    }
    homekit = preferences.getBool("hk", false);
    volumetricTarget = preferences.getBool("vt", false);
    allowYieldOverride = preferences.getBool("ayo", false);
    autoSteamEnabled = preferences.getBool("autosteam", false);
    standbyOnBrewEnabled = preferences.getBool("sbonbrew", false);
    doseGrams = preferences.getDouble("dosegrams", 18.0);
    manualGrindSetting = preferences.getDouble("mgrind", 0.0);
    otaChannel = preferences.getString("oc", DEFAULT_OTA_CHANNEL);
    // PRO-400: EMPTY default so a device that never stored "ic" is detectable.
    installedChannel = preferences.getString("ic", "");
    // One-time migration backfill: existing devices are assumed to have the
    // selected channel's head already installed (installed == selected), so a
    // normal upgrade sees NO forced re-flash. Written back read-write here
    // (load() opened NVS with begin(..., false)).
    if (installedChannel.isEmpty()) {
        installedChannel = otaChannel;
        preferences.putString("ic", installedChannel);
    }
    savedScale = preferences.getString("ssc", "");
    momentaryButtons = preferences.getBool("mb", false);
    boilerFillActive = preferences.getBool("bf_a", false);
    startupFillTime = preferences.getInt("bf_su", 5000);
    steamFillTime = preferences.getInt("bf_st", 5000);
    smartGrindActive = preferences.getBool("sg_a", false);
    diagnosticLogEnabled = preferences.getBool("diag_log", false);
    smartGrindIp = preferences.getString("sg_i", "");
    smartGrindToggle = preferences.getBool("sg_t", false);
    smartGrindMode = preferences.getInt("sg_m", smartGrindToggle ? 1 : 0);
    homeAssistant = preferences.getBool("ha_a", false);
    homeAssistantIP = preferences.getString("ha_i", "");
    homeAssistantPort = preferences.getInt("ha_p", 1883);
    homeAssistantTopic = preferences.getString("ha_t", DEFAULT_HOME_ASSISTANT_TOPIC);
    // Self-heal an oversized topic persisted before the clamp shipped, matching setHomeAssistantTopic().
    if (homeAssistantTopic.length() > MAX_HOME_ASSISTANT_TOPIC_LENGTH) {
        homeAssistantTopic = homeAssistantTopic.substring(0, MAX_HOME_ASSISTANT_TOPIC_LENGTH);
    }
    homeAssistantUser = preferences.getString("ha_u", "");
    homeAssistantPassword = preferences.getString("ha_pw", "");
    standbyTimeout = preferences.getInt("sbt", DEFAULT_STANDBY_TIMEOUT_MS);
    timezone = preferences.getString("tz", DEFAULT_TIMEZONE);
    clock24hFormat = preferences.getBool("clk_24h", true);
    selectedProfile = preferences.getString("sp", "");
    brewTemperatureOverrideEnabled = preferences.getBool("bto_en", false);
    brewTemperatureOverride = preferences.getFloat("bto", 0.0f);
    brewTemperatureOverrideProfile = preferences.getString("bto_p", "");
    if (!brewTemperatureOverrideEnabled || brewTemperatureOverrideProfile.isEmpty()) {
        brewTemperatureOverrideEnabled = false;
        brewTemperatureOverride = 0.0f;
        brewTemperatureOverrideProfile = "";
    }
    selectedBean = preferences.getString("sb", "");
    selectedGrinder = preferences.getString("sg", "");
    favoritedProfiles = cleanProfileIds(explode(preferences.getString("fp", ""), ','), "favoritedProfiles");
    profileOrder = cleanProfileIds(explode(preferences.getString("po", ""), ','), "profileOrder");
    steamPumpPercentage = preferences.getFloat("spp", DEFAULT_STEAM_PUMP_PERCENTAGE);
    steamPumpCutoff = preferences.getFloat("spc", DEFAULT_STEAM_PUMP_CUTOFF);
    historyIndex = preferences.getInt("hi", 0);
    flushDuration = preferences.getInt("fd", 5000);
    manualTargetType = preferences.getInt("mtt", DEFAULT_MANUAL_TARGET_TYPE);
    manualPressure = preferences.getFloat("mp", DEFAULT_MANUAL_PRESSURE);
    manualFlow = preferences.getFloat("mf", DEFAULT_MANUAL_FLOW);
    manualTemperature = preferences.getInt("mt", DEFAULT_MANUAL_TEMPERATURE);
    setManualTargetType(manualTargetType);
    setManualPressure(manualPressure);
    setManualFlow(manualFlow);
    setManualTemperature(manualTemperature);
    persistenceTransaction.clearDirty();
    autowakeupEnabled = preferences.getBool("ab_en", false);

    // Load schedule format: "time1|days1;time2|days2" where days is 7-bit string (e.g., "1111100" for weekdays only)
    String schedulesStr = preferences.getString("ab_schedules", "");
    autowakeupSchedules.clear();

    if (schedulesStr.length() > 0) {
        int start = 0;
        int end = schedulesStr.indexOf(';');

        while (end != -1 || start < schedulesStr.length()) {
            String scheduleStr = (end != -1) ? schedulesStr.substring(start, end) : schedulesStr.substring(start);

            int pipePos = scheduleStr.indexOf('|');
            if (pipePos != -1) {
                String timeStr = scheduleStr.substring(0, pipePos);
                String daysStr = scheduleStr.substring(pipePos + 1);

                AutoWakeupSchedule schedule;
                schedule.time = timeStr;

                if (daysStr.length() == 7) {
                    for (int i = 0; i < 7; i++) {
                        schedule.days[i] = (daysStr.charAt(i) == '1');
                    }
                }

                autowakeupSchedules.push_back(schedule);
            }

            if (end == -1)
                break;
            start = end + 1;
            end = schedulesStr.indexOf(';', start);
        }
    }

    if (autowakeupSchedules.empty()) {
        autowakeupSchedules.emplace_back(AutoWakeupSchedule("07:00"));
    }

    // Display settings
    mainBrightness = preferences.getInt("main_b", 16);
    standbyBrightness = preferences.getInt("standby_b", 8);
    standbyBrightnessTimeout = preferences.getInt("standby_bt", 60000);
    wifiApTimeout = preferences.getInt("wifi_apt", DEFAULT_WIFI_AP_TIMEOUT_MS);
    themeMode = preferences.getInt("theme", 0);

    // Sunrise settings
    sunriseR = preferences.getInt("sr_r", 0);
    sunriseG = preferences.getInt("sr_g", 0);
    sunriseB = preferences.getInt("sr_b", 255);
    sunriseW = preferences.getInt("sr_w", 50);
    sunriseExtBrightness = preferences.getInt("sr_exb", 255);
    emptyTankDistance = preferences.getInt("sr_ed", 200);
    fullTankDistance = preferences.getInt("sr_fd", 50);
    altRelayConfigured = preferences.getBool("alt_set", false);
    const bool hasAltRelaySetting = preferences.isKey("alt_relay");
    const int storedAltRelayFunction = preferences.getInt("alt_relay", ALT_RELAY_NONE);
    if (altRelayConfigured || (hasAltRelaySetting && storedAltRelayFunction != ALT_RELAY_GRIND)) {
        altRelayFunction = storedAltRelayFunction;
        altRelayConfigured = true;
    } else {
        altRelayFunction = ALT_RELAY_NONE;
    }
    cloudRelayUrl = preferences.getString("cr_url", "");
    cloudRelayToken = preferences.getString("cr_token", "");
    cloudRelayEnabled = preferences.getBool("cr_enabled", false);
    localAdminToken = preferences.getString("admin_token", "");
    localAuthProvisioned = preferences.getBool("admin_ready", false);

    preferences.end();

    // PRO-486: create vectorMutex here, eagerly, before the loop task (and
    // therefore any other task) can reach a vector accessor. Closes the
    // theoretical lazy-init double-create race in ensureVectorMutex() —
    // load() runs single-threaded, so there is no concurrent caller to race.
    // PRO-488: guard against double-init if load() is ever called twice
    // (e.g. in a future test harness). No-op when mutex already created.
    if (vectorMutex == nullptr) {
        vectorMutex = xSemaphoreCreateMutex();
    }

    // Create the persistence mutex before starting the flush task or exposing
    // Settings to other tasks. If allocation fails, ScopedRecursiveSemaphore
    // retains its existing null-handle degradation behavior.
    persistenceMutex = initializePersistenceMutex(persistenceMutex, [] { return xSemaphoreCreateRecursiveMutex(); });

    // PRO-492: guard against double-init if load() is ever called twice,
    // mirroring the vectorMutex guard above (PRO-488). Without this, a
    // second call would spawn a duplicate Settings::loop task and
    // overwrite taskHandle, leaking the original task.
    if (taskHandle == nullptr) {
        xTaskCreate(loopTask, "Settings::loop", configMINIMAL_STACK_SIZE * 6, this, 1, &taskHandle);
    }
}

// PRO-494: teardown counterpart to load(), so the PRO-492 double-init guard
// stays logically complete -- if a future re-init path ever clears taskHandle
// externally, it must go through here first, or the FreeRTOS task started by
// load() would leak as a zombie. Only tears down resources load() explicitly
// creates: the loop task, vectorMutex, and persistenceMutex.
// selectedNameMutex is intentionally left alone -- it is lazily created by
// ensureSelectedNameMutex() on first use, not by load(), so it is out of
// scope for this teardown.
void Settings::unload() {
    // Keep this in sync with Settings::load() — it must tear down every
    // resource load() creates (loop task via vTaskDelete and the eager mutexes
    // via vSemaphoreDelete). selectedNameMutex is intentionally excluded:
    // it is lazily created by ensureSelectedNameMutex(), not by load().
    // Flush any pending dirty write BEFORE killing the deferred-flush task,
    // otherwise a settings change made shortly before unload() would be
    // silently discarded when the loop task is deleted.
    // Note: the loop task may fire a concurrent doSave() in the brief window
    // between this check and vTaskDelete below. The ESP32 NVS Preferences
    // library serializes begin() internally, so the worst case is a redundant
    // write — not data corruption. This is a pre-existing design constraint,
    // not a regression of this teardown; see PRO-499 for context.
    if (persistenceTransaction.isDirty()) {
        doSave();
    }
    if (taskHandle != nullptr) {
        vTaskDelete(taskHandle);
        taskHandle = nullptr;
    }
    if (vectorMutex != nullptr) {
        vSemaphoreDelete(vectorMutex);
        vectorMutex = nullptr;
    }
    if (persistenceMutex != nullptr) {
        vSemaphoreDelete(persistenceMutex);
        persistenceMutex = nullptr;
    }
}

void Settings::batchUpdate(const SettingsCallback &callback) {
    bool saveImmediately = false;
    {
        ScopedRecursiveSemaphore lock(ensurePersistenceMutex());
        persistenceTransaction.beginBatch();
        callback(this);
        persistenceTransaction.endBatch();
        persistenceTransaction.markDirty();
        saveImmediately = persistenceTransaction.consumeImmediateSaveRequest();
    }
    if (saveImmediately) {
        doSave();
    }
}

void Settings::save(bool noDelay) {
    bool saveImmediately = false;
    {
        ScopedRecursiveSemaphore lock(ensurePersistenceMutex());
        persistenceTransaction.markDirty();
        if (noDelay) {
            persistenceTransaction.requestImmediateSave();
            saveImmediately = persistenceTransaction.consumeImmediateSaveRequest();
        }
    }
    if (saveImmediately) {
        doSave();
    }
}

SemaphoreHandle_t Settings::ensurePersistenceMutex() { return persistenceMutex; }

void Settings::setTargetSteamTemp(const int target_steam_temp) {
    ScopedRecursiveSemaphore lock(ensurePersistenceMutex());
    targetSteamTemp = target_steam_temp;
    save();
}

void Settings::setTargetWaterTemp(const int target_water_temp) {
    ScopedRecursiveSemaphore lock(ensurePersistenceMutex());
    targetWaterTemp = target_water_temp;
    save();
}

void Settings::setTemperatureOffset(const int temperature_offset) {
    ScopedRecursiveSemaphore lock(ensurePersistenceMutex());
    temperatureOffset = temperature_offset;
    save();
}

void Settings::setPressureScaling(const float pressure_scaling) {
    ScopedRecursiveSemaphore lock(ensurePersistenceMutex());
    pressureScaling = pressure_scaling;
    save();
}

void Settings::setTargetGrindVolume(double target_grind_volume) {
    ScopedRecursiveSemaphore lock(ensurePersistenceMutex());
    targetGrindVolume = target_grind_volume;
    save();
}

void Settings::setTargetGrindDuration(const int target_duration) {
    ScopedRecursiveSemaphore lock(ensurePersistenceMutex());
    targetGrindDuration = target_duration;
    save();
}

void Settings::setBrewDelay(double brew_Delay) {
    ScopedRecursiveSemaphore lock(ensurePersistenceMutex());
    brewDelay = std::clamp(brew_Delay, 0.0, 4000.0);
    save();
}

void Settings::setGrindDelay(double grind_Delay) {
    ScopedRecursiveSemaphore lock(ensurePersistenceMutex());
    grindDelay = std::clamp(grind_Delay, 0.0, 4000.0);
    save();
}

void Settings::setDelayAdjust(bool delay_adjust) {
    ScopedRecursiveSemaphore lock(ensurePersistenceMutex());
    delayAdjust = delay_adjust;
    save();
}

void Settings::setStartupMode(const int startup_mode) {
    ScopedRecursiveSemaphore lock(ensurePersistenceMutex());
    startupMode = startup_mode;
    save();
}

void Settings::setStandbyTimeout(int standby_timeout) {
    ScopedRecursiveSemaphore lock(ensurePersistenceMutex());
    standbyTimeout = standby_timeout;
    save();
}

void Settings::setPid(const String &pid) {
    ScopedRecursiveSemaphore lock(ensurePersistenceMutex());
    assignUnderSelectedNameLock(this->pid, String(pid));
    save();
}

void Settings::setPumpModelCoeffs(const String &pumpModelCoeffs) {
    ScopedRecursiveSemaphore lock(ensurePersistenceMutex());
    assignUnderSelectedNameLock(this->pumpModelCoeffs, String(pumpModelCoeffs));
    save();
}

void Settings::setWifiSsid(const String &wifiSsid) {
    ScopedRecursiveSemaphore lock(ensurePersistenceMutex());
    assignUnderSelectedNameLock(this->wifiSsid, String(wifiSsid));
    save();
}

void Settings::setWifiPassword(const String &wifiPassword) {
    ScopedRecursiveSemaphore lock(ensurePersistenceMutex());
    assignUnderSelectedNameLock(this->wifiPassword, String(wifiPassword));
    save();
}

void Settings::setMdnsName(const String &mdnsName) {
    ScopedRecursiveSemaphore lock(ensurePersistenceMutex());
    if (!isValidMdnsName(mdnsName.c_str(), mdnsName.length())) {
        ESP_LOGW("Settings", "Rejecting invalid mDNS name");
        return;
    }
    assignUnderSelectedNameLock(this->mdnsName, String(mdnsName));
    save();
}

void Settings::setHomekit(const bool homekit) {
    ScopedRecursiveSemaphore lock(ensurePersistenceMutex());
    this->homekit = homekit;
    save();
}

void Settings::setVolumetricTarget(bool volumetric_target) {
    ScopedRecursiveSemaphore lock(ensurePersistenceMutex());
    this->volumetricTarget = volumetric_target;
    save();
}

void Settings::setAllowYieldOverride(bool allow_yield_override) {
    ScopedRecursiveSemaphore lock(ensurePersistenceMutex());
    this->allowYieldOverride = allow_yield_override;
    save();
}

void Settings::setAutoSteamEnabled(bool auto_steam_enabled) {
    ScopedRecursiveSemaphore lock(ensurePersistenceMutex());
    this->autoSteamEnabled = auto_steam_enabled;
    save();
}

void Settings::setStandbyOnBrewEnabled(bool standby_on_brew_enabled) {
    ScopedRecursiveSemaphore lock(ensurePersistenceMutex());
    this->standbyOnBrewEnabled = standby_on_brew_enabled;
    save();
}

void Settings::setDoseGrams(double dose_grams) {
    ScopedRecursiveSemaphore lock(ensurePersistenceMutex());
    this->doseGrams = std::clamp(dose_grams, 0.1, 200.0);
    save();
}

void Settings::setManualGrindSetting(double manual_grind_setting) {
    ScopedRecursiveSemaphore lock(ensurePersistenceMutex());
    // PRO-603: clamp to the Dashboard's manual-grind range [0, 100] (0 = "not
    // set"). Distinct from doseGrams' [0.1, 200] since 0 is a valid grind value.
    this->manualGrindSetting = std::clamp(manual_grind_setting, 0.0, 100.0);
    save();
}

void Settings::setOTAChannel(const String &otaChannel) {
    ScopedRecursiveSemaphore lock(ensurePersistenceMutex());
    assignUnderSelectedNameLock(this->otaChannel, String(otaChannel));
    save();
}

void Settings::setInstalledChannel(const String &installedChannel) {
    ScopedRecursiveSemaphore lock(ensurePersistenceMutex());
    assignUnderSelectedNameLock(this->installedChannel, String(installedChannel));
    save();
}

void Settings::setSavedScale(const String &savedScale) {
    ScopedRecursiveSemaphore lock(ensurePersistenceMutex());
    assignUnderSelectedNameLock(this->savedScale, String(savedScale));
    save();
}

void Settings::setBoilerFillActive(bool boiler_fill_active) {
    ScopedRecursiveSemaphore lock(ensurePersistenceMutex());
    boilerFillActive = boiler_fill_active;
    save();
}

void Settings::setStartupFillTime(int startup_fill_time) {
    ScopedRecursiveSemaphore lock(ensurePersistenceMutex());
    startupFillTime = startup_fill_time;
    save();
}

void Settings::setSteamFillTime(int steam_fill_time) {
    ScopedRecursiveSemaphore lock(ensurePersistenceMutex());
    steamFillTime = steam_fill_time;
    save();
}

void Settings::setSmartGrindActive(bool smart_grind_active) {
    ScopedRecursiveSemaphore lock(ensurePersistenceMutex());
    smartGrindActive = smart_grind_active;
    save();
}

void Settings::setDiagnosticLogEnabled(bool diagnostic_log_enabled) {
    ScopedRecursiveSemaphore lock(ensurePersistenceMutex());
    diagnosticLogEnabled = diagnostic_log_enabled;
    save();
}

void Settings::setSmartGrindIp(String smart_grind_ip) {
    ScopedRecursiveSemaphore lock(ensurePersistenceMutex());
    assignUnderSelectedNameLock(this->smartGrindIp, std::move(smart_grind_ip));
    save();
}

void Settings::setSmartGrindMode(int smart_grind_mode) {
    ScopedRecursiveSemaphore lock(ensurePersistenceMutex());
    this->smartGrindMode = smart_grind_mode;
    save();
}

void Settings::setHomeAssistant(const bool homeAssistant) {
    ScopedRecursiveSemaphore lock(ensurePersistenceMutex());
    this->homeAssistant = homeAssistant;
    save();
}

void Settings::setHomeAssistantIP(const String &homeAssistantIP) {
    ScopedRecursiveSemaphore lock(ensurePersistenceMutex());
    assignUnderSelectedNameLock(this->homeAssistantIP, String(homeAssistantIP));
    save();
}

void Settings::setHomeAssistantPort(const int homeAssistantPort) {
    ScopedRecursiveSemaphore lock(ensurePersistenceMutex());
    this->homeAssistantPort = homeAssistantPort;
    save();
}
void Settings::setHomeAssistantTopic(const String &homeAssistantTopic) {
    ScopedRecursiveSemaphore lock(ensurePersistenceMutex());
    // Bound the discovery-topic prefix so the topic built in MQTTPlugin (an 80-byte buffer)
    // can never be silently truncated by snprintf. See MAX_HOME_ASSISTANT_TOPIC_LENGTH.
    if (homeAssistantTopic.length() > MAX_HOME_ASSISTANT_TOPIC_LENGTH) {
        assignUnderSelectedNameLock(this->homeAssistantTopic, homeAssistantTopic.substring(0, MAX_HOME_ASSISTANT_TOPIC_LENGTH));
    } else {
        assignUnderSelectedNameLock(this->homeAssistantTopic, String(homeAssistantTopic));
    }
    save();
}
void Settings::setHomeAssistantUser(const String &homeAssistantUser) {
    ScopedRecursiveSemaphore lock(ensurePersistenceMutex());
    assignUnderSelectedNameLock(this->homeAssistantUser, String(homeAssistantUser));
    save();
}
void Settings::setHomeAssistantPassword(const String &homeAssistantPassword) {
    ScopedRecursiveSemaphore lock(ensurePersistenceMutex());
    assignUnderSelectedNameLock(this->homeAssistantPassword, String(homeAssistantPassword));
    save();
}

void Settings::setMomentaryButtons(bool momentary_buttons) {
    ScopedRecursiveSemaphore lock(ensurePersistenceMutex());
    momentaryButtons = momentary_buttons;
    save();
}

void Settings::setTimezone(String timezone) {
    ScopedRecursiveSemaphore lock(ensurePersistenceMutex());
    assignUnderSelectedNameLock(this->timezone, std::move(timezone));
    save();
}

void Settings::setClockFormat(bool clock_24h_format) {
    ScopedRecursiveSemaphore lock(ensurePersistenceMutex());
    this->clock24hFormat = clock_24h_format;
    save();
}

void Settings::setSelectedProfile(String selected_profile) {
    ScopedRecursiveSemaphore lock(ensurePersistenceMutex());
    assignUnderSelectedNameLock(this->selectedProfile, std::move(selected_profile));
    save();
}

String Settings::getBrewTemperatureOverrideProfile() const { return copyUnderSelectedNameLock(brewTemperatureOverrideProfile); }

void Settings::setBrewTemperatureOverride(const String &profileId, const float temperature) {
    ScopedRecursiveSemaphore lock(ensurePersistenceMutex());
    brewTemperatureOverrideEnabled = true;
    brewTemperatureOverride = temperature;
    assignUnderSelectedNameLock(brewTemperatureOverrideProfile, String(profileId));
    save();
}

void Settings::clearBrewTemperatureOverride() {
    ScopedRecursiveSemaphore lock(ensurePersistenceMutex());
    brewTemperatureOverrideEnabled = false;
    brewTemperatureOverride = 0.0f;
    assignUnderSelectedNameLock(brewTemperatureOverrideProfile, String(""));
    save();
}

SemaphoreHandle_t Settings::ensureSelectedNameMutex() const {
    if (selectedNameMutex == nullptr) {
        // Out of memory creating the mutex: degrade to lock-free (pre-PRO-427
        // behavior). A null handle makes the accessors below skip locking.
        selectedNameMutex = xSemaphoreCreateMutex();
    }
    return selectedNameMutex;
}

String Settings::copyUnderSelectedNameLock(const String &member) const noexcept {
    // PRO-427: copy the member into a local under the lock, then return the local so
    // the String copy cannot race a concurrent setter's buffer realloc. A null
    // handle degrades to a lock-free copy (pre-PRO-427 behavior). Structured with a
    // single copy site so only one String-copy exception path is emitted.
    SemaphoreHandle_t mutex = ensureSelectedNameMutex();
    if (mutex != nullptr) {
        xSemaphoreTake(mutex, portMAX_DELAY);
    }
    String copy = member;
    if (mutex != nullptr) {
        xSemaphoreGive(mutex);
    }
    return copy;
}

void Settings::assignUnderSelectedNameLock(String &member, String &&value) noexcept {
    // PRO-427: assign under the shared lock so a concurrent copy-read never observes
    // a String buffer mid-realloc. Takes an rvalue reference so the by-value setter
    // parameters move straight in without an extra temporary + unwind cleanup. A null
    // handle degrades to a lock-free assign. Single assign site so only one path is
    // emitted (mirrors copyUnderSelectedNameLock).
    SemaphoreHandle_t mutex = ensureSelectedNameMutex();
    if (mutex != nullptr) {
        xSemaphoreTake(mutex, portMAX_DELAY);
    }
    member = std::move(value);
    if (mutex != nullptr) {
        xSemaphoreGive(mutex);
    }
}

SemaphoreHandle_t Settings::ensureVectorMutex() const {
    // PRO-486: vectorMutex is created eagerly in Settings::load(), before the
    // loop task (the first possible concurrent caller) starts. No lazy-init
    // here anymore -- this removes the theoretical double-create race from
    // PRO-481 where two concurrent first-callers could both observe nullptr
    // and each call xSemaphoreCreateMutex(), leaking one handle. A null
    // handle (creation failed in load(), e.g. out of memory) still degrades
    // accessors to lock-free, same as before.
    return vectorMutex;
}

std::vector<String> Settings::copyUnderVectorLock(const std::vector<String> &member) const noexcept {
    // PRO-481: copy the vector into a local under the lock, then return the
    // local so the copy cannot race a concurrent setter's mutation/reallocation.
    // A null handle degrades to a lock-free copy. Mirrors
    // copyUnderSelectedNameLock's single-copy-site structure.
    SemaphoreHandle_t mutex = ensureVectorMutex();
    if (mutex != nullptr) {
        xSemaphoreTake(mutex, portMAX_DELAY);
    }
    std::vector<String> copy = member;
    if (mutex != nullptr) {
        xSemaphoreGive(mutex);
    }
    return copy;
}

void Settings::assignUnderVectorLock(std::vector<String> &member, std::vector<String> &&value) noexcept {
    // PRO-481: assign under the shared lock so a concurrent copy-read (or
    // doSave()'s implode()) never observes the vector mid-mutation. Mirrors
    // assignUnderSelectedNameLock's single-assign-site structure.
    SemaphoreHandle_t mutex = ensureVectorMutex();
    if (mutex != nullptr) {
        xSemaphoreTake(mutex, portMAX_DELAY);
    }
    member = std::move(value);
    if (mutex != nullptr) {
        xSemaphoreGive(mutex);
    }
}

// PRO-478: out-of-line getters for the String members guarded by
// selectedNameMutex — mirrors getSelectedBean()/getSelectedGrinder() (PRO-427).
String Settings::getPid() const { return copyUnderSelectedNameLock(pid); }

String Settings::getPumpModelCoeffs() const { return copyUnderSelectedNameLock(pumpModelCoeffs); }

String Settings::getWifiSsid() const { return copyUnderSelectedNameLock(wifiSsid); }

String Settings::getWifiPassword() const { return copyUnderSelectedNameLock(wifiPassword); }

String Settings::getMdnsName() const { return copyUnderSelectedNameLock(mdnsName); }

String Settings::getOTAChannel() const { return copyUnderSelectedNameLock(otaChannel); }

String Settings::getInstalledChannel() const { return copyUnderSelectedNameLock(installedChannel); }

String Settings::getSavedScale() const { return copyUnderSelectedNameLock(savedScale); }

String Settings::getSmartGrindIp() const { return copyUnderSelectedNameLock(smartGrindIp); }

String Settings::getHomeAssistantIP() const { return copyUnderSelectedNameLock(homeAssistantIP); }

String Settings::getHomeAssistantUser() const { return copyUnderSelectedNameLock(homeAssistantUser); }

String Settings::getHomeAssistantPassword() const { return copyUnderSelectedNameLock(homeAssistantPassword); }

String Settings::getHomeAssistantTopic() const { return copyUnderSelectedNameLock(homeAssistantTopic); }

String Settings::getTimezone() const { return copyUnderSelectedNameLock(timezone); }

String Settings::getSelectedProfile() const { return copyUnderSelectedNameLock(selectedProfile); }

String Settings::getCloudRelayUrl() const { return copyUnderSelectedNameLock(cloudRelayUrl); }

String Settings::getCloudRelayToken() const { return copyUnderSelectedNameLock(cloudRelayToken); }

String Settings::getSelectedBean() const { return copyUnderSelectedNameLock(selectedBean); }

String Settings::getSelectedGrinder() const { return copyUnderSelectedNameLock(selectedGrinder); }

// PRO-481: out-of-line vector getters guarded by vectorMutex — mirrors the
// String getters guarded by selectedNameMutex above.
std::vector<String> Settings::getFavoritedProfiles() const { return copyUnderVectorLock(favoritedProfiles); }

std::vector<String> Settings::getProfileOrder() const { return copyUnderVectorLock(profileOrder); }

void Settings::setSelectedBean(String selected_bean) {
    ScopedRecursiveSemaphore lock(ensurePersistenceMutex());
    // save() is intentionally left OUTSIDE the lock scope: it only sets dirty=true
    // (the deferred flush task calls doSave() later), so holding the lock across it
    // would gain nothing and risk widening the critical section over a flash write.
    assignUnderSelectedNameLock(selectedBean, std::move(selected_bean));
    save();
}

void Settings::setSelectedGrinder(String selected_grinder) {
    ScopedRecursiveSemaphore lock(ensurePersistenceMutex());
    assignUnderSelectedNameLock(selectedGrinder, std::move(selected_grinder));
    save();
}

void Settings::setFavoritedProfiles(std::vector<String> favorited_profiles) {
    ScopedRecursiveSemaphore lock(ensurePersistenceMutex());
    assignUnderVectorLock(favoritedProfiles, cleanProfileIds(std::move(favorited_profiles), "favoritedProfiles"));
    save();
}

void Settings::addFavoritedProfile(String profile) {
    if (!isSafeId(profile)) {
        return;
    }
    ScopedRecursiveSemaphore persistenceLock(ensurePersistenceMutex());
    // PRO-481: guard the whole find+emplace under vectorMutex so a concurrent
    // doSave() implode() (or another setter) never observes the vector
    // mid-mutation. save() is only called when a profile was actually added,
    // matching the pre-guard behavior, and is left outside the lock scope for
    // the same reason assignUnderSelectedNameLock's callers leave it out.
    bool added = false;
    SemaphoreHandle_t mutex = ensureVectorMutex();
    if (mutex != nullptr) {
        xSemaphoreTake(mutex, portMAX_DELAY);
    }
    if (std::find(favoritedProfiles.begin(), favoritedProfiles.end(), profile) == favoritedProfiles.end()) {
        favoritedProfiles.emplace_back(profile);
        added = true;
    }
    if (mutex != nullptr) {
        xSemaphoreGive(mutex);
    }
    if (added) {
        save();
    }
}

void Settings::removeFavoritedProfile(String profile) {
    ScopedRecursiveSemaphore persistenceLock(ensurePersistenceMutex());
    // PRO-481: guard the erase+shrink_to_fit under vectorMutex; both mutate
    // the vector's storage and must not race a concurrent implode() read.
    // PRO-487: save() is only called when a profile was actually erased,
    // mirroring the bool-added guard in addFavoritedProfile.
    bool erased = false;
    SemaphoreHandle_t mutex = ensureVectorMutex();
    if (mutex != nullptr) {
        xSemaphoreTake(mutex, portMAX_DELAY);
    }
    auto newEnd = std::remove(favoritedProfiles.begin(), favoritedProfiles.end(), profile);
    if (newEnd != favoritedProfiles.end()) {
        favoritedProfiles.erase(newEnd, favoritedProfiles.end());
        favoritedProfiles.shrink_to_fit();
        erased = true;
    }
    if (mutex != nullptr) {
        xSemaphoreGive(mutex);
    }
    if (erased) {
        save();
    }
}

void Settings::setProfileOrder(std::vector<String> profile_order) {
    ScopedRecursiveSemaphore lock(ensurePersistenceMutex());
    assignUnderVectorLock(profileOrder, cleanProfileIds(std::move(profile_order), "profileOrder"));
    save();
}

// PRO-481: migrateProfileIds() (including the direct `selectedProfile = ...`
// assignments below and the favoritedProfiles/profileOrder assignments further
// down) runs exactly once, synchronously, from ProfileManager::setup() during
// Controller::setup() -- BEFORE pluginManager->registerPlugin(new
// WebUIPlugin()) and BEFORE xTaskCreatePinnedToCore() spin up the loop task.
// No AsyncTCP/WebSocket-handler task or deferred-flush task exists yet at
// this point, so there is no concurrent accessor to race against and neither
// selectedNameMutex nor vectorMutex is needed here. Do not remove this
// assertion when touching this function -- if migrateProfileIds() is ever
// called after setup() (e.g. from a runtime "re-scan profiles" feature), the
// assignments below must be routed through assignUnderSelectedNameLock /
// assignUnderVectorLock instead.
void Settings::migrateProfileIds(const std::vector<std::pair<String, String>> &migrations) {
    preferences.begin(PREFERENCES_KEY, true);

    bool needsSave = false;
    const auto selectedProfileRaw = preferences.getString("sp", "");
    selectedProfile = remapProfileId(selectedProfileRaw, migrations);
    if (selectedProfile != selectedProfileRaw) {
        needsSave = true;
    }
    if (!selectedProfile.isEmpty() && !isSafeId(selectedProfile)) {
        ESP_LOGW("Settings", "Dropping stale persisted selectedProfile id: %s", selectedProfile.c_str());
        selectedProfile = "";
        needsSave = true;
    }

    const auto rawFavoritedProfiles = explode(preferences.getString("fp", ""), ',');
    const auto remappedFavoritedProfiles = remapProfileIds(rawFavoritedProfiles, migrations);
    favoritedProfiles = cleanProfileIds(remappedFavoritedProfiles, "favoritedProfiles");
    if (remappedFavoritedProfiles != rawFavoritedProfiles || favoritedProfiles != remappedFavoritedProfiles) {
        needsSave = true;
    }

    const auto rawProfileOrder = explode(preferences.getString("po", ""), ',');
    const auto remappedProfileOrder = remapProfileIds(rawProfileOrder, migrations);
    profileOrder = cleanProfileIds(remappedProfileOrder, "profileOrder");
    if (remappedProfileOrder != rawProfileOrder || profileOrder != remappedProfileOrder) {
        needsSave = true;
    }

    preferences.end();

    if (needsSave) {
        // save(true) marks the migrated sp/fp/po values dirty before taking
        // the synchronous persistence snapshot.
        save(true);
    }
}

void Settings::setMainBrightness(int main_brightness) {
    ScopedRecursiveSemaphore lock(ensurePersistenceMutex());
    mainBrightness = main_brightness;
    save();
}

void Settings::setStandbyBrightness(int standby_brightness) {
    ScopedRecursiveSemaphore lock(ensurePersistenceMutex());
    standbyBrightness = standby_brightness;
    save();
}

void Settings::setStandbyBrightnessTimeout(int standby_brightness_timeout) {
    ScopedRecursiveSemaphore lock(ensurePersistenceMutex());
    standbyBrightnessTimeout = standby_brightness_timeout;
    save();
}

void Settings::setWifiApTimeout(int timeout) {
    ScopedRecursiveSemaphore lock(ensurePersistenceMutex());
    wifiApTimeout = timeout;
    save();
}

void Settings::setSteamPumpPercentage(float steam_pump_percentage) {
    ScopedRecursiveSemaphore lock(ensurePersistenceMutex());
    steamPumpPercentage = steam_pump_percentage;
    save();
}

void Settings::setSteamPumpCutoff(float steam_pump_cutoff) {
    ScopedRecursiveSemaphore lock(ensurePersistenceMutex());
    steamPumpCutoff = steam_pump_cutoff;
    save();
}

void Settings::setThemeMode(int theme_mode) {
    ScopedRecursiveSemaphore lock(ensurePersistenceMutex());
    themeMode = theme_mode;
    save();
}

void Settings::setHistoryIndex(int history_index) {
    ScopedRecursiveSemaphore lock(ensurePersistenceMutex());
    historyIndex = history_index;
    save();
}

void Settings::setFlushDuration(int flush_duration) {
    ScopedRecursiveSemaphore lock(ensurePersistenceMutex());
    flushDuration = std::clamp(flush_duration, 1000, 60000);
    save();
}

void Settings::setCloudRelayUrl(const String &url) {
    ScopedRecursiveSemaphore lock(ensurePersistenceMutex());
    assignUnderSelectedNameLock(cloudRelayUrl, String(url));
    save();
}

void Settings::setCloudRelayToken(const String &token) {
    ScopedRecursiveSemaphore lock(ensurePersistenceMutex());
    assignUnderSelectedNameLock(cloudRelayToken, String(token));
    save();
}

void Settings::setCloudRelayEnabled(bool enabled) {
    ScopedRecursiveSemaphore lock(ensurePersistenceMutex());
    cloudRelayEnabled = enabled;
    save();
}

void Settings::setLocalAdminToken(const String &token) {
    ScopedRecursiveSemaphore lock(ensurePersistenceMutex());
    localAdminToken = token;
    save(true);
}

void Settings::setLocalAuthProvisioned(bool provisioned) {
    ScopedRecursiveSemaphore lock(ensurePersistenceMutex());
    localAuthProvisioned = provisioned;
    save(true);
}

// PRO-23: setManualTargetType/setManualPressure/setManualFlow/setManualTemperature
// no longer call save() individually; updateManualTargets() calls save() once
// after all four. NOTE: setManualTemperature has one additional call site
// (Controller::setTargetTemp MODE_MANUAL) which calls save() explicitly.
void Settings::setManualTargetType(int target_type) {
    ScopedRecursiveSemaphore lock(ensurePersistenceMutex());
    manualTargetType = target_type == MANUAL_TARGET_FLOW ? MANUAL_TARGET_FLOW : MANUAL_TARGET_PRESSURE;
}

void Settings::setManualPressure(float pressure) {
    ScopedRecursiveSemaphore lock(ensurePersistenceMutex());
    manualPressure = std::clamp(pressure, MIN_MANUAL_PRESSURE, MAX_MANUAL_PRESSURE);
}

void Settings::setManualFlow(float flow) {
    ScopedRecursiveSemaphore lock(ensurePersistenceMutex());
    manualFlow = std::clamp(flow, MIN_MANUAL_FLOW, MAX_MANUAL_FLOW);
}

void Settings::setManualTemperature(int temperature) {
    ScopedRecursiveSemaphore lock(ensurePersistenceMutex());
    manualTemperature = std::clamp(temperature, MIN_MANUAL_TEMPERATURE, MAX_MANUAL_TEMPERATURE);
}

void Settings::setSunriseR(int sunrise_r) {
    ScopedRecursiveSemaphore lock(ensurePersistenceMutex());
    sunriseR = sunrise_r;
    save();
}

void Settings::setSunriseG(int sunrise_g) {
    ScopedRecursiveSemaphore lock(ensurePersistenceMutex());
    sunriseG = sunrise_g;
    save();
}

void Settings::setSunriseB(int sunrise_b) {
    ScopedRecursiveSemaphore lock(ensurePersistenceMutex());
    sunriseB = sunrise_b;
    save();
}

void Settings::setSunriseW(int sunrise_w) {
    ScopedRecursiveSemaphore lock(ensurePersistenceMutex());
    sunriseW = sunrise_w;
    save();
}

void Settings::setSunriseExtBrightness(int sunrise_ext_brightness) {
    ScopedRecursiveSemaphore lock(ensurePersistenceMutex());
    sunriseExtBrightness = sunrise_ext_brightness;
    save();
}

void Settings::setEmptyTankDistance(int empty_tank_distance) {
    ScopedRecursiveSemaphore lock(ensurePersistenceMutex());
    emptyTankDistance = empty_tank_distance;
    save();
}

void Settings::setFullTankDistance(int full_tank_distance) {
    ScopedRecursiveSemaphore lock(ensurePersistenceMutex());
    fullTankDistance = full_tank_distance;
    save();
}

void Settings::setAltRelayFunction(int alt_relay_function) {
    ScopedRecursiveSemaphore lock(ensurePersistenceMutex());
    altRelayFunction = alt_relay_function;
    altRelayConfigured = true;
}

void Settings::setAutoWakeupEnabled(bool enabled) {
    ScopedRecursiveSemaphore lock(ensurePersistenceMutex());
    autowakeupEnabled = enabled;
    save();
}

void Settings::setAutoWakeupSchedules(const std::vector<AutoWakeupSchedule> &schedules) {
    ScopedRecursiveSemaphore lock(ensurePersistenceMutex());
    autowakeupSchedules = schedules;
    save();
}

Settings::PersistenceSnapshot Settings::takePersistenceSnapshot() {
    PersistenceSnapshot snapshot;
    snapshot.startupMode = startupMode;
    snapshot.targetSteamTemp = targetSteamTemp;
    snapshot.targetWaterTemp = targetWaterTemp;
    snapshot.targetGrindDuration = targetGrindDuration;
    snapshot.temperatureOffset = temperatureOffset;
    snapshot.standbyTimeout = standbyTimeout;
    snapshot.startupFillTime = startupFillTime;
    snapshot.steamFillTime = steamFillTime;
    snapshot.smartGrindMode = smartGrindMode;
    snapshot.homeAssistantPort = homeAssistantPort;
    snapshot.mainBrightness = mainBrightness;
    snapshot.standbyBrightness = standbyBrightness;
    snapshot.standbyBrightnessTimeout = standbyBrightnessTimeout;
    snapshot.wifiApTimeout = wifiApTimeout;
    snapshot.themeMode = themeMode;
    snapshot.historyIndex = historyIndex;
    snapshot.flushDuration = flushDuration;
    snapshot.manualTargetType = manualTargetType;
    snapshot.manualTemperature = manualTemperature;
    snapshot.sunriseR = sunriseR;
    snapshot.sunriseG = sunriseG;
    snapshot.sunriseB = sunriseB;
    snapshot.sunriseW = sunriseW;
    snapshot.sunriseExtBrightness = sunriseExtBrightness;
    snapshot.emptyTankDistance = emptyTankDistance;
    snapshot.fullTankDistance = fullTankDistance;
    snapshot.altRelayFunction = altRelayFunction;
    snapshot.pressureScaling = pressureScaling;
    snapshot.steamPumpPercentage = steamPumpPercentage;
    snapshot.steamPumpCutoff = steamPumpCutoff;
    snapshot.manualPressure = manualPressure;
    snapshot.manualFlow = manualFlow;
    snapshot.targetGrindVolume = targetGrindVolume;
    snapshot.brewDelay = brewDelay;
    snapshot.grindDelay = grindDelay;
    snapshot.doseGrams = doseGrams;
    snapshot.manualGrindSetting = manualGrindSetting;
    snapshot.delayAdjust = delayAdjust;
    snapshot.homekit = homekit;
    snapshot.volumetricTarget = volumetricTarget;
    snapshot.allowYieldOverride = allowYieldOverride;
    snapshot.autoSteamEnabled = autoSteamEnabled;
    snapshot.standbyOnBrewEnabled = standbyOnBrewEnabled;
    snapshot.boilerFillActive = boilerFillActive;
    snapshot.smartGrindActive = smartGrindActive;
    snapshot.diagnosticLogEnabled = diagnosticLogEnabled;
    snapshot.smartGrindToggle = smartGrindToggle;
    snapshot.homeAssistant = homeAssistant;
    snapshot.momentaryButtons = momentaryButtons;
    snapshot.clock24hFormat = clock24hFormat;
    snapshot.autowakeupEnabled = autowakeupEnabled;
    snapshot.altRelayConfigured = altRelayConfigured;
    snapshot.cloudRelayEnabled = cloudRelayEnabled;
    snapshot.localAuthProvisioned = localAuthProvisioned;
    snapshot.brewTemperatureOverrideEnabled = brewTemperatureOverrideEnabled;
    snapshot.brewTemperatureOverride = brewTemperatureOverride;
    SemaphoreHandle_t stringLock = ensureSelectedNameMutex();
    if (stringLock != nullptr)
        xSemaphoreTake(stringLock, portMAX_DELAY);
    snapshot.pid = pid;
    snapshot.pumpModelCoeffs = pumpModelCoeffs;
    snapshot.wifiSsid = wifiSsid;
    snapshot.wifiPassword = wifiPassword;
    snapshot.mdnsName = mdnsName;
    snapshot.otaChannel = otaChannel;
    snapshot.installedChannel = installedChannel;
    snapshot.savedScale = savedScale;
    snapshot.smartGrindIp = smartGrindIp;
    snapshot.homeAssistantIP = homeAssistantIP;
    snapshot.homeAssistantTopic = homeAssistantTopic;
    snapshot.homeAssistantUser = homeAssistantUser;
    snapshot.homeAssistantPassword = homeAssistantPassword;
    snapshot.timezone = timezone;
    snapshot.selectedProfile = selectedProfile;
    snapshot.brewTemperatureOverrideProfile = brewTemperatureOverrideProfile;
    snapshot.selectedBean = selectedBean;
    snapshot.selectedGrinder = selectedGrinder;
    snapshot.cloudRelayUrl = cloudRelayUrl;
    snapshot.cloudRelayToken = cloudRelayToken;
    snapshot.localAdminToken = localAdminToken;
    if (stringLock != nullptr)
        xSemaphoreGive(stringLock);
    SemaphoreHandle_t vectorLock = ensureVectorMutex();
    if (vectorLock != nullptr)
        xSemaphoreTake(vectorLock, portMAX_DELAY);
    snapshot.favoritedProfiles = implode(favoritedProfiles, ",");
    snapshot.profileOrder = implode(profileOrder, ",");
    snapshot.autowakeupSchedules = autowakeupSchedules;
    if (vectorLock != nullptr)
        xSemaphoreGive(vectorLock);
    return snapshot;
}

void Settings::doSave() {
    // Copy the complete persisted state while the batch transaction is locked,
    // then release it before the slow NVS writes below.
    PersistenceSnapshot snapshot;
    {
        ScopedRecursiveSemaphore lock(ensurePersistenceMutex());
        if (!persistenceTransaction.tryBeginSnapshot()) {
            return;
        }
        snapshot = takePersistenceSnapshot();
    }

    ESP_LOGI("Settings", "Saving settings");
    preferences.begin(PREFERENCES_KEY, false);
    preferences.putInt("sm", snapshot.startupMode);
    preferences.putInt("ts", snapshot.targetSteamTemp);
    preferences.putInt("tw", snapshot.targetWaterTemp);
    preferences.putDouble("tgv", snapshot.targetGrindVolume);
    preferences.putInt("tgd", snapshot.targetGrindDuration);
    preferences.putDouble("del_br", snapshot.brewDelay);
    preferences.putDouble("del_gd", snapshot.grindDelay);
    preferences.putBool("del_ad", snapshot.delayAdjust);
    preferences.putInt("to", snapshot.temperatureOffset);
    preferences.putFloat("ps", snapshot.pressureScaling);
    preferences.putString("pid", snapshot.pid);
    preferences.putString("pmc", snapshot.pumpModelCoeffs);
    preferences.putString("ws", snapshot.wifiSsid);
    preferences.putString("wp", snapshot.wifiPassword);
    preferences.putString("mn", snapshot.mdnsName);
    preferences.putBool("hk", snapshot.homekit);
    preferences.putBool("vt", snapshot.volumetricTarget);
    preferences.putBool("ayo", snapshot.allowYieldOverride);
    preferences.putBool("autosteam", snapshot.autoSteamEnabled);
    preferences.putBool("sbonbrew", snapshot.standbyOnBrewEnabled);
    preferences.putDouble("dosegrams", snapshot.doseGrams);
    preferences.putDouble("mgrind", snapshot.manualGrindSetting);
    preferences.putString("oc", snapshot.otaChannel);
    preferences.putString("ic", snapshot.installedChannel);
    preferences.putString("ssc", snapshot.savedScale);
    preferences.putBool("bf_a", snapshot.boilerFillActive);
    preferences.putInt("bf_su", snapshot.startupFillTime);
    preferences.putInt("bf_st", snapshot.steamFillTime);
    preferences.putBool("sg_a", snapshot.smartGrindActive);
    preferences.putBool("diag_log", snapshot.diagnosticLogEnabled);
    preferences.putString("sg_i", snapshot.smartGrindIp);
    preferences.putBool("sg_t", snapshot.smartGrindToggle);
    preferences.putInt("sg_m", snapshot.smartGrindMode);
    preferences.putBool("ha_a", snapshot.homeAssistant);
    preferences.putString("ha_i", snapshot.homeAssistantIP);
    preferences.putInt("ha_p", snapshot.homeAssistantPort);
    preferences.putString("ha_t", snapshot.homeAssistantTopic);
    preferences.putString("ha_u", snapshot.homeAssistantUser);
    preferences.putString("ha_pw", snapshot.homeAssistantPassword);
    preferences.putString("tz", snapshot.timezone);
    preferences.putBool("clk_24h", snapshot.clock24hFormat);
    preferences.putString("sp", snapshot.selectedProfile);
    preferences.putBool("bto_en", snapshot.brewTemperatureOverrideEnabled);
    preferences.putFloat("bto", snapshot.brewTemperatureOverride);
    preferences.putString("bto_p", snapshot.brewTemperatureOverrideProfile);
    preferences.putString("sb", snapshot.selectedBean);
    preferences.putString("sg", snapshot.selectedGrinder);
    preferences.putInt("sbt", snapshot.standbyTimeout);
    preferences.putBool("mb", snapshot.momentaryButtons);
    preferences.putString("fp", snapshot.favoritedProfiles);
    preferences.putString("po", snapshot.profileOrder);
    preferences.putFloat("spp", snapshot.steamPumpPercentage);
    preferences.putFloat("spc", snapshot.steamPumpCutoff);
    preferences.putInt("hi", snapshot.historyIndex);
    preferences.putInt("fd", snapshot.flushDuration);
    preferences.putInt("mtt", snapshot.manualTargetType);
    preferences.putFloat("mp", snapshot.manualPressure);
    preferences.putFloat("mf", snapshot.manualFlow);
    preferences.putInt("mt", snapshot.manualTemperature);
    preferences.putBool("ab_en", snapshot.autowakeupEnabled);

    // Save schedule format
    String schedulesForSave = "";
    for (size_t i = 0; i < snapshot.autowakeupSchedules.size(); i++) {
        if (i > 0)
            schedulesForSave += ";";
        schedulesForSave += snapshot.autowakeupSchedules[i].time + "|";

        // Convert days array to 7-bit string
        for (int j = 0; j < 7; j++) {
            schedulesForSave += snapshot.autowakeupSchedules[i].days[j] ? "1" : "0";
        }
    }
    preferences.putString("ab_schedules", schedulesForSave);

    // Display settings
    preferences.putInt("main_b", snapshot.mainBrightness);
    preferences.putInt("standby_b", snapshot.standbyBrightness);
    preferences.putInt("standby_bt", snapshot.standbyBrightnessTimeout);
    preferences.putInt("wifi_apt", snapshot.wifiApTimeout);
    preferences.putInt("theme", snapshot.themeMode);

    // Sunrise Settings
    preferences.putInt("sr_r", snapshot.sunriseR);
    preferences.putInt("sr_g", snapshot.sunriseG);
    preferences.putInt("sr_b", snapshot.sunriseB);
    preferences.putInt("sr_w", snapshot.sunriseW);
    preferences.putInt("sr_exb", snapshot.sunriseExtBrightness);
    preferences.putInt("sr_ed", snapshot.emptyTankDistance);
    preferences.putInt("sr_fd", snapshot.fullTankDistance);
    preferences.putInt("alt_relay", snapshot.altRelayFunction);
    preferences.putBool("alt_set", snapshot.altRelayConfigured);
    preferences.putString("cr_url", snapshot.cloudRelayUrl);
    preferences.putString("cr_token", snapshot.cloudRelayToken);
    preferences.putBool("cr_enabled", snapshot.cloudRelayEnabled);
    preferences.putString("admin_token", snapshot.localAdminToken);
    preferences.putBool("admin_ready", snapshot.localAuthProvisioned);

    preferences.end();
}

[[noreturn]] void Settings::loopTask(void *arg) {
    auto *settings = static_cast<Settings *>(arg);
    while (true) {
        settings->doSave();
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}
