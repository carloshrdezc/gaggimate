#ifndef EVENTIDS_H
#define EVENTIDS_H

// PRO-24: central registry of PluginManager event-ID strings.
//
// PluginManager::on()/trigger() dispatch by raw String key (see
// PluginManager.h). Before this header, ~60 unique event-ID strings were
// scattered as literals across ~9 files; a typo in either the on() or the
// trigger() side compiles fine and just silently never fires. Routing every
// call site through these constants turns a typo into a compile error and
// makes `grep -rn EventIds::CONTROLLER_BREW_START` the way to find every
// producer/consumer of an event.
//
// Only PluginManager on()/trigger()/subscribe()/emit() call sites belong
// here. WebSocket `tp` field strings sent to the JS client (e.g.
// "evt:status", "req:profiles:list", "res:...") are a separate protocol
// surface and are intentionally NOT included.

namespace EventIds {

// Controller lifecycle / connectivity
inline constexpr const char *CONTROLLER_STARTUP = "controller:startup";
inline constexpr const char *CONTROLLER_READY = "controller:ready";
inline constexpr const char *CONTROLLER_ERROR = "controller:error";
inline constexpr const char *CONTROLLER_BLUETOOTH_INIT = "controller:bluetooth:init";
inline constexpr const char *CONTROLLER_BLUETOOTH_CONNECT = "controller:bluetooth:connect";
inline constexpr const char *CONTROLLER_BLUETOOTH_DISCONNECT = "controller:bluetooth:disconnect";
inline constexpr const char *CONTROLLER_BLUETOOTH_WAITING = "controller:bluetooth:waiting";
inline constexpr const char *CONTROLLER_WIFI_CONNECT = "controller:wifi:connect";
inline constexpr const char *CONTROLLER_WIFI_DISCONNECT = "controller:wifi:disconnect";

// Controller mode / process
inline constexpr const char *CONTROLLER_MODE_CHANGE = "controller:mode:change";
inline constexpr const char *CONTROLLER_PROCESS_START = "controller:process:start";
inline constexpr const char *CONTROLLER_PROCESS_END = "controller:process:end";

// Controller brew lifecycle
inline constexpr const char *CONTROLLER_BREW_PRESTART = "controller:brew:prestart";
inline constexpr const char *CONTROLLER_BREW_START = "controller:brew:start";
inline constexpr const char *CONTROLLER_BREW_END = "controller:brew:end";
inline constexpr const char *CONTROLLER_BREW_CLEAR = "controller:brew:clear";

// Controller grind lifecycle
inline constexpr const char *CONTROLLER_GRIND_START = "controller:grind:start";
inline constexpr const char *CONTROLLER_GRIND_END = "controller:grind:end";
inline constexpr const char *CONTROLLER_GRIND_DURATION_CHANGE = "controller:grindDuration:change";
inline constexpr const char *CONTROLLER_GRIND_VOLUME_CHANGE = "controller:grindVolume:change";

// Controller targets (set when a profile is applied)
inline constexpr const char *CONTROLLER_TARGET_DURATION_CHANGE = "controller:targetDuration:change";
inline constexpr const char *CONTROLLER_TARGET_VOLUME_CHANGE = "controller:targetVolume:change";

// Controller autotune
inline constexpr const char *CONTROLLER_AUTOTUNE_START = "controller:autotune:start";
inline constexpr const char *CONTROLLER_AUTOTUNE_RESULT = "controller:autotune:result";

// Controller sensors
inline constexpr const char *CONTROLLER_TOF_CHANGE = "controller:tof:change";

// Controller volumetric measurement
inline constexpr const char *CONTROLLER_VOLUMETRIC_MEASUREMENT_BLUETOOTH_CHANGE =
    "controller:volumetric-measurement:bluetooth:change";
inline constexpr const char *CONTROLLER_VOLUMETRIC_MEASUREMENT_ESTIMATION_CHANGE =
    "controller:volumetric-measurement:estimation:change";
inline constexpr const char *CONTROLLER_VOLUMETRIC_MEASUREMENT_SOURCE_CHANGE =
    "controller:volumetric-measurement:source:change";

// Boiler
inline constexpr const char *BOILER_CURRENT_TEMPERATURE_CHANGE = "boiler:currentTemperature:change";
inline constexpr const char *BOILER_TARGET_TEMPERATURE_CHANGE = "boiler:targetTemperature:change";
inline constexpr const char *BOILER_PRESSURE_CHANGE = "boiler:pressure:change";

// Pump
inline constexpr const char *PUMP_FLOW_CHANGE = "pump:flow:change";
inline constexpr const char *PUMP_PUCK_FLOW_CHANGE = "pump:puck-flow:change";
inline constexpr const char *PUMP_PUCK_RESISTANCE_CHANGE = "pump:puck-resistance:change";

// Profiles
inline constexpr const char *PROFILES_PROFILE_SAVE = "profiles:profile:save";
inline constexpr const char *PROFILES_PROFILE_SELECT = "profiles:profile:select";
inline constexpr const char *PROFILES_PROFILE_FAVORITE = "profiles:profile:favorite";
inline constexpr const char *PROFILES_PROFILE_UNFAVORITE = "profiles:profile:unfavorite";

// Beans / grinders
inline constexpr const char *BEANS_SELECTED = "beans:selected";
inline constexpr const char *GRINDERS_SELECTED = "grinders:selected";

// Auto-wakeup
inline constexpr const char *AUTOWAKEUP_ACTIVATED = "autowakeup:activated";

// Settings
inline constexpr const char *SETTINGS_CHANGED = "settings:changed";

// OTA
inline constexpr const char *OTA_UPDATE_START = "ota:update:start";
inline constexpr const char *OTA_UPDATE_END = "ota:update:end";
inline constexpr const char *OTA_UPDATE_PHASE = "ota:update:phase";
inline constexpr const char *OTA_UPDATE_PROGRESS = "ota:update:progress";
inline constexpr const char *OTA_UPDATE_STATUS = "ota:update:status";

// Shot history
inline constexpr const char *EVT_HISTORY_REBUILD_PROGRESS = "evt:history-rebuild-progress";

} // namespace EventIds

#endif // EVENTIDS_H
