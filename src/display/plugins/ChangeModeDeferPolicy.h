#ifndef CHANGEMODEDEFERPOLICY_H
#define CHANGEMODEDEFERPOLICY_H

#include "../core/constants.h"
#include <cstdint>

// PRO-267: decision predicate for the WebUIPlugin `req:change-mode` arming gate.
//
// When a `req:change-mode` request arrives, WebUIPlugin calls
// Controller::deactivate() (ending any active process and synchronously firing
// controller:brew:end, which opens the post-stop settle window via
// ShotHistory.endRecording() iff a healthy BLE scale was the volumetric source).
// It then decides whether to DEFER the clear()+setMode() to loop() (the main
// task) — keeping the BLE scale connected and record() logging so the final
// drips reach the recorded yield — or to engage the new mode IMMEDIATELY.
//
// The decision (PRO-261 + PRO-265 + PRO-587):
//   - DEFER iff the settle window is open AND the target is not an EXPLICIT
//     STANDBY. A non-STANDBY target always defers while the window is open; a
//     STANDBY target defers only when the transition is marked `automatic`.
//   - An EXPLICIT STANDBY (automatic == false) is a human stop and must NEVER
//     defer (PRO-265): it bypasses the settle window and stops immediately,
//     mirroring Controller::activateStandby() and the physical STANDBY button.
//     Sources: physical button, web "Standby" button, HomeKit.
//   - An AUTOMATIC STANDBY (automatic == true) is the post-shot standby-on-brew
//     transition (PRO-545/PRO-587). It rides the SAME settle-window deferral
//     Auto-Steam already uses so the BLE scale stays connected and the final
//     drips reach the recorded yield, instead of tearing down immediately.
//   - With no settle window (no scale / flow-estimation / time-based shot, or
//     not coming from an active brew), engage immediately — no added latency.
//
// PRO-587 adds the `automatic` dimension WITHOUT changing PRO-265's
// explicit-STANDBY contract: with `automatic` absent/false the predicate reduces
// to the original `newMode != MODE_STANDBY && isExtendedRecording` — bit-for-bit
// the pre-PRO-587 behavior for every explicit stop. Pure, header-only, so it is
// host-testable in [env:native] without linking Controller/BLE/LVGL/FreeRTOS.
// Only the lightweight `constants.h` (pure `#define`s, no includes) is pulled in
// for MODE_STANDBY.
//
// Inputs:
//  - newMode:             the requested target mode (MODE_* from constants.h).
//  - isExtendedRecording: ShotHistory.isExtendedRecording() — true while the
//                         post-stop settle window is open.
//  - automatic:           true when the transition is an automatic, non-explicit
//                         post-shot standby-on-brew request (PRO-587); false
//                         (default) for an explicit user-initiated request. Only
//                         relevant for a STANDBY target — a non-STANDBY target
//                         defers on the settle window regardless of this flag.
constexpr bool shouldDeferModeChange(uint8_t newMode, bool isExtendedRecording, bool automatic = false) {
    return isExtendedRecording && (newMode != MODE_STANDBY || automatic);
}

// Compile-time truth table — pins the contract so a future edit fails the
// firmware compile rather than silently changing the arming decision.
//
// PRO-265: an EXPLICIT STANDBY (automatic absent/false) NEVER defers, regardless
// of the settle window — the untouched fast-stop guarantee:
static_assert(!shouldDeferModeChange(MODE_STANDBY, true), "PRO-265: explicit STANDBY must never defer (settle open)");
static_assert(!shouldDeferModeChange(MODE_STANDBY, false), "PRO-265: explicit STANDBY must never defer (no settle)");
static_assert(!shouldDeferModeChange(MODE_STANDBY, true, false), "PRO-265: explicit STANDBY must never defer (settle open)");
static_assert(!shouldDeferModeChange(MODE_STANDBY, false, false), "PRO-265: explicit STANDBY must never defer (no settle)");
// PRO-587: an AUTOMATIC STANDBY defers IFF the settle window is open (rides the
// same window as Auto-Steam), and engages immediately when there is none:
static_assert(shouldDeferModeChange(MODE_STANDBY, true, true), "PRO-587: automatic STANDBY defers while settle open");
static_assert(!shouldDeferModeChange(MODE_STANDBY, false, true), "PRO-587: automatic STANDBY engages immediately (no settle)");
// Non-STANDBY target defers IFF the settle window is open (independent of the
// automatic flag — the flag only ever matters for a STANDBY target):
static_assert(shouldDeferModeChange(MODE_BREW, true), "PRO-261: non-standby + settle open -> defer");
static_assert(shouldDeferModeChange(MODE_STEAM, true), "PRO-261: non-standby + settle open -> defer");
static_assert(shouldDeferModeChange(MODE_WATER, true), "PRO-261: non-standby + settle open -> defer");
static_assert(shouldDeferModeChange(MODE_GRIND, true), "PRO-261: non-standby + settle open -> defer");
static_assert(shouldDeferModeChange(MODE_MANUAL, true), "PRO-261: non-standby + settle open -> defer");
static_assert(shouldDeferModeChange(MODE_STEAM, true, true), "PRO-587: non-standby defers regardless of automatic flag");
static_assert(shouldDeferModeChange(MODE_STEAM, true, false), "PRO-587: non-standby defers regardless of automatic flag");
// Non-STANDBY target with no settle window engages immediately:
static_assert(!shouldDeferModeChange(MODE_BREW, false), "PRO-261: no settle window -> engage immediately");
static_assert(!shouldDeferModeChange(MODE_STEAM, false), "PRO-261: no settle window -> engage immediately");
static_assert(!shouldDeferModeChange(MODE_WATER, false), "PRO-261: no settle window -> engage immediately");
static_assert(!shouldDeferModeChange(MODE_GRIND, false), "PRO-261: no settle window -> engage immediately");
static_assert(!shouldDeferModeChange(MODE_MANUAL, false), "PRO-261: no settle window -> engage immediately");

#endif // CHANGEMODEDEFERPOLICY_H
