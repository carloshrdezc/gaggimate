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

// After rejecting an incompatible controller, ignore ITS adverts for this long
// so the reject/rescan cycle cannot monopolise the radio (BLE scale scanning).
// Other controllers are never blocked.
constexpr uint32_t LEGACY_CLIENT_INCOMPATIBLE_BACKOFF_MS = 30000;

// Snapshot of the per-address rejection backoff. Plain value type: the owner
// publishes/reads it as ONE unit under a lock so address and timestamp can
// never be observed torn (PRO-669 review).
struct LegacyClientBackoff {
    bool active = false;
    uint64_t address = 0; // BLE address as 48-bit integer
    uint8_t addressType = 0;
    uint32_t rejectedAtMs = 0;
};

inline LegacyClientBackoff legacyClientBackoffReject(uint64_t address, uint8_t addressType, uint32_t nowMs) {
    LegacyClientBackoff b;
    b.active = true;
    b.address = address;
    b.addressType = addressType;
    b.rejectedAtMs = nowMs;
    return b;
}

// Wrap-safe: true once the window has elapsed since the rejection.
constexpr bool legacyClientBackoffExpired(const LegacyClientBackoff &b, uint32_t nowMs) {
    return !b.active || static_cast<uint32_t>(nowMs - b.rejectedAtMs) >= LEGACY_CLIENT_INCOMPATIBLE_BACKOFF_MS;
}

// Decide an advert. Returns true if it must be skipped (same rejected address,
// window still open). Clears `b` in place once the window has expired so a
// stale address is never kept around.
inline bool legacyClientAdvertBlocked(LegacyClientBackoff &b, uint64_t address, uint8_t addressType, uint32_t nowMs) {
    if (!b.active) {
        return false;
    }
    if (legacyClientBackoffExpired(b, nowMs)) {
        b = LegacyClientBackoff{};
        return false;
    }
    return b.address == address && b.addressType == addressType;
}

// PRO-669 round 2: fail closed while a rejected controller's link lingers
// (client->disconnect() failed or onDisconnect() never fired).

// The display must never see a rejected link as "connected": that suppresses
// the waiting-for-controller UI and would let callers treat it as live.
constexpr bool legacyClientShouldReportConnected(bool linkUp, bool incompatible) { return linkUp && !incompatible; }

// Every write to the controller is gated here: needs a live, accepted link and
// a cached characteristic (cleared on reject/disconnect).
constexpr bool legacyClientMaySendOutput(bool linkUp, bool incompatible, bool charPresent) {
    return legacyClientShouldReportConnected(linkUp, incompatible) && charPresent;
}

// Re-issue disconnect() for a rejected link that is still up, at most once per
// this interval. Wrap-safe; hasAttempted=false means "never tried" => retry now.
constexpr uint32_t LEGACY_CLIENT_DISCONNECT_RETRY_MS = 1000;
constexpr bool legacyClientShouldRetryDisconnect(bool linkUp, bool incompatible, bool hasAttempted, uint32_t nowMs,
                                                 uint32_t lastAttemptMs) {
    return linkUp && incompatible &&
           (!hasAttempted || static_cast<uint32_t>(nowMs - lastAttemptMs) >= LEGACY_CLIENT_DISCONNECT_RETRY_MS);
}
