#ifndef STANDBYREASSERTPOLICY_H
#define STANDBYREASSERTPOLICY_H

#include "../core/constants.h"
#include <cstdint>

// PRO-421: an explicit Standby must win over a stale, near-immediate re-assert
// of a non-Standby mode over the WebSocket `req:change-mode` path.
//
// Confirmed live on the device (nightly-231 / dev-master): pressing the web-UI
// "Stop Steam" button while auto-steam is enabled sends `req:change-mode`
// STANDBY, the firmware lands in Standby, and then the web dashboard's
// auto-steam effect reflexively re-fires `req:change-mode` STEAM ~150 ms later
// (its `lastActiveWasBrewRef` was latched true during the auto-steamed session).
// The firmware faithfully applies that STEAM request and the machine bounces
// straight back to Steam; a second Standby is needed to make it stick. The web
// side is fixed separately, but the firmware is the authoritative layer and must
// not let a stale re-assert override an explicit user stop — the same principle
// PRO-391 applied to the physical latching steam switch (a held level does not
// re-assert Steam after an explicit Standby), lifted here to the mode-change
// layer so it also defeats the web / UI / auto-steam re-assert path.
//
// The guard is intentionally narrow:
//   * It only ever suppresses a NON-STANDBY target (a STANDBY request always
//     applies — you can never get stuck out of Standby).
//   * It only fires within a short window (STANDBY_REASSERT_GUARD_MS) after the
//     last explicit STANDBY `req:change-mode`. A reflexive/programmatic re-fire
//     lands inside this window; a deliberate human re-press (re-entering Steam a
//     moment later) lands well outside it and still works — the switch/mode
//     never wedges.
//   * The physical latching steam switch is already handled by SteamButtonPolicy
//     (edge-gated, PRO-391) on a separate code path and is unaffected here: this
//     predicate governs only the WebUIPlugin `req:change-mode` handler.
//
// Pure and header-only so it is host-testable in [env:native] without linking
// Controller/BLE/LVGL/FreeRTOS. Only the lightweight `constants.h` (pure
// `#define`s) is pulled in for MODE_STANDBY.

// Guard window (ms) after an explicit STANDBY `req:change-mode` during which a
// non-STANDBY `req:change-mode` is treated as a stale re-assert and suppressed.
// Sized to absorb the observed reflexive re-fire (~150 ms, plus WS round-trip
// and render latency) while staying short enough that a deliberate human
// re-press is never blocked.
#ifndef STANDBY_REASSERT_GUARD_MS
#define STANDBY_REASSERT_GUARD_MS 1000UL
#endif

// True when a `req:change-mode` to `newMode` should be SUPPRESSED as a stale
// re-assert of a non-Standby mode arriving right after an explicit Standby.
//
// Inputs:
//  - newMode:                 the requested target mode (MODE_* from constants.h).
//  - msSinceExplicitStandby:  elapsed ms since the last explicit STANDBY
//                             `req:change-mode` was applied. Callers that have
//                             never seen an explicit STANDBY pass a value >=
//                             STANDBY_REASSERT_GUARD_MS (e.g. the guard window
//                             itself) so the predicate is false.
//
// Contract:
//   * A STANDBY target is NEVER suppressed (you can always stop the machine).
//   * A non-STANDBY target is suppressed IFF it arrives strictly within the
//     guard window after the last explicit STANDBY.
constexpr bool shouldSuppressStandbyReassert(uint8_t newMode, unsigned long msSinceExplicitStandby) {
    return newMode != MODE_STANDBY && msSinceExplicitStandby < STANDBY_REASSERT_GUARD_MS;
}

// Compile-time truth table — pins the contract so a future edit fails the
// firmware compile rather than silently changing the guard decision.
//
// STANDBY target is never suppressed, regardless of timing:
static_assert(!shouldSuppressStandbyReassert(MODE_STANDBY, 0UL), "PRO-421: STANDBY must never be suppressed");
static_assert(!shouldSuppressStandbyReassert(MODE_STANDBY, STANDBY_REASSERT_GUARD_MS + 1),
              "PRO-421: STANDBY must never be suppressed");
// Non-STANDBY re-assert INSIDE the window is suppressed (the bounce):
static_assert(shouldSuppressStandbyReassert(MODE_STEAM, 0UL), "PRO-421: immediate STEAM re-assert suppressed");
static_assert(shouldSuppressStandbyReassert(MODE_STEAM, 150UL), "PRO-421: reflexive STEAM re-fire suppressed");
static_assert(shouldSuppressStandbyReassert(MODE_BREW, STANDBY_REASSERT_GUARD_MS - 1),
              "PRO-421: non-standby re-assert just inside window suppressed");
// Non-STANDBY at/after the window edge is allowed (a deliberate re-press):
static_assert(!shouldSuppressStandbyReassert(MODE_STEAM, STANDBY_REASSERT_GUARD_MS),
              "PRO-421: deliberate re-entry at window edge allowed");
static_assert(!shouldSuppressStandbyReassert(MODE_STEAM, STANDBY_REASSERT_GUARD_MS + 5000),
              "PRO-421: deliberate re-entry well after window allowed");
static_assert(!shouldSuppressStandbyReassert(MODE_BREW, STANDBY_REASSERT_GUARD_MS + 1),
              "PRO-421: deliberate BREW re-entry after window allowed");

#endif // STANDBYREASSERTPOLICY_H
