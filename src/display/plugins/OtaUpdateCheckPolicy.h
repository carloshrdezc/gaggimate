#ifndef OTAUPDATECHECKPOLICY_H
#define OTAUPDATECHECKPOLICY_H

#include <cstddef>
#include <cstdint>

// PRO-411: pure, host-testable guards + backoff math for the periodic GitHub
// OTA update-check path.
//
// Background: the periodic update-check (WebUIPlugin::loop -> GitHubOTA::
// checkForUpdates -> get_updated_base_url_via_redirect -> get_redirect_location)
// opens a fresh TLS connection to github.com every 5 minutes. A failure in that
// path (TLS connect failure, missing/empty redirect Location header, HTTP error,
// transient WiFi loss) previously let an empty/uninitialised String flow into
// the base-url derivation and downstream WiFi/TLS APIs, and hammered github.com
// on a fixed 5-minute cadence forever. On-device this manifested as an
// IllegalInstruction crash in the async_tcp/WiFiClientSecure connect path with a
// NULL String::c_str() deref.
//
// The String-typed HTTPS helpers in lib/OTA/src/common.cpp are too coupled to
// WiFiClientSecure to unit-test directly, so the *decision* logic is extracted
// here: whether a redirect Location / version string is usable, and how far to
// back off after N consecutive failures. common.cpp / GitHubOTA.cpp call these;
// the tests cover them against empty/malformed inputs.
//
// Header-only + free of any Arduino-String method (const char* + integers only),
// mirroring the OtaChannelSwitchPolicy.h / ChangeModeDeferPolicy.h precedent in
// this directory: it links on [env:native] via the existing `-I src` with the
// host String shim, and no new OTA .cpp is added to the native allow-list.

// A redirect Location is usable only if it is non-null and non-empty. An empty
// or null Location is exactly the crash vector (an uninitialised/empty String
// deref in the WiFi/TLS connect path), so callers must NOT derive a base_url or
// call any WiFi/TLS API from it — bail and let the caller back off.
constexpr bool otaRedirectLocationValid(const char *location) { return location != nullptr && location[0] != '\0'; }

// A resolved version string is usable only if non-null and non-empty. Mirrors
// the version.txt guard: an empty result means "resolve produced nothing" and
// must be treated as a failed check, never parsed into a semver_t.
constexpr bool otaVersionValid(const char *version) { return version != nullptr && version[0] != '\0'; }

// Whether a version.txt payload carries a leading `v`/`V` that must be stripped
// before semver parsing (mirrors GitHubOTA::checkForUpdates version handling).
// Null/empty is treated as "no prefix".
constexpr bool otaVersionHasLeadingV(const char *version) {
    return version != nullptr && (version[0] == 'v' || version[0] == 'V');
}

// Backoff for the periodic OTA update-check (PRO-411).
//
// A repeatedly-failing check (github.com unreachable, TLS handshake failing,
// transient WiFi loss) must not keep opening a fresh TLS connection every
// baseInterval ms forever — that both hammers github.com and gives the crash
// path ~288 chances/night to fire. On each consecutive failure the effective
// interval doubles (exponential backoff), capped at maxInterval; a success
// resets the failure count to 0 and the interval back to baseInterval.
//
// Pure integer math (no clock, no side effects) so it is identical on host and
// device and trivially unit-testable. `consecutiveFailures` is the count of
// failures BEFORE the current wait (0 => first check / just-succeeded => wait
// baseInterval). The shift is clamped so it can never overflow or exceed the
// cap regardless of how large the failure count grows.
constexpr uint32_t otaBackoffInterval(uint32_t baseInterval, uint32_t maxInterval, uint32_t consecutiveFailures) {
    if (consecutiveFailures == 0) {
        return baseInterval;
    }
    if (baseInterval >= maxInterval) {
        return maxInterval;
    }
    // Cap the shift so `baseInterval << shift` cannot overflow a uint32_t and so
    // we stop multiplying well before the cap is reached. 31 shifts is already
    // far beyond any realistic cap; the min() below clamps to maxInterval.
    uint32_t shift = consecutiveFailures - 1;
    if (shift > 31) {
        shift = 31;
    }
    uint64_t scaled = static_cast<uint64_t>(baseInterval) << shift;
    if (scaled >= static_cast<uint64_t>(maxInterval)) {
        return maxInterval;
    }
    return static_cast<uint32_t>(scaled);
}

// Compile-time truth table — pins the contract so a future edit fails the
// firmware compile rather than silently changing the guard/backoff behavior.
static_assert(!otaRedirectLocationValid(nullptr), "PRO-411: null redirect location is invalid");
static_assert(!otaRedirectLocationValid(""), "PRO-411: empty redirect location is invalid");
static_assert(otaRedirectLocationValid("https://github.com/x/y/releases/tag/2.0.0"),
              "PRO-411: non-empty redirect location is valid");

static_assert(!otaVersionValid(nullptr), "PRO-411: null version is invalid");
static_assert(!otaVersionValid(""), "PRO-411: empty version is invalid");
static_assert(otaVersionValid("2.0.14"), "PRO-411: non-empty version is valid");

static_assert(!otaVersionHasLeadingV(nullptr), "PRO-411: null has no leading v");
static_assert(!otaVersionHasLeadingV(""), "PRO-411: empty has no leading v");
static_assert(otaVersionHasLeadingV("v2.0.0"), "PRO-411: lowercase v prefix detected");
static_assert(otaVersionHasLeadingV("V2.0.0"), "PRO-411: uppercase V prefix detected");
static_assert(!otaVersionHasLeadingV("2.0.0"), "PRO-411: no prefix");

// Backoff: 0 failures -> base; grows x2 per failure; capped at max.
static_assert(otaBackoffInterval(300000, 3600000, 0) == 300000, "PRO-411: 0 failures -> base interval");
static_assert(otaBackoffInterval(300000, 3600000, 1) == 300000, "PRO-411: 1 failure -> base interval (2^0)");
static_assert(otaBackoffInterval(300000, 3600000, 2) == 600000, "PRO-411: 2 failures -> 2x base");
static_assert(otaBackoffInterval(300000, 3600000, 3) == 1200000, "PRO-411: 3 failures -> 4x base");
static_assert(otaBackoffInterval(300000, 3600000, 4) == 2400000, "PRO-411: 4 failures -> 8x base");
static_assert(otaBackoffInterval(300000, 3600000, 5) == 3600000, "PRO-411: 5 failures -> capped at max");
static_assert(otaBackoffInterval(300000, 3600000, 100) == 3600000, "PRO-411: many failures stay capped, no overflow");
static_assert(otaBackoffInterval(300000, 3600000, 0xFFFFFFFF) == 3600000, "PRO-411: extreme failure count stays capped");
static_assert(otaBackoffInterval(500, 500, 10) == 500, "PRO-411: base==max always returns cap");

#endif // OTAUPDATECHECKPOLICY_H
