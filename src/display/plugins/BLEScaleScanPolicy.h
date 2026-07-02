#ifndef BLESCALESCANPOLICY_H
#define BLESCALESCANPOLICY_H

#include <display/core/constants.h>

// PRO-5: max number of CONSECUTIVE failed reconnect ticks tolerated before the
// scale is torn down and a fresh async scan is started. Lives here (host-
// includable) rather than in BLEScalePlugin.h so nextReconnectionTries() and its
// host tests share one source of truth. UPDATE_INTERVAL_MS ticks are ~1 s apart,
// so this is roughly the reconnect grace in seconds.
constexpr unsigned int RECONNECTION_TRIES = 15;

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

// PRO-5: next value of the scale reconnection-tries counter for one update tick.
// `tries` is the current counter, `connected` whether the scale link is up this
// tick. A healthy tick resets the counter to 0; an unhealthy tick increments it.
// The counter must measure *consecutive* failed reconnect attempts: it was
// previously reset only on a full teardown, so a link that briefly dropped and
// recovered on its own left the counter stuck, and clustered transient flaps
// (common right after a display-sleep radio-idle/wake cycle) exhausted the
// RECONNECTION_TRIES budget and prematurely tore the scale down. Resetting on
// every healthy tick keeps the budget measuring only back-to-back failures.
constexpr unsigned int nextReconnectionTries(unsigned int tries, bool connected) { return connected ? 0u : tries + 1u; }

#endif // BLESCALESCANPOLICY_H
