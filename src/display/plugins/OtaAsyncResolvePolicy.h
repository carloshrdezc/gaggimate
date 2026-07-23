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
// ReadyToFlash/Failed before Idle), so skipping the periodic check while state ==
// Resolving gives mutual exclusion on `ota` for the COMMON case with no runtime
// mutex around the instance.
//
// PRO-563 CAVEAT — this is "by construction" for a resolve that completes or is
// refused, but NOT unconditionally: per the PRO-13 point-7 tradeoff (WebUIPlugin.h),
// a resolve abandoned on the soft 10s TIMEOUT is never force-killed and its task
// can keep running ota->checkForUpdates() for a few more seconds AFTER state has
// already left Resolving (Failed for one tick, then Idle). During that residual
// window this predicate alone would wrongly let the periodic check run. loop()
// therefore does NOT call this predicate directly; it calls otaPeriodicCheckShouldSkip()
// below, which ORs this in with a bounded post-timeout grace window
// (otaPeriodicCheckShouldSkipForResolveDuringGrace) to cover exactly that residual
// case. The grace window — not a runtime mutex — is what closes the gap.
//
// Like PRO-555's DRAM defer, this SKIPS rather than fails: the caller must NOT
// bump otaCheckFailureCount (the check never ran — it is not a network failure
// and must not feed the PRO-411 backoff) and must NOT advance lastUpdateCheck
// (so the periodic check retries promptly on the next loop pass once the resolve
// completes and state leaves Resolving), matching the PRO-555 defer semantics.
constexpr bool otaPeriodicCheckShouldSkipForResolve(OtaResolveState resolveState) {
    return resolveState == OtaResolveState::Resolving;
}

// PRO-563: grace margin (ms) added past the resolve soft-timeout boundary during
// which the periodic check stays skipped even though the resolve state has left
// Resolving. See otaPeriodicCheckShouldSkipForResolveDuringGrace() below for why.
constexpr uint32_t kOtaResolveAbandonGraceMs = 5000;

// PRO-563: whether we are still inside the post-TIMEOUT grace window during which
// an ABANDONED resolve task may plausibly still be inside ota->checkForUpdates().
//
// Background (the residual PRO-560 gap): otaPeriodicCheckShouldSkipForResolve()
// above skips the periodic check exactly while state == Resolving. But per the
// PRO-13 design tradeoff (WebUIPlugin.h, "PRO-13 point 7"): when the resolve's
// soft 10s timeout fires, loop() transitions Resolving -> Failed and abandons the
// task WITHOUT force-killing it — the task keeps running its own
// ota->checkForUpdates() to completion in the background and its late result is
// merely dropped via the generation check (otaResolveResultIsCurrent). So for a
// few seconds AFTER the timeout, the abandoned task can STILL be touching the
// shared, non-reentrant `ota` instance while state has already moved to Failed
// (one loop tick) and then Idle — both states in which the plain skip predicate
// above returns false and would let the periodic check reopen the exact race
// PRO-560 closed, just in the narrow case where the resolve stalls to the full
// 10s timeout AND the ~5 min periodic interval also elapses in that immediate
// post-timeout window.
//
// This predicate closes that gap: after a TIMEOUT-driven abandonment
// (lastResolveTimedOut), keep skipping the periodic check until
// startMs + timeoutMs + graceMs has elapsed — i.e. until the abandoned task's own
// checkForUpdates() has plausibly finished. It keys off the SAME otaResolveStartMs
// / kOtaResolveTimeoutMs already used by otaResolveTimedOut(), plus a grace margin.
// Note this is deliberately NOT gated on a particular resolve state: after the
// timeout, loop() flips Failed -> Idle within one tick, so the grace must span
// both. It IS gated on lastResolveTimedOut so a NON-timeout Failed (an immediate
// refuse whose task already posted its result and exited) is not needlessly
// skipped. loop() clears lastResolveTimedOut at each fresh spawn, so a new resolve
// starts the window over; the unsigned (nowMs - startMs) subtraction stays correct
// across a millis() rollover exactly like otaResolveTimedOut() above.
constexpr bool otaPeriodicCheckShouldSkipForResolveDuringGrace(bool lastResolveTimedOut, uint32_t startMs, uint32_t nowMs,
                                                               uint32_t timeoutMs, uint32_t graceMs) {
    return lastResolveTimedOut && (nowMs - startMs) < (timeoutMs + graceMs);
}

