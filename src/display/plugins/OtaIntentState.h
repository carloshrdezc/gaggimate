#ifndef OTAINTENTSTATE_H
#define OTAINTENTSTATE_H

#include <cstddef>
#include <string>

// PRO-11: pure, host-testable extraction of the WebUIPlugin OTA-intent state
// machine (component selection + deferred-intent coalescing + channel->URL
// resolution) introduced by CAR-178 (release URL) and CAR-377 (OTA start).
// WebUIPlugin.cpp is NOT in the [env:native] build_src_filter, so before this
// extraction none of this logic had any host test coverage.
//
// This header holds ONLY branch-free-ish decision logic: no FreeRTOS, no
// Arduino `String`, no `ota`/`Settings`. The mutex (`otaIntentMutex`) and the
// `ota`/`Settings` glue stay in WebUIPlugin.cpp/.h: WebUIPlugin.cpp calls
// these pure functions WHILE ALREADY HOLDING otaIntentMutex, passing plain
// values in and assigning the returned values back onto its own
// mutex-guarded fields. The header itself never touches a mutex, a String, or
// FreeRTOS — every function here is a deterministic function of its
// arguments. This is a pure refactor: every function reproduces the exact
// pre-existing inline behavior, just made host-compilable — no semantic
// change.
//
// Mirrors the established pattern in this directory: see
// BLEScaleScanPolicy.h / OtaChannelSwitchPolicy.h / OtaUpdateCheckPolicy.h.

// ---------------------------------------------------------------------------
// Component selection: the `cp` request field -> which components
// ota->update() should flash (CAR-377). Empty/absent component means "full
// update" (both components) — the safe superset default.
// ---------------------------------------------------------------------------

struct OtaComponentSelection {
    bool updateController;
    bool updateDisplay;
};

// constexpr C-string equality (gnu++17 std::strcmp is not constexpr). Null is
// treated as empty, mirroring OtaChannelSwitchPolicy.h's otaCStrEq.
constexpr bool otaIntentCStrEq(const char *a, const char *b) {
    const char *aa = a ? a : "";
    const char *bb = b ? b : "";
    while (*aa != '\0' && *aa == *bb) {
        ++aa;
        ++bb;
    }
    return *aa == *bb;
}

// Mirrors the inline mapping previously at WebUIPlugin.cpp:
//   ota->update(updateComponent != "display", updateComponent != "controller", force)
// i.e. requesting "display" flashes the display only (not the controller),
// requesting "controller" flashes the controller only (not the display), and
// anything else (including an empty/absent component, e.g. the contended
// OTA-start handoff that could only raise the flag) flashes both.
constexpr OtaComponentSelection selectOtaComponents(const char *component) {
    return OtaComponentSelection{/*updateController=*/!otaIntentCStrEq(component, "display"),
                                 /*updateDisplay=*/!otaIntentCStrEq(component, "controller")};
}

// Compile-time truth table — pins the contract so a future edit to the
// mapping fails the firmware compile rather than silently changing which
// component(s) get flashed (mirrors OtaChannelSwitchPolicy.h's precedent).
static_assert(selectOtaComponents("display").updateController == false, "PRO-11: cp=display does not flash the controller");
static_assert(selectOtaComponents("display").updateDisplay == true, "PRO-11: cp=display still flashes the display");
static_assert(selectOtaComponents("controller").updateController == true, "PRO-11: cp=controller still flashes the controller");
static_assert(selectOtaComponents("controller").updateDisplay == false, "PRO-11: cp=controller does not flash the display");
static_assert(selectOtaComponents(nullptr).updateController == true, "PRO-11: absent cp flashes both (full update default)");
static_assert(selectOtaComponents(nullptr).updateDisplay == true, "PRO-11: absent cp flashes both (full update default)");
static_assert(selectOtaComponents("").updateController == true, "PRO-11: empty cp flashes both (full update default)");
static_assert(selectOtaComponents("").updateDisplay == true, "PRO-11: empty cp flashes both (full update default)");

// ---------------------------------------------------------------------------
// Deferred single-in-flight intent slot (CAR-178 / CAR-377): a WS/relay-task
// handler posts an intent (optionally carrying a string payload) while
// holding otaIntentMutex; the loop task later drains and clears it while
// holding the same mutex. The functions below are the pure VALUE semantics of
// what happens inside those critical sections — WebUIPlugin.cpp still owns
// the actual storage (its existing `pendingUpdateComponent`/`pendingOtaStart`
// and `pendingReleaseUrl`/`pendingReleaseUrlChange` fields) and the mutex;
// these functions just compute what the new field values should become, so
// the decision logic is testable without any FreeRTOS dependency.
//
// Semantics preserved exactly from the inline WebUIPlugin.cpp handoff:
//   * single in-flight slot (no queue) — at most one pending intent at a time.
//   * last-writer-wins coalescing: postOtaDeferredIntent() unconditionally
//     overwrites the payload and raises the flag, with no dependency on
//     whatever was previously latched. So two successful posts before a
//     drain coalesce into one pending intent carrying only the LATEST
//     payload — calling it twice and keeping only the final result IS the
//     coalescing behavior (there is no merge step to lose one for the other).
//   * clear-on-latch: draining resets both the flag and the payload (modeled
//     by drainOtaDeferredIntent() returning an empty payload whenever
//     hadPending is false, matching what a freshly-cleared slot reads back
//     as) so a stale payload can never leak into the next cycle.
//   * contended fallback ("raise flag without payload -> safe default"): if
//     the caller could not take the mutex, it may ONLY raise the flag
//     (postOtaDeferredIntentFlagOnly()) — it never had a guarded read of the
//     current payload, so it must not touch it. The eventual drain then sees
//     whatever payload was already latched: empty for a slot that was never
//     successfully posted. Downstream logic (selectOtaComponents /
//     resolveOtaReleaseUrl) treats an empty payload as its safe default
//     (full update / re-resolve the release URL from settings).
// ---------------------------------------------------------------------------

