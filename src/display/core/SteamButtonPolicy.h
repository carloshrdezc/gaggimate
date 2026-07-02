#ifndef STEAMBUTTONPOLICY_H
#define STEAMBUTTONPOLICY_H

#include "constants.h"

// PRO-391: with a non-momentary (latching) steam switch, pressing web-UI
// Stop-Steam shows Standby for an instant, then bounces straight back to Steam;
// a second manual Standby is needed to make it stick.
//
// This is the residual half of PRO-278 (#257). PRO-278 fixed the ordering inside
// Controller::activateStandby() (deactivate() before setMode(MODE_STANDBY)), but
// it did not touch Controller::handleSteamButton().
//
// Root cause: a non-momentary switch does not report a one-shot press — it
// reports a persistent LEVEL. steamButtonStatus stays 1 for as long as the
// physical switch is closed. The old handler treated every level-high
// notification as a fresh press:
//
//     if (steamButtonStatus) {
//         switch (getMode()) {
//         case MODE_STANDBY: setMode(MODE_STEAM); break;   // <-- re-asserts STEAM
//         case MODE_BREW:    setMode(MODE_STEAM); break;
//         }
//     } else if (!isMomentary && getMode() == MODE_STEAM) { deactivate(); setMode(MODE_BREW); }
//
// So when the web UI drives the machine to MODE_STANDBY while the physical steam
// switch is still ON, the next latched-high notification hits
// `case MODE_STANDBY: setMode(MODE_STEAM)` and bounces the machine back to Steam.
//
// The fix: for NON-momentary switches, only act on the RISING EDGE of the level
// (previous low -> current high). A sustained latched-high level after an
// explicit Standby is no longer a fresh assertion, so Standby wins. A genuine
// user re-press (toggle the switch OFF then ON) is a real rising edge and still
// enters Steam, so the switch never wedges. Momentary buttons are one-shot
// presses at the source and are intentionally NOT edge-gated: they keep behaving
// exactly as before.
//
// This header pins the pure edge-vs-level decision so it is host-testable in
// [env:native] without instantiating the full Controller (which pulls in
// BLE/LVGL/FreeRTOS and cannot link on the host).

// What handleSteamButton() should do for a given steam-button notification.
enum class SteamButtonAction {
    NONE,        // do nothing
    ENTER_STEAM, // setMode(MODE_STEAM)
    EXIT_STEAM,  // deactivate() + setMode(MODE_BREW)
};

// Decide the action for a steam-button notification.
//
//   momentary   - settings.isMomentaryButtons(): true = one-shot press at the
//                 source, false = latching switch reporting a persistent level.
//   previousLevel - the previous steamButtonStatus this handler saw (0/1). Only
//                 meaningful for non-momentary; ignored for momentary.
//   currentLevel  - the steamButtonStatus for this notification (0/1).
//   mode        - the current Controller mode (MODE_* from constants.h).
//
// Contract:
//   * Momentary press (currentLevel != 0): ENTER_STEAM from STANDBY or BREW,
//     otherwise NONE. Matches the pre-PRO-391 momentary behavior exactly.
//   * Non-momentary rising edge (previousLevel == 0, currentLevel != 0):
//     ENTER_STEAM from STANDBY or BREW. A rising edge is a genuine physical
//     switch toggle, so a real re-press (off then on) always enters Steam and
//     the switch never wedges.
//   * Non-momentary sustained high (previousLevel != 0, currentLevel != 0):
//     NONE — no re-assert on a held level. This is the bug fix / regression
//     guard: an explicit Standby (web UI) fired while the switch was already
//     latched ON must win, because it produces no new rising edge.
//   * Non-momentary level low (currentLevel == 0) while in STEAM: EXIT_STEAM.
//     Preserves the switch-OFF release path.
constexpr SteamButtonAction decideSteamButtonAction(bool momentary, int previousLevel, int currentLevel, int mode) {
    if (currentLevel != 0) {
        if (momentary) {
            // One-shot press at the source: act on every press, as before.
            return (mode == MODE_STANDBY || mode == MODE_BREW) ? SteamButtonAction::ENTER_STEAM : SteamButtonAction::NONE;
        }
        // Non-momentary: only a rising edge is a fresh assertion.
        const bool risingEdge = (previousLevel == 0);
        if (!risingEdge) {
            return SteamButtonAction::NONE; // sustained latched-high level: no re-assert (PRO-391)
        }
        // Fresh rising edge: a genuine physical switch toggle. Enter Steam from
        // STANDBY or BREW, exactly as a momentary press would. The sustained-high
        // guard above is what stops a still-latched switch from re-asserting
        // Steam after an explicit web-UI Standby.
        return (mode == MODE_STANDBY || mode == MODE_BREW) ? SteamButtonAction::ENTER_STEAM : SteamButtonAction::NONE;
    }
    // Level low. Non-momentary release from STEAM tears down and returns to BREW.
    if (!momentary && mode == MODE_STEAM) {
        return SteamButtonAction::EXIT_STEAM;
    }
    return SteamButtonAction::NONE;
}

#endif // STEAMBUTTONPOLICY_H
