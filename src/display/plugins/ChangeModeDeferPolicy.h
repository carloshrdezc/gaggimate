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
// The decision (PRO-261 + PRO-265):
//   - DEFER iff the settle window is open AND the target is not STANDBY.
//   - STANDBY is an explicit user stop and must NEVER defer (PRO-265): it
//     bypasses the settle window and stops immediately, mirroring
//     Controller::activateStandby() and the physical STANDBY button.
//   - With no settle window (no scale / flow-estimation / time-based shot, or
//     not coming from an active brew), engage immediately — no added latency.
//
// This is the exact inline condition the handler used
// (`newMode != MODE_STANDBY && ShotHistory.isExtendedRecording()`), lifted into
// a pure, header-only predicate so it is host-testable in [env:native] without
// linking Controller/BLE/LVGL/FreeRTOS. Only the lightweight `constants.h`
// (pure `#define`s, no includes) is pulled in for MODE_STANDBY.
//
// Inputs:
//  - newMode:             the requested target mode (MODE_* from constants.h).
//  - isExtendedRecording: ShotHistory.isExtendedRecording() — true while the
//                         post-stop settle window is open.
constexpr bool shouldDeferModeChange(uint8_t newMode, bool isExtendedRecording) {
    return newMode != MODE_STANDBY && isExtendedRecording;
}

// Compile-time truth table — pins the no-behavior-change contract so a future
// edit to the predicate fails the firmware compile rather than silently
// changing the arming decision.
//
// STANDBY target NEVER defers, regardless of the settle window:
static_assert(!shouldDeferModeChange(MODE_STANDBY, true), "PRO-265: STANDBY must never defer (settle open)");
static_assert(!shouldDeferModeChange(MODE_STANDBY, false), "PRO-265: STANDBY must never defer (no settle)");
// Non-STANDBY target defers IFF the settle window is open:
static_assert(shouldDeferModeChange(MODE_STEAM, true), "PRO-261: non-standby + settle open -> defer");
static_assert(shouldDeferModeChange(MODE_GRIND, true), "PRO-261: non-standby + settle open -> defer");
static_assert(shouldDeferModeChange(MODE_MANUAL, true), "PRO-261: non-standby + settle open -> defer");
// Non-STANDBY target with no settle window engages immediately:
static_assert(!shouldDeferModeChange(MODE_STEAM, false), "no settle window -> engage immediately");
static_assert(!shouldDeferModeChange(MODE_GRIND, false), "no settle window -> engage immediately");
static_assert(!shouldDeferModeChange(MODE_MANUAL, false), "no settle window -> engage immediately");

#endif // CHANGEMODEDEFERPOLICY_H
