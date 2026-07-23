#ifndef OTARESOLVEREUSEPOLICY_H
#define OTARESOLVEREUSEPOLICY_H

#include "OtaChannelSwitchPolicy.h" // otaCStrEq / otaStrEmpty
#include <cstddef>
#include <cstdint>

// PRO-556: pure, host-testable predicate deciding whether the WebUIPlugin
// forced-tag / channel-switch resolve task (otaResolveTask) may REUSE the
// periodic background OTA check's already-resolved result instead of opening a
// SECOND, independent HTTPS/TLS connection to GitHub.
//
// Background: `WebUIPlugin::otaResolveTask` (PRO-13) opens its own fresh
// `GitHubOTA::checkForUpdates()` TLS connection to resolve the selected
// channel's head whenever a channel switch / pinned-tag flash is confirmed.
// That duplicates the periodic background OTA check (PRO-411/PRO-555) which
// already runs `checkForUpdates()` on the SAME `GitHubOTA` instance roughly
// every 5 minutes and already has a resolved head version cached in
// `getCurrentVersion()`. PRO-554 (OtaResolveHeapPolicy.h) made the resolve
// path's second TLS handshake heap-safe; PRO-555 (OtaUpdateCheckPolicy.h) added
// a defer guard to the periodic check's TLS call. This is the architectural
// follow-up: avoid the redundant TLS round-trip altogether when a
// sufficiently-fresh, SAME-CHANNEL periodic result already exists.
//
// Header-only + free of any Arduino-String / FreeRTOS / `ota` / Settings /
// heap_caps dependency: the on-device caller (otaResolveTask) reads the cached
// periodic result (channel, version, failure flag, resolved-at timestamp) and
// the current millis() and passes them in as primitives + `const char*`. So the
// decision logic links on [env:native] via the existing `-I src` with the host
// String shim and needs no new build_src_filter entry — mirroring the
// OtaAsyncResolvePolicy.h / OtaResolveHeapPolicy.h / OtaUpdateCheckPolicy.h /
// OtaChannelSwitchPolicy.h precedent in this directory (deterministic function
// of its arguments, pinned by a compile-time static_assert truth table).

// ---------------------------------------------------------------------------
// Staleness window
// ---------------------------------------------------------------------------
//
// How recent a periodic-check result must be to be trustworthy enough to reuse
// for a click-driven resolve, instead of opening a fresh TLS handshake.
//
// Chosen as HALF the base periodic-check interval (UPDATE_CHECK_INTERVAL is
// 5 min = 300000 ms; half = 150000 ms = 2.5 min). Reasoning:
//   * The periodic check runs every UPDATE_CHECK_INTERVAL on success (PRO-411).
//     A result younger than half that interval is, by construction, from the
//     MOST RECENT scheduled check — there is no plausible newer head we would
//     have learned about by opening a fresh connection right now.
//   * It is deliberately well under a full interval so a result that is merely
//     "the previous cycle's, about to be refreshed" is NOT reused: confidence
//     in GitHub's head not having moved decays across the interval, and a
//     channel-switch / tag-pin flash is a high-stakes action where a fresh
//     confirmation is cheap insurance once the cached answer is no longer
//     obviously current.
//   * Under PRO-411 backoff a repeatedly-failing check can push the effective
//     interval out to 6 h; a 2.5-min window means we NEVER reuse a
//     backoff-delayed stale result (a 6-h-old answer is nowhere near fresh),
//     which is exactly the desired behavior — those are the low-confidence
//     cases the issue calls out.
//
// This is a documented default (see PR body), not an ambiguous choice: a
// smaller window trades more redundant TLS handshakes for fresher answers; a
// larger one trades staleness risk for fewer handshakes. Half the interval is
// the balance point where "reuse" means "the current scheduled cycle's result".
static constexpr uint32_t kOtaResolveReuseStalenessWindowMs = 150000u; // UPDATE_CHECK_INTERVAL / 2

