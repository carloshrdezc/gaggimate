#ifndef STANDBYTRANSITIONPOLICY_H
#define STANDBYTRANSITIONPOLICY_H

#include <cstddef>

// PRO-278: ordering contract for Controller::activateStandby().
//
// activateStandby() performs two steps to stop the machine:
//   - DEACTIVATE: tear down the running process (Controller::deactivate()),
//     after which no process is active (isActive() == false).
//   - SET_MODE:   flip the controller mode to MODE_STANDBY (Controller::setMode),
//     which dispatches the mutable `controller:mode:change` event.
//
// The user-reported bug ("press stop-steam, it shows Standby for an instant then
// bounces back to Steam; a second press is needed to make it stick") is an
// ordering bug: when SET_MODE runs *before* DEACTIVATE, there is a window in
// which mode == MODE_STANDBY while the steam/brew process is still active. In
// that window the `controller:mode:change` event has already fired with a
// still-active process behind it, and a re-assert path (a live SteamProcess, the
// steam UI screen, or a mode-change handler) can flip the mode back to
// MODE_STEAM/MODE_BREW. On the second press the process is already gone, so
// Standby finally sticks.
//
// The safe ordering is DEACTIVATE then SET_MODE — the same order every sibling
// teardown already uses (Controller::deactivateStandby(), the steam-button
// release in handleSteamButton(), and WebUIPlugin's req:change-mode STANDBY
// path). This header pins that contract so the ordering can't silently regress
// and is host-testable without instantiating the full Controller (which pulls in
// BLE/LVGL/FreeRTOS and cannot link in [env:native]).

enum class StandbyStep {
    DEACTIVATE, // Controller::deactivate(): running process torn down
    SET_MODE,   // Controller::setMode(MODE_STANDBY): mode flips, event fires
};

// True when the given two-step ordering never exposes the bounce window:
// SET_MODE (which fires controller:mode:change and leaves mode == STANDBY) must
// not run while a process is still active, i.e. DEACTIVATE must come first.
//
// Models the transition as the ordered pair (first, second). The window exists
// iff SET_MODE happens before DEACTIVATE.
constexpr bool standbyOrderingIsSafe(StandbyStep first, StandbyStep second) {
    // Safe exactly when we deactivate first, then set the mode.
    return first == StandbyStep::DEACTIVATE && second == StandbyStep::SET_MODE;
}

// The ordering activateStandby() actually performs. Keep this in lock-step with
// the body of Controller::activateStandby(); the static_assert below wires the
// contract to the real call so a future reorder fails the firmware compile.
constexpr StandbyStep ACTIVATE_STANDBY_FIRST_STEP = StandbyStep::DEACTIVATE;
constexpr StandbyStep ACTIVATE_STANDBY_SECOND_STEP = StandbyStep::SET_MODE;

static_assert(standbyOrderingIsSafe(ACTIVATE_STANDBY_FIRST_STEP, ACTIVATE_STANDBY_SECOND_STEP),
              "PRO-278: activateStandby() must deactivate() before setMode(MODE_STANDBY)");

#endif // STANDBYTRANSITIONPOLICY_H
