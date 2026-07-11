#include "Settings.h"

#include <algorithm>
#include <utility>

namespace {
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
    homekit = preferences.getBool("hk", false);
    volumetricTarget = preferences.getBool("vt", false);
    allowYieldOverride = preferences.getBool("ayo", false);
    autoSteamEnabled = preferences.getBool("autosteam", false);
    doseGrams = preferences.getDouble("dosegrams", 18.0);
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
    dirty = false;
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

    preferences.end();

    // PRO-486: create vectorMutex here, eagerly, before the loop task (and
    // therefore any other task) can reach a vector accessor. Closes the
    // theoretical lazy-init double-create race in ensureVectorMutex() —
    // load() runs single-threaded, so there is no concurrent caller to race.
    // PRO-488: guard against double-init if load() is ever called twice
    // (e.g. in a future test harness). No-op when mutex already created.
    if (vectorMutex == nullptr)
        vectorMutex = xSemaphoreCreateMutex();

    xTaskCreate(loopTask, "Settings::loop", configMINIMAL_STACK_SIZE * 6, this, 1, &taskHandle);
}

void Settings::batchUpdate(const SettingsCallback &callback) {
    callback(this);
    save();
}

void Settings::save(bool noDelay) {
    dirty = true;
    if (noDelay) {
        doSave();
    }
}

void Settings::setTargetSteamTemp(const int target_steam_temp) {
    targetSteamTemp = target_steam_temp;
    save();
}

void Settings::setTargetWaterTemp(const int target_water_temp) {
    targetWaterTemp = target_water_temp;
    save();
}

void Settings::setTemperatureOffset(const int temperature_offset) {
    temperatureOffset = temperature_offset;
    save();
}

void Settings::setPressureScaling(const float pressure_scaling) {
    pressureScaling = pressure_scaling;
    save();
}

void Settings::setTargetGrindVolume(double target_grind_volume) {
    targetGrindVolume = target_grind_volume;
    save();
}

void Settings::setTargetGrindDuration(const int target_duration) {
    targetGrindDuration = target_duration;
    save();
}

void Settings::setBrewDelay(double brew_Delay) {
    brewDelay = std::clamp(brew_Delay, 0.0, 4000.0);
    save();
}

void Settings::setGrindDelay(double grind_Delay) {
    grindDelay = std::clamp(grind_Delay, 0.0, 4000.0);
    save();
}

void Settings::setDelayAdjust(bool delay_adjust) {
    delayAdjust = delay_adjust;
    save();
}

void Settings::setStartupMode(const int startup_mode) {
    startupMode = startup_mode;
    save();
}

void Settings::setStandbyTimeout(int standby_timeout) {
    standbyTimeout = standby_timeout;
    save();
}

void Settings::setPid(const String &pid) {
    assignUnderSelectedNameLock(this->pid, String(pid));
    save();
}

void Settings::setPumpModelCoeffs(const String &pumpModelCoeffs) {
    assignUnderSelectedNameLock(this->pumpModelCoeffs, String(pumpModelCoeffs));
    save();
}

void Settings::setWifiSsid(const String &wifiSsid) {
    assignUnderSelectedNameLock(this->wifiSsid, String(wifiSsid));
    save();
}

void Settings::setWifiPassword(const String &wifiPassword) {
    assignUnderSelectedNameLock(this->wifiPassword, String(wifiPassword));
    save();
}

void Settings::setMdnsName(const String &mdnsName) {
    assignUnderSelectedNameLock(this->mdnsName, String(mdnsName));
    save();
}

void Settings::setHomekit(const bool homekit) {
    this->homekit = homekit;
    save();
}

void Settings::setVolumetricTarget(bool volumetric_target) {
    this->volumetricTarget = volumetric_target;
    save();
}

void Settings::setAllowYieldOverride(bool allow_yield_override) {
    this->allowYieldOverride = allow_yield_override;
    save();
}

void Settings::setAutoSteamEnabled(bool auto_steam_enabled) {
    this->autoSteamEnabled = auto_steam_enabled;
    save();
}