// Elapsed-since-resolve freshness check. `resolvedAtMs` is the millis()
// timestamp stamped when the periodic check last SUCCEEDED (the loop's
// `lastUpdateCheck`); `nowMs` is the current millis(). Plain unsigned
// subtraction is deliberate — it stays correct across a millis() rollover
// (~49.7 days uptime), exactly like otaResolveTimedOut() in
// OtaAsyncResolvePolicy.h and every other millis()-delta in this firmware.
// Fresh iff strictly less than the window has elapsed (an exactly-window-old
// result is treated as too stale — inclusive on the stale side).
constexpr bool otaPeriodicResultFresh(uint32_t resolvedAtMs, uint32_t nowMs,
                                      uint32_t windowMs = kOtaResolveReuseStalenessWindowMs) {
    return (nowMs - resolvedAtMs) < windowMs;
}

// The reuse decision.
//
// Returns true when otaResolveTask MAY skip its own TLS handshake and reuse the
// periodic check's cached result directly. ALL of the following must hold:
//
//  - haveEverChecked:        the periodic check has produced at least one
//                            successful result (loop's lastUpdateCheck != 0).
//                            With no result at all there is nothing to reuse.
//  - !periodicResultFailed:  the last periodic check actually SUCCEEDED. A
//                            failed/deferred periodic check leaves a stale head
//                            in getCurrentVersion() and (PRO-555) does NOT
//                            advance lastUpdateCheck — reusing it would resolve
//                            against a stale or wrong answer. This is the
//                            explicit interaction with PRO-555's defer behavior:
//                            when the periodic check deferred, no fresh result
//                            exists and reuse is refused (fall back to the
//                            heap-guarded independent handshake).
//  - !empty(periodicVersion): the cached head is a real, non-empty version. An
//                            empty resolved version is never trustworthy (same
//                            rule as otaVersionValid / decideOtaFlash's empty
//                            guard).
//  - channels match:         the channel the periodic check last RESOLVED
//                            AGAINST equals the channel the user is now
//                            switching to / pinning (`resolveChannel`, latched
//                            at otaResolveTask spawn time). The periodic check
//                            resolves whatever `settings.getOTAChannel()` was at
//                            its run time, which may differ from the just-
//                            selected channel; a straight cache reuse without
//                            this check would resolve against the WRONG
//                            channel's head. Comparison is exact (channel
//                            strings are the persisted settings values, byte-for
//                            -byte identical when they refer to the same
//                            channel — e.g. "stable", "beta", "tag:2.0.8").
//  - fresh:                  the result is within the staleness window (above).
//
// When this returns false, otaResolveTask MUST fall back to its existing
// independent `checkForUpdates()` call, still protected by the PRO-554 heap
// guard (otaResolveHeapSufficient()) — this predicate never removes or weakens
// that fallback.
constexpr bool otaResolveCanReusePeriodic(bool haveEverChecked, bool periodicResultFailed, const char *periodicResolvedVersion,
                                          const char *periodicResultChannel, const char *resolveChannel,
                                          uint32_t periodicResolvedAtMs, uint32_t nowMs,
                                          uint32_t windowMs = kOtaResolveReuseStalenessWindowMs) {
    if (!haveEverChecked) {
        return false;
    }
    if (periodicResultFailed) {
        return false;
    }
    if (otaStrEmpty(periodicResolvedVersion)) {
        return false;
    }
    // A null/empty channel on either side can never be a confident match: the
    // periodic result must be known to belong to the same, concretely-named
    // channel we are resolving for.
    if (otaStrEmpty(periodicResultChannel) || otaStrEmpty(resolveChannel)) {
        return false;
    }
    if (!otaCStrEq(periodicResultChannel, resolveChannel)) {
        return false;
    }
    return otaPeriodicResultFresh(periodicResolvedAtMs, nowMs, windowMs);
}

// ---------------------------------------------------------------------------
// Compile-time truth table — pins the contract so a future edit to the window
// or the reuse conditions fails the firmware compile rather than silently
// changing the network-path decision (mirrors the OtaResolveHeapPolicy.h /
// OtaUpdateCheckPolicy.h / OtaChannelSwitchPolicy.h precedent in this dir).
// ---------------------------------------------------------------------------

