#ifndef DISPLAYRESTARTPOLICY_H
#define DISPLAYRESTARTPOLICY_H

#include "../../core/constants.h"
#include <cstdint>

// PRO-539: physical restart is intentionally fail-closed. `processActive` is
// read by Controller under processMutex; a mutex timeout denies restart.
constexpr bool shouldRestartDisplay(bool processActive, bool updating, bool autotuning, bool errorState, uint8_t mode,
                                    bool grindActive) {
    return !processActive && !updating && !autotuning && !errorState && mode != MODE_WATER && mode != MODE_GRIND && !grindActive;
}

static_assert(shouldRestartDisplay(false, false, false, false, MODE_BREW, false), "PRO-539: idle brew mode may restart");
static_assert(!shouldRestartDisplay(true, false, false, false, MODE_BREW, false),
              "PRO-539: active or mutex-timeout blocks restart");
static_assert(!shouldRestartDisplay(false, true, false, false, MODE_BREW, false), "PRO-539: update blocks restart");
static_assert(!shouldRestartDisplay(false, false, true, false, MODE_BREW, false), "PRO-539: autotuning blocks restart");
static_assert(!shouldRestartDisplay(false, false, false, true, MODE_BREW, false), "PRO-539: error state blocks restart");
static_assert(!shouldRestartDisplay(false, false, false, false, MODE_WATER, false), "PRO-539: water mode blocks restart");
static_assert(!shouldRestartDisplay(false, false, false, false, MODE_GRIND, false), "PRO-539: grind mode blocks restart");
static_assert(!shouldRestartDisplay(false, false, false, false, MODE_BREW, true), "PRO-539: active grinder blocks restart");

#endif // DISPLAYRESTARTPOLICY_H