struct OtaDeferredStringIntent {
    bool pending;
    std::string payload;
};

// Successful post: unconditionally overwrite payload and raise the flag.
// Calling this twice before a drain and keeping only the second result is the
// last-writer-wins coalescing behavior — the first call's payload is never
// merged in and is simply superseded.
inline OtaDeferredStringIntent postOtaDeferredIntent(const std::string &payload) {
    return OtaDeferredStringIntent{true, payload};
}

// Contended fallback: the caller never got a guarded read of the current
// payload, so it raises the flag WITHOUT supplying a payload. Callers must
// assign only the `.pending` field back onto their own flag and leave their
// existing payload field untouched (mirrors the original inline code, which
// never wrote pendingUpdateComponent / pendingReleaseUrl on this path).
inline OtaDeferredStringIntent postOtaDeferredIntentFlagOnly() { return OtaDeferredStringIntent{true, std::string()}; }

struct OtaDeferredDrainResult {
    bool hadPending;
    std::string payload;
};

// Drain-and-clear: given the currently-latched (flag, payload), produce what
// the caller should act on AND what the slot should read back as once
// cleared. When hadPending is false the payload comes back empty, matching a
// freshly-cleared (never-posted) slot; when true, the payload is exactly what
// was latched (empty if this was a flag-only contended post).
inline OtaDeferredDrainResult drainOtaDeferredIntent(bool pendingFlag, const std::string &payload) {
    return OtaDeferredDrainResult{pendingFlag, pendingFlag ? payload : std::string()};
}

// ---------------------------------------------------------------------------
// Channel -> release-URL resolution (CAR-178). Mirrors the file-local statics
// `resolveReleaseUrl` / `normalizeChannel` previously defined in
// WebUIPlugin.cpp, made pure by taking the release-URL base and the stable-tag
// allow-list as parameters instead of reading the WebUIPlugin.h globals
// (`RELEASE_URL`, `STABLE_VERSIONS`) directly.
//
// Channel semantics (preserved exactly):
//   "beta"          -> moving tag tracking the master branch: <base>tag/beta
//   "nightly"       -> moving tag: <base>tag/nightly
//   "tag:<semver>"  -> pinned release, ONLY if <semver> is present in the
//                      stable-tag allow-list: <base>tag/<semver>
//   anything else (including an unrecognized/invalid tag: pin) -> <base>latest
// ---------------------------------------------------------------------------

inline bool otaIntentIsAllowedStableTag(const std::string &tag, const char *const *stableVersions, size_t stableVersionsCount) {
    for (size_t i = 0; i < stableVersionsCount; ++i) {
        if (stableVersions[i] != nullptr && tag == stableVersions[i]) {
            return true;
        }
    }
    return false;
}

// Normalize an incoming channel string to the value persisted in settings.
// Unknown values (including a tag: pin not in the allow-list) fall back to
// "latest" so a malformed websocket payload can never poison the stored
// setting.
inline std::string normalizeOtaChannel(const std::string &channel, const char *const *stableVersions,
                                       size_t stableVersionsCount) {
    if (channel == "beta") {
        return "beta";
    }
    if (channel == "nightly") {
        return "nightly";
    }
    if (channel.rfind("tag:", 0) == 0) {
        const std::string tag = channel.substr(4);
        if (otaIntentIsAllowedStableTag(tag, stableVersions, stableVersionsCount)) {
            return channel;
        }
    }
    return "latest";
}

// Resolve a stored/normalized OTA channel string to the GitHub release URL.
inline std::string resolveOtaReleaseUrl(const std::string &channel, const std::string &releaseUrlBase,
                                        const char *const *stableVersions, size_t stableVersionsCount) {
    if (channel == "beta") {
        return releaseUrlBase + "tag/beta";
    }
    if (channel == "nightly") {
        return releaseUrlBase + "tag/nightly";
    }
    if (channel.rfind("tag:", 0) == 0) {
        const std::string tag = channel.substr(4);
        if (otaIntentIsAllowedStableTag(tag, stableVersions, stableVersionsCount)) {
            return releaseUrlBase + "tag/" + tag;
        }
    }
    return releaseUrlBase + "latest";
}

#endif // OTAINTENTSTATE_H
