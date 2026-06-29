#ifndef BLESCALESCANPOLICY_H
#define BLESCALESCANPOLICY_H

#include <display/core/constants.h>

constexpr bool shouldScanForBleScaleMode(int mode) { return mode == MODE_BREW || mode == MODE_GRIND || mode == MODE_MANUAL; }

// True when a mode transition should open the STEAM scale grace window: the
// machine is leaving a scanning mode (brew/grind/manual) and entering STEAM.
// In this case the BLE scale is kept connected briefly (see
// STEAM_SCALE_GRACE_PERIOD_MS) to capture the last drops before disconnecting.
// Every other transition out of a scanning mode disconnects immediately.
constexpr bool shouldStartSteamScaleGrace(int previousMode, int newMode) {
    return newMode == MODE_STEAM && shouldScanForBleScaleMode(previousMode);
}

// True when a mode-change event is a no-op transition (the mode did not actually
// change). A same-mode re-fire (e.g. WebUIPlugin re-sending the current mode, or
// Controller.cpp not guarding a same-mode change) must NOT re-run any
// scan/teardown logic: in particular a redundant STEAM->STEAM event must not
// collapse an in-flight steam grace window into an immediate disconnect.
constexpr bool isRedundantModeChange(int previousMode, int newMode) { return previousMode == newMode; }

#endif // BLESCALESCANPOLICY_H