static_assert(kOtaResolveReuseStalenessWindowMs == 150000u, "PRO-556: staleness window is UPDATE_CHECK_INTERVAL/2 (2.5 min)");

// Freshness boundary (strict-less-than on the fresh side).
static_assert(otaPeriodicResultFresh(0u, 0u), "PRO-556: zero elapsed is fresh");
static_assert(otaPeriodicResultFresh(0u, 149999u), "PRO-556: one ms under the window is fresh");
static_assert(!otaPeriodicResultFresh(0u, 150000u), "PRO-556: exactly the window is too stale");
static_assert(!otaPeriodicResultFresh(0u, 150001u), "PRO-556: past the window is too stale");
// millis() rollover: resolvedAt just before wraparound, now just after — the
// unsigned subtraction still yields the true (small) elapsed duration.
static_assert(otaPeriodicResultFresh(4294967295u, 4u), "PRO-556: freshness survives a millis() rollover");

// The full reuse decision. Common baseline: a fresh, successful, same-channel,
// non-empty result IS reusable.
static_assert(otaResolveCanReusePeriodic(true, false, "2.0.14", "stable", "stable", 0u, 1000u),
              "PRO-556: fresh same-channel successful result is reusable");
static_assert(otaResolveCanReusePeriodic(true, false, "2.0.8", "tag:2.0.8", "tag:2.0.8", 0u, 1000u),
              "PRO-556: a pinned-tag channel string also matches exactly");

// Never reuse when the periodic check never produced a result.
static_assert(!otaResolveCanReusePeriodic(false, false, "2.0.14", "stable", "stable", 0u, 1000u),
              "PRO-556: no periodic result ever -> cannot reuse");

// Never reuse a failed/deferred periodic result (PRO-555 interaction).
static_assert(!otaResolveCanReusePeriodic(true, true, "2.0.14", "stable", "stable", 0u, 1000u),
              "PRO-556: last periodic check failed/deferred -> cannot reuse (fall back to guarded handshake)");

// Never reuse an empty cached version.
static_assert(!otaResolveCanReusePeriodic(true, false, "", "stable", "stable", 0u, 1000u),
              "PRO-556: empty cached version -> cannot reuse");
static_assert(!otaResolveCanReusePeriodic(true, false, nullptr, "stable", "stable", 0u, 1000u),
              "PRO-556: null cached version -> cannot reuse");

// Never reuse across a channel mismatch — this is the core correctness guard.
static_assert(!otaResolveCanReusePeriodic(true, false, "2.0.14", "beta", "stable", 0u, 1000u),
              "PRO-556: periodic resolved a DIFFERENT channel -> cannot reuse");
static_assert(!otaResolveCanReusePeriodic(true, false, "2.0.14", "stable", "tag:2.0.14", 0u, 1000u),
              "PRO-556: channel vs pinned-tag are different channel strings -> cannot reuse");
static_assert(!otaResolveCanReusePeriodic(true, false, "2.0.14", "", "stable", 0u, 1000u),
              "PRO-556: empty periodic channel -> cannot reuse");
static_assert(!otaResolveCanReusePeriodic(true, false, "2.0.14", "stable", "", 0u, 1000u),
              "PRO-556: empty resolve channel -> cannot reuse");

// Never reuse a stale result even when everything else matches.
static_assert(!otaResolveCanReusePeriodic(true, false, "2.0.14", "stable", "stable", 0u, 150000u),
              "PRO-556: exactly-window-old same-channel result is too stale -> cannot reuse");
static_assert(!otaResolveCanReusePeriodic(true, false, "2.0.14", "stable", "stable", 0u, 6u * 60u * 60u * 1000u),
              "PRO-556: a 6h backoff-delayed result is far too stale -> cannot reuse");

#endif // OTARESOLVEREUSEPOLICY_H
