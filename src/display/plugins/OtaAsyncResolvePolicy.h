#ifndef OTAASYNCRESOLVEPOLICY_H
#define OTAASYNCRESOLVEPOLICY_H

#include "OtaChannelSwitchPolicy.h"
#include <cstdint>

// PRO-13: pure, host-testable state machine + timing/staleness guards for the
// async resolve of the WebUIPlugin forced-tag/channel-switch OTA path.
//
// Background: loop()'s `if (updating)` block previously called
// `ota->checkForUpdates()` SYNCHRONOUSLY on the Arduino main loop task before
// running `decideOtaFlash()` (OtaChannelSwitchPolicy.h) whenever the user
// pinned a tag or switched channels. `checkForUpdates()` does a blocking
// HTTPS GET (+ redirect chase) against api.github.com/github.com that can
// take 1-5s+ on a slow/congested link, stalling everything else in loop()
// including the 200ms evt:status broadcast.
//
// The fix hoists the resolve (`checkForUpdates()` + `decideOtaFlash()`) into
// a one-shot FreeRTOS task, mirroring the existing OtaIntentState.h
// post/drain-under-mutex idiom. This header holds only the DECISION logic
// that drives that hoist:
//   - the four-state machine (Idle / Resolving / ReadyToFlash / Failed)
//   - the state a resolve decision (from decideOtaFlash) transitions to
//   - the soft-timeout boundary check (millis()-based, matching the
//     BeanManager / rest-of-firmware millis() convention, NOT Unix time)
//   - the generation-counter staleness guard, so a late resolve-task result
//     arriving after loop() has already abandoned it (timeout, or a fresh
//     resolve was kicked off) is recognized and dropped rather than acted on.
//
// Header-only + free of any Arduino-String method / FreeRTOS / Settings /
// `ota`, mirroring the OtaChannelSwitchPolicy.h / OtaUpdateCheckPolicy.h /
// OtaIntentState.h precedent in this directory: every function here is a
// deterministic function of its arguments, so it links on [env:native] via
// the existing `-I src` and needs no new build_src_filter entry (header-only).
// The FreeRTOS task, the mutex-guarded result handoff, and the `ota`/Settings
// glue stay in WebUIPlugin.cpp/.h, exactly like OtaIntentState.h's contract.

enum class OtaResolveState : uint8_t { Idle, Resolving, ReadyToFlash, Failed };

// A resolve task's decision (from decideOtaFlash) maps to exactly two
// outcomes here: Refuse means we could not confirm what would be flashed
// (tag mismatch, or a switch whose resolve failed/came back empty) -> Failed.
// Every other decision (ForceMatchTag, ForceChannelSwitch, and — though the
// async resolve path never actually produces it, since it is only invoked
// for a genuine tag pin or channel switch — UpgradeOnly) means the resolve
// confirmed enough to proceed -> ReadyToFlash.
constexpr OtaResolveState otaResolveStateForDecision(OtaFlashDecision decision) {
    return decision == OtaFlashDecision::Refuse ? OtaResolveState::Failed : OtaResolveState::ReadyToFlash;
}

// Soft 10s timeout on the in-flight resolve (PRO-13 acceptance criteria):
// abandon waiting once `nowMs - startMs >= timeoutMs`. Plain unsigned
// subtraction is deliberate: it stays correct across a millis() rollover
// (~49.7 days uptime) the same way every other millis()-delta comparison in
// this firmware does (see BeanManager), because unsigned wraparound makes
// `nowMs - startMs` compute the correct elapsed duration even when nowMs has
// wrapped past startMs.
// PRO-13 fix: these are millis()-flavored uint32_t, not `unsigned long`.
// `unsigned long` is 32-bit on the ESP32 Arduino target (matching millis()'s
// real 32-bit rollover) but 64-bit on the [env:native] host test target, so
// the rollover static_assert below (0xFFFFFFFF wrapping to 5) only holds if
// the subtraction is explicitly 32-bit-wide on BOTH platforms. Mirrors
// OtaUpdateCheckPolicy.h's otaBackoffInterval, which already uses uint32_t
// for the same millis()-math reason.
constexpr bool otaResolveTimedOut(uint32_t startMs, uint32_t nowMs, uint32_t timeoutMs) { return (nowMs - startMs) >= timeoutMs; }

// Staleness guard: a resolve task stamps the generation it was handed at
// spawn time onto its posted result. loop() bumps its own generation counter
// both when it spawns a fresh resolve task AND when it abandons one on
// timeout, so any result a task posts after being abandoned (or after a
// newer resolve superseded it) carries a generation that no longer matches
// loop()'s current one and must be dropped rather than acted upon — the task
// itself is still let run to completion (never vTaskDelete'd mid-flight);
// only its result is ignored.
constexpr bool otaResolveResultIsCurrent(uint32_t resultGeneration, uint32_t currentGeneration) {
    return resultGeneration == currentGeneration;
}