void Settings::setDoseGrams(double dose_grams) {
    this->doseGrams = std::clamp(dose_grams, 0.1, 200.0);
    save();
}

void Settings::setOTAChannel(const String &otaChannel) {
    assignUnderSelectedNameLock(this->otaChannel, String(otaChannel));
    save();
}

void Settings::setInstalledChannel(const String &installedChannel) {
    assignUnderSelectedNameLock(this->installedChannel, String(installedChannel));
    save();
}

void Settings::setSavedScale(const String &savedScale) {
    assignUnderSelectedNameLock(this->savedScale, String(savedScale));
    save();
}

void Settings::setBoilerFillActive(bool boiler_fill_active) {
    boilerFillActive = boiler_fill_active;
    save();
}

void Settings::setStartupFillTime(int startup_fill_time) {
    startupFillTime = startup_fill_time;
    save();
}

void Settings::setSteamFillTime(int steam_fill_time) {
    steamFillTime = steam_fill_time;
    save();
}

void Settings::setSmartGrindActive(bool smart_grind_active) {
    smartGrindActive = smart_grind_active;
    save();
}

void Settings::setDiagnosticLogEnabled(bool diagnostic_log_enabled) {
    diagnosticLogEnabled = diagnostic_log_enabled;
    save();
}

void Settings::setSmartGrindIp(String smart_grind_ip) {
    assignUnderSelectedNameLock(this->smartGrindIp, std::move(smart_grind_ip));
    save();
}

void Settings::setSmartGrindMode(int smart_grind_mode) {
    this->smartGrindMode = smart_grind_mode;
    save();
}

void Settings::setHomeAssistant(const bool homeAssistant) {
    this->homeAssistant = homeAssistant;
    save();
}

void Settings::setHomeAssistantIP(const String &homeAssistantIP) {
    assignUnderSelectedNameLock(this->homeAssistantIP, String(homeAssistantIP));
    save();
}

void Settings::setHomeAssistantPort(const int homeAssistantPort) {
    this->homeAssistantPort = homeAssistantPort;
    save();
}
void Settings::setHomeAssistantTopic(const String &homeAssistantTopic) {
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
    assignUnderSelectedNameLock(this->homeAssistantUser, String(homeAssistantUser));
    save();
}
void Settings::setHomeAssistantPassword(const String &homeAssistantPassword) {
    assignUnderSelectedNameLock(this->homeAssistantPassword, String(homeAssistantPassword));
    save();
}

void Settings::setMomentaryButtons(bool momentary_buttons) {
    momentaryButtons = momentary_buttons;
    save();
}

void Settings::setTimezone(String timezone) {
    assignUnderSelectedNameLock(this->timezone, std::move(timezone));
    save();
}

void Settings::setClockFormat(bool clock_24h_format) {
    this->clock24hFormat = clock_24h_format;
    save();
}

void Settings::setSelectedProfile(String selected_profile) {
    assignUnderSelectedNameLock(this->selectedProfile, std::move(selected_profile));
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
    // save() is intentionally left OUTSIDE the lock scope: it only sets dirty=true
    // (the deferred flush task calls doSave() later), so holding the lock across it
    // would gain nothing and risk widening the critical section over a flash write.
    assignUnderSelectedNameLock(selectedBean, std::move(selected_bean));
    save();
}

void Settings::setSelectedGrinder(String selected_grinder) {
    assignUnderSelectedNameLock(selectedGrinder, std::move(selected_grinder));
    save();
}

void Settings::setFavoritedProfiles(std::vector<String> favorited_profiles) {
    assignUnderVectorLock(favoritedProfiles, cleanProfileIds(std::move(favorited_profiles), "favoritedProfiles"));
    save();
}

void Settings::addFavoritedProfile(String profile) {
    if (!isSafeId(profile)) {
        return;
    }
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
        // doSave() early-returns when !dirty. Without setting the flag here,
        // save(true) would no-op and the migrated sp/fp/po values stay only in
        // memory — a reboot before any other setter dirties the state would
        // lose the migration for that boot. Mark dirty explicitly so the
        // synchronous save actually writes to NVS.
        dirty = true;
        save(true);
    }
}

