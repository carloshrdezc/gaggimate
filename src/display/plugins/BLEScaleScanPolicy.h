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

#endif // BLESCALESCANPOLICY_H