// PRO-560: whether loop()'s periodic background OTA check must be SKIPPED this
// pass because a click-driven resolve task is currently in flight.
//
// Background: the periodic check (WebUIPlugin::loop, PRO-411/PRO-555) and the
// forced-tag/channel-switch resolve task (otaResolveTask, PRO-13) both call
// ota->checkForUpdates() on the SAME shared GitHubOTA instance — one on the
// loop task, one on the resolve task. checkForUpdates() (and the WiFiClientSecure
// / mbedtls connection it drives) is NOT re-entrant/atomic. PRO-556 narrowed the
// window (a reuse-hit never touches `ota` from the resolve task), but the
// non-reuse FALLBACK path still opens its own handshake; if the ~5 min periodic
// interval elapses while a resolve is mid-flight, BOTH tasks can be inside
// checkForUpdates() on the same instance at once. This is the long-standing
// PRO-13/PRO-411 interaction race, not a regression from any single prior PR.
//
// otaResolveState is loop-task-owned (only loop() reads/writes it, transitioning
// Idle -> Resolving -> ReadyToFlash/Failed -> Idle), so loop() can read it here
// with no new mutex. A resolve task only touches `ota` while the state is
// Resolving (it is spawned as loop() sets Resolving and is drained back to
// ReadyToFlash/Failed before Idle), so skipping the periodic check exactly while
// state == Resolving gives mutual exclusion on `ota` BY CONSTRUCTION — no runtime
// mutex around the instance is needed.
//
// Like PRO-555's DRAM defer, this SKIPS rather than fails: the caller must NOT
// bump otaCheckFailureCount (the check never ran — it is not a network failure
// and must not feed the PRO-411 backoff) and must NOT advance lastUpdateCheck
// (so the periodic check retries promptly on the next loop pass once the resolve
// completes and state leaves Resolving), matching the PRO-555 defer semantics.
constexpr bool otaPeriodicCheckShouldSkipForResolve(OtaResolveState resolveState) {
    return resolveState == OtaResolveState::Resolving;
}

// Compile-time truth table — pins the contract so a future edit to the
// predicate fails the firmware compile rather than silently changing the
// async-resolve state machine (mirrors the OtaChannelSwitchPolicy.h /
// OtaUpdateCheckPolicy.h precedent in this directory).
static_assert(otaResolveStateForDecision(OtaFlashDecision::ForceMatchTag) == OtaResolveState::ReadyToFlash,
              "PRO-13: a confirmed tag match is ready to flash");
static_assert(otaResolveStateForDecision(OtaFlashDecision::ForceChannelSwitch) == OtaResolveState::ReadyToFlash,
              "PRO-13: a confirmed channel switch is ready to flash");
static_assert(otaResolveStateForDecision(OtaFlashDecision::UpgradeOnly) == OtaResolveState::ReadyToFlash,
              "PRO-13: UpgradeOnly is ready to flash (the async resolve path never actually produces this)");
static_assert(otaResolveStateForDecision(OtaFlashDecision::Refuse) == OtaResolveState::Failed,
              "PRO-13: an unconfirmed resolve refuses -> failed, never flashed");

static_assert(!otaResolveTimedOut(0, 9999, 10000), "PRO-13: 9999ms elapsed of a 10000ms budget has not timed out");
static_assert(otaResolveTimedOut(0, 10000, 10000), "PRO-13: exactly the timeout boundary counts as timed out");
static_assert(otaResolveTimedOut(0, 10001, 10000), "PRO-13: past the timeout boundary counts as timed out");
static_assert(!otaResolveTimedOut(1000, 1000, 10000), "PRO-13: zero elapsed has not timed out");
// millis() rollover: startMs just before wraparound, nowMs just after — the
// unsigned subtraction still yields the true (small) elapsed duration.
static_assert(!otaResolveTimedOut(4294967295u, 5u, 10000),
              "PRO-13: elapsed survives a millis() rollover without a false timeout");

static_assert(otaResolveResultIsCurrent(3, 3), "PRO-13: matching generations are current");
static_assert(!otaResolveResultIsCurrent(2, 3), "PRO-13: a stale (older) generation is not current");
static_assert(!otaResolveResultIsCurrent(4, 3), "PRO-13: a mismatched (never-current) generation is not current");

// PRO-560: the periodic check is skipped IFF a resolve is in flight (Resolving),
// giving mutual exclusion on the shared `ota` instance. Every terminal/settled
// state (Idle, ReadyToFlash, Failed) leaves the periodic check free to run — a
// resolve task only touches `ota` during Resolving.
static_assert(otaPeriodicCheckShouldSkipForResolve(OtaResolveState::Resolving),
              "PRO-560: skip the periodic check while a resolve is in flight (mutual exclusion on `ota`)");
static_assert(!otaPeriodicCheckShouldSkipForResolve(OtaResolveState::Idle),
              "PRO-560: no resolve in flight -> periodic check runs");
static_assert(!otaPeriodicCheckShouldSkipForResolve(OtaResolveState::ReadyToFlash),
              "PRO-560: a settled (ReadyToFlash) resolve no longer touches `ota` -> periodic check runs");
static_assert(!otaPeriodicCheckShouldSkipForResolve(OtaResolveState::Failed),
              "PRO-560: a settled (Failed) resolve no longer touches `ota` -> periodic check runs");

#endif // OTAASYNCRESOLVEPOLICY_H