void Settings::setMainBrightness(int main_brightness) {
    mainBrightness = main_brightness;
    save();
}

void Settings::setStandbyBrightness(int standby_brightness) {
    standbyBrightness = standby_brightness;
    save();
}

void Settings::setStandbyBrightnessTimeout(int standby_brightness_timeout) {
    standbyBrightnessTimeout = standby_brightness_timeout;
    save();
}

void Settings::setWifiApTimeout(int timeout) {
    wifiApTimeout = timeout;
    save();
}

void Settings::setSteamPumpPercentage(float steam_pump_percentage) {
    steamPumpPercentage = steam_pump_percentage;
    save();
}

void Settings::setSteamPumpCutoff(float steam_pump_cutoff) {
    steamPumpCutoff = steam_pump_cutoff;
    save();
}

void Settings::setThemeMode(int theme_mode) {
    themeMode = theme_mode;
    save();
}

void Settings::setHistoryIndex(int history_index) {
    historyIndex = history_index;
    save();
}

void Settings::setFlushDuration(int flush_duration) {
    flushDuration = std::clamp(flush_duration, 1000, 60000);
    save();
}

void Settings::setCloudRelayUrl(const String &url) {
    assignUnderSelectedNameLock(cloudRelayUrl, String(url));
    save();
}

void Settings::setCloudRelayToken(const String &token) {
    assignUnderSelectedNameLock(cloudRelayToken, String(token));
    save();
}

void Settings::setCloudRelayEnabled(bool enabled) {
    cloudRelayEnabled = enabled;
    save();
}

// PRO-23: setManualTargetType/setManualPressure/setManualFlow/setManualTemperature
// no longer call save() individually; updateManualTargets() calls save() once
// after all four. NOTE: setManualTemperature has one additional call site
// (Controller::setTargetTemp MODE_MANUAL) which calls save() explicitly.
void Settings::setManualTargetType(int target_type) {
    manualTargetType = target_type == MANUAL_TARGET_FLOW ? MANUAL_TARGET_FLOW : MANUAL_TARGET_PRESSURE;
}

void Settings::setManualPressure(float pressure) {
    manualPressure = std::clamp(pressure, MIN_MANUAL_PRESSURE, MAX_MANUAL_PRESSURE);
}

void Settings::setManualFlow(float flow) { manualFlow = std::clamp(flow, MIN_MANUAL_FLOW, MAX_MANUAL_FLOW); }

void Settings::setManualTemperature(int temperature) {
    manualTemperature = std::clamp(temperature, MIN_MANUAL_TEMPERATURE, MAX_MANUAL_TEMPERATURE);
}

void Settings::setSunriseR(int sunrise_r) {
    sunriseR = sunrise_r;
    save();
}

void Settings::setSunriseG(int sunrise_g) {
    sunriseG = sunrise_g;
    save();
}

void Settings::setSunriseB(int sunrise_b) {
    sunriseB = sunrise_b;
    save();
}

void Settings::setSunriseW(int sunrise_w) {
    sunriseW = sunrise_w;
    save();
}

void Settings::setSunriseExtBrightness(int sunrise_ext_brightness) {
    sunriseExtBrightness = sunrise_ext_brightness;
    save();
}

void Settings::setEmptyTankDistance(int empty_tank_distance) {
    emptyTankDistance = empty_tank_distance;
    save();
}

void Settings::setFullTankDistance(int full_tank_distance) {
    fullTankDistance = full_tank_distance;
    save();
}

void Settings::setAltRelayFunction(int alt_relay_function) {
    altRelayFunction = alt_relay_function;
    altRelayConfigured = true;
}

void Settings::setAutoWakeupEnabled(bool enabled) {
    autowakeupEnabled = enabled;
    save();
}

void Settings::setAutoWakeupSchedules(const std::vector<AutoWakeupSchedule> &schedules) {
    autowakeupSchedules = schedules;
    save();
}