// PRO-563: the full periodic-check skip decision — skip while a resolve is in
// flight (PRO-560) OR while inside the post-timeout abandon grace window (this
// change). Combines the two above so loop() has a single call site.
constexpr bool otaPeriodicCheckShouldSkip(OtaResolveState resolveState, bool lastResolveTimedOut, uint32_t startMs,
                                          uint32_t nowMs, uint32_t timeoutMs, uint32_t graceMs) {
    return otaPeriodicCheckShouldSkipForResolve(resolveState) ||
           otaPeriodicCheckShouldSkipForResolveDuringGrace(lastResolveTimedOut, startMs, nowMs, timeoutMs, graceMs);
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
              "PRO-560: a settled (Failed) resolve no longer touches `ota` -> periodic check runs "
              "(PRO-563 narrows this: a TIMEOUT-driven Failed may still be touching `ota` — see the "
              "grace-window predicate below, which loop() ORs in via otaPeriodicCheckShouldSkip())");

// PRO-563: the post-timeout abandon grace window. lastResolveTimedOut gates it,
// so an immediate (non-timeout) refuse is never needlessly skipped; the timing
// window bounds it, so once startMs + timeoutMs + graceMs elapses the periodic
// check runs again even though lastResolveTimedOut stays latched until the next spawn.
static_assert(!otaPeriodicCheckShouldSkipForResolveDuringGrace(/*lastResolveTimedOut=*/false, 0, 10000, 10000, 5000),
              "PRO-563: a non-timeout resolve never enters the abandon grace window");
static_assert(otaPeriodicCheckShouldSkipForResolveDuringGrace(/*lastResolveTimedOut=*/true, 0, 10000, 10000, 5000),
              "PRO-563: right at the timeout boundary, the abandon grace window is active");
static_assert(otaPeriodicCheckShouldSkipForResolveDuringGrace(/*lastResolveTimedOut=*/true, 0, 14999, 10000, 5000),
              "PRO-563: just before the grace window closes, the periodic check is still skipped");
static_assert(!otaPeriodicCheckShouldSkipForResolveDuringGrace(/*lastResolveTimedOut=*/true, 0, 15000, 10000, 5000),
              "PRO-563: once startMs+timeout+grace has elapsed, the abandon grace window has closed");
// millis() rollover: startMs just before wraparound, nowMs just after -> small true elapsed, window still open.
static_assert(otaPeriodicCheckShouldSkipForResolveDuringGrace(/*lastResolveTimedOut=*/true, 4294967295u, 5u, 10000, 5000),
              "PRO-563: the abandon grace window survives a millis() rollover without a false early close");

// PRO-563: the combined skip decision loop() uses. Skips while Resolving (PRO-560)
// OR while inside the post-timeout abandon grace window, regardless of state.
static_assert(otaPeriodicCheckShouldSkip(OtaResolveState::Resolving, /*lastResolveTimedOut=*/false, 0, 0, 10000, 5000),
              "PRO-563: an in-flight resolve is skipped (PRO-560 mutual exclusion) regardless of the grace window");
static_assert(otaPeriodicCheckShouldSkip(OtaResolveState::Failed, /*lastResolveTimedOut=*/true, 0, 12000, 10000, 5000),
              "PRO-563: a just-timed-out (Failed) resolve is skipped while the abandoned task may still touch `ota`");
static_assert(otaPeriodicCheckShouldSkip(OtaResolveState::Idle, /*lastResolveTimedOut=*/true, 0, 12000, 10000, 5000),
              "PRO-563: the grace window still skips after Failed->Idle flips within one tick post-timeout");
static_assert(!otaPeriodicCheckShouldSkip(OtaResolveState::Idle, /*lastResolveTimedOut=*/true, 0, 15000, 10000, 5000),
              "PRO-563: once the grace window closes, an Idle state lets the periodic check run again");
static_assert(!otaPeriodicCheckShouldSkip(OtaResolveState::Idle, /*lastResolveTimedOut=*/false, 0, 0, 10000, 5000),
              "PRO-563: no resolve pending and no timeout -> periodic check runs");
static_assert(!otaPeriodicCheckShouldSkip(OtaResolveState::Failed, /*lastResolveTimedOut=*/false, 0, 0, 10000, 5000),
              "PRO-563: a non-timeout (immediate refuse) Failed -> periodic check runs, no grace needed");

#endif // OTAASYNCRESOLVEPOLICY_H
