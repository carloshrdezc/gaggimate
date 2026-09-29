#pragma once

#include <cstdint>
#include <string>

// PRO-669 (R4a of docs/pro-655-nanopb-comms-migration.md): pure policy for the
// display's legacy BLE client deciding whether a connected controller exposes
// the characteristics the display cannot work without. Host-testable; no BLE.
//
// REQUIRED set — derived from how the display uses each characteristic:
//   - outputControl: the ONLY path for boiler setpoint / pump / valve
//     (sendOutputControl / sendAdvancedOutputControl). Without it nothing brews.
//   - sensor: the ONLY telemetry path (temp, pressure, flow) feeding
//     Controller::onTempRead and the brew processes. Without it the display
//     shows stale/zero temperature and pressure-based profiles are blind.
//   - ping: the controller's watchdog (PING_TIMEOUT_SECONDS) shuts outputs off
//     when pings stop, so a connection with no ping char would fault-cycle.
//   - info: Controller::setupInfos() reads it to learn hardware/version and
//     capabilities (pressure, dimming, LED, ToF); without it the display runs
//     with empty/default capabilities and mis-gates features.
// Everything else (tof, error, brew/steam buttons, LED, volumetric, tare, alt,
// autotune, PID/pump-model coeffs, pressure scale) is OPTIONAL: older or
// feature-less controllers legitimately lack them and every call site already
// null-checks before use.
struct LegacyClientChars {
    bool outputControl = false;
    bool sensor = false;
    bool ping = false;
    bool info = false;
};

// Comma-separated names of missing required characteristics; empty => compatible.
inline std::string legacyClientMissingRequiredChars(const LegacyClientChars &c) {
    std::string missing;
    const auto add = [&missing](const char *name) {
        if (!missing.empty()) {
            missing += ", ";
        }
        missing += name;
    };
    if (!c.outputControl) {
        add("outputControl");
    }
    if (!c.sensor) {
        add("sensor");
    }
    if (!c.ping) {
        add("ping");
    }
    if (!c.info) {
        add("info");
    }
    return missing;
}

inline bool legacyClientIsCompatible(const LegacyClientChars &c) { return legacyClientMissingRequiredChars(c).empty(); }

// After rejecting an incompatible controller, ignore its adverts for this long
// so the reject/rescan cycle cannot monopolise the radio (BLE scale scanning).
constexpr uint32_t LEGACY_CLIENT_INCOMPATIBLE_BACKOFF_MS = 30000;

// Wrap-safe: true once backoff has elapsed since the last rejection.
constexpr bool legacyClientRetryAllowed(bool incompatible, uint32_t nowMs, uint32_t rejectedAtMs) {
    return !incompatible || static_cast<uint32_t>(nowMs - rejectedAtMs) >= LEGACY_CLIENT_INCOMPATIBLE_BACKOFF_MS;
}