void Settings::doSave() {
    if (!dirty) {
        return;
    }
    dirty = false;

    // PRO-478: doSave() runs on the deferred flush task, while every String member
    // below is written from the AsyncTCP/WebSocket-handler task via the setters
    // (which already assign under selectedNameMutex per PRO-427/PRO-437 for
    // selectedBean/selectedGrinder — this extends the same guard to the rest).
    // Snapshot ALL String members under a single lock acquisition up front, then
    // use the snapshots (not the live members) for every putString() below. This
    // keeps the critical section short (no lock held across the flash-write
    // preferences block) while eliminating the torn-String-read hazard on every
    // member, not just selectedBean/selectedGrinder.
    SemaphoreHandle_t stringLock = ensureSelectedNameMutex();
    if (stringLock != nullptr) {
        xSemaphoreTake(stringLock, portMAX_DELAY);
    }
    String pidSnap = pid;
    String pumpModelCoeffsSnap = pumpModelCoeffs;
    String wifiSsidSnap = wifiSsid;
    String wifiPasswordSnap = wifiPassword;
    String mdnsNameSnap = mdnsName;
    String otaChannelSnap = otaChannel;
    String installedChannelSnap = installedChannel;
    String savedScaleSnap = savedScale;
    String smartGrindIpSnap = smartGrindIp;
    String homeAssistantIPSnap = homeAssistantIP;
    String homeAssistantTopicSnap = homeAssistantTopic;
    String homeAssistantUserSnap = homeAssistantUser;
    String homeAssistantPasswordSnap = homeAssistantPassword;
    String timezoneSnap = timezone;
    String selectedProfileSnap = selectedProfile;
    String beanSnap = selectedBean;
    String grinderSnap = selectedGrinder;
    // PRO-481: cloudRelayUrl/cloudRelayToken are also guarded by
    // selectedNameMutex (see getCloudRelayUrl/getCloudRelayToken +
    // setCloudRelayUrl/setCloudRelayToken). Snapshot them inside the
    // same stringLock block so doSave() never observes them torn.
    String cloudRelayUrlSnap = cloudRelayUrl;
    String cloudRelayTokenSnap = cloudRelayToken;
    if (stringLock != nullptr) {
        xSemaphoreGive(stringLock);
    }

    // PRO-481: favoritedProfiles/profileOrder are guarded by the separate
    // vectorMutex (not selectedNameMutex — see the vectorMutex declaration in
    // Settings.h for why the two mutexes must stay independent). Take it only
    // around the implode() calls so the vector iterator stays consistent
    // against a concurrent setFavoritedProfiles()/addFavoritedProfile()/
    // removeFavoritedProfile()/setProfileOrder() call from the WS-handler
    // task, then release before the flash-write preferences block below.
    SemaphoreHandle_t vecLock = ensureVectorMutex();
    if (vecLock != nullptr) {
        xSemaphoreTake(vecLock, portMAX_DELAY);
    }
    String favoritedProfilesSnap = implode(favoritedProfiles, ",");
    String profileOrderSnap = implode(profileOrder, ",");
    if (vecLock != nullptr) {
        xSemaphoreGive(vecLock);
    }

    ESP_LOGI("Settings", "Saving settings");
    preferences.begin(PREFERENCES_KEY, false);
    preferences.putInt("sm", startupMode);
    preferences.putInt("ts", targetSteamTemp);
    preferences.putInt("tw", targetWaterTemp);
    preferences.putDouble("tgv", targetGrindVolume);
    preferences.putInt("tgd", targetGrindDuration);
    preferences.putDouble("del_br", brewDelay);
    preferences.putDouble("del_gd", grindDelay);
    preferences.putBool("del_ad", delayAdjust);
    preferences.putInt("to", temperatureOffset);
    preferences.putFloat("ps", pressureScaling);
    preferences.putString("pid", pidSnap);
    preferences.putString("pmc", pumpModelCoeffsSnap);
    preferences.putString("ws", wifiSsidSnap);
    preferences.putString("wp", wifiPasswordSnap);
    preferences.putString("mn", mdnsNameSnap);
    preferences.putBool("hk", homekit);
    preferences.putBool("vt", volumetricTarget);
    preferences.putBool("ayo", allowYieldOverride);
    preferences.putBool("autosteam", autoSteamEnabled);
    preferences.putDouble("dosegrams", doseGrams);
    preferences.putString("oc", otaChannelSnap);
    preferences.putString("ic", installedChannelSnap);
    preferences.putString("ssc", savedScaleSnap);
    preferences.putBool("bf_a", boilerFillActive);
    preferences.putInt("bf_su", startupFillTime);
    preferences.putInt("bf_st", steamFillTime);
    preferences.putBool("sg_a", smartGrindActive);
    preferences.putBool("diag_log", diagnosticLogEnabled);
    preferences.putString("sg_i", smartGrindIpSnap);
    preferences.putBool("sg_t", smartGrindToggle);
    preferences.putInt("sg_m", smartGrindMode);
    preferences.putBool("ha_a", homeAssistant);
    preferences.putString("ha_i", homeAssistantIPSnap);
    preferences.putInt("ha_p", homeAssistantPort);
    preferences.putString("ha_t", homeAssistantTopicSnap);
    preferences.putString("ha_u", homeAssistantUserSnap);
    preferences.putString("ha_pw", homeAssistantPasswordSnap);
    preferences.putString("tz", timezoneSnap);
    preferences.putBool("clk_24h", clock24hFormat);
    preferences.putString("sp", selectedProfileSnap);
    preferences.putString("sb", beanSnap);
    preferences.putString("sg", grinderSnap);
    preferences.putInt("sbt", standbyTimeout);
    preferences.putBool("mb", momentaryButtons);
    preferences.putString("fp", favoritedProfilesSnap);
    preferences.putString("po", profileOrderSnap);
    preferences.putFloat("spp", steamPumpPercentage);
    preferences.putFloat("spc", steamPumpCutoff);
    preferences.putInt("hi", historyIndex);
    preferences.putInt("fd", flushDuration);
    preferences.putInt("mtt", manualTargetType);
    preferences.putFloat("mp", manualPressure);
    preferences.putFloat("mf", manualFlow);
    preferences.putInt("mt", manualTemperature);
    preferences.putBool("ab_en", autowakeupEnabled);

    // Save schedule format
    String schedulesForSave = "";
    for (size_t i = 0; i < autowakeupSchedules.size(); i++) {
        if (i > 0)
            schedulesForSave += ";";
        schedulesForSave += autowakeupSchedules[i].time + "|";

        // Convert days array to 7-bit string
        for (int j = 0; j < 7; j++) {
            schedulesForSave += autowakeupSchedules[i].days[j] ? "1" : "0";
        }
    }
    preferences.putString("ab_schedules", schedulesForSave);

    // Display settings
    preferences.putInt("main_b", mainBrightness);
    preferences.putInt("standby_b", standbyBrightness);
    preferences.putInt("standby_bt", standbyBrightnessTimeout);
    preferences.putInt("wifi_apt", wifiApTimeout);
    preferences.putInt("theme", themeMode);

    // Sunrise Settings
    preferences.putInt("sr_r", sunriseR);
    preferences.putInt("sr_g", sunriseG);
    preferences.putInt("sr_b", sunriseB);
    preferences.putInt("sr_w", sunriseW);
    preferences.putInt("sr_exb", sunriseExtBrightness);
    preferences.putInt("sr_ed", emptyTankDistance);
    preferences.putInt("sr_fd", fullTankDistance);
    preferences.putInt("alt_relay", altRelayFunction);
    preferences.putBool("alt_set", altRelayConfigured);
    preferences.putString("cr_url", cloudRelayUrlSnap);
    preferences.putString("cr_token", cloudRelayTokenSnap);
    preferences.putBool("cr_enabled", cloudRelayEnabled);

    preferences.end();
}

[[noreturn]] void Settings::loopTask(void *arg) {
    auto *settings = static_cast<Settings *>(arg);
    while (true) {
        settings->doSave();
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}
