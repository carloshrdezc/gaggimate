#ifndef CONTROLLER_LINK_POLICY_H
#define CONTROLLER_LINK_POLICY_H

#include <cstdint>

// PRO-655 (inc 3, R3/R4b): pure display-side decisions for the NanoPbComm link to
// the controller, extracted from Controller so they can be host-tested
// (test/test_controller_link_policy). Model follows upstream v1.9's
// SystemInfo.protocolMismatch (Controller.cpp :322-352, :636, :652-678, :897),
// tightened so no control is sent before a matching SystemInfo has arrived.
namespace controller_link {

// Any version other than ours is a mismatch. 0 = unknown / legacy controller
// (missing TX/RX chars -> onIncompatibleController, or a v<=4 SystemInfo).
inline bool isProtocolMismatch(uint32_t controllerVersion, uint32_t localVersion) { return controllerVersion != localVersion; }

// Control (boiler/pump/valve/alt, PID, pump model, pressure scale, tare) may only
// be sent once a SystemInfo for THIS link arrived and it matches our protocol.
inline bool controlAllowed(bool connected, bool systemInfoReceived, bool mismatch) {
    return connected && systemInfoReceived && !mismatch;
}

// Keepalive ping: needed before SystemInfo (it completes the server handshake) and
// on a healthy link; suppressed on a known mismatch so the controller's own ping
// watchdog keeps its outputs off.
inline bool shouldSendPing(bool connected, bool systemInfoReceived, bool mismatch) {
    return connected && !(systemInfoReceived && mismatch);
}

// B-P3-1: enter the "waiting for controller" state (CONTROLLER_BLUETOOTH_WAITING)
// when no usable controller exists after the grace window: either no link, OR a
// link that never delivered SystemInfo (a peer with the framed chars that never
// sends info). Keyed on SystemInfo, not on the raw link state. `elapsedMs` is
// measured from boot / last disconnect / last link-up (wrap-safe unsigned diff).
inline bool shouldEnterWaiting(bool alreadyWaiting, bool initialized, bool connected, bool systemInfoReceived, uint32_t elapsedMs,
                               uint32_t timeoutMs) {
    if (alreadyWaiting || !initialized) {
        return false;
    }
    const bool usable = connected && systemInfoReceived;
    return !usable && elapsedMs > timeoutMs;
}

// B-P3-2: the control frame is ALWAYS the full four-part state (boiler + pump +
// brew valve + alt relay, one atomic batch), but it is only sent when any part
// changed, when a resend is forced (new link / SystemInfo), or when the keepalive
// interval elapsed since the last send. That cuts steady-state BLE traffic from
// 10 frames/s to 1 frame/s while keeping the controller refreshed with the whole
// state well inside its 20 s link watchdog. `elapsedMs` is a wrap-safe diff.
inline bool shouldSendControl(bool stateChanged, bool forceResend, uint32_t elapsedMs, uint32_t keepaliveMs) {
    return forceResend || stateChanged || elapsedMs >= keepaliveMs;
}

// PRO-670 (B-P3-4): a mode change that LEAVES standby (standby -> any non-standby
// mode) is only allowed while control is allowed (verified, matching controller).
// Entering / staying in standby is always allowed, as is any change that doesn't
// start from standby (stop / OTA paths go through activateStandby()).
inline bool modeChangeAllowed(bool currentIsStandby, bool targetIsStandby, bool controlIsAllowed) {
    if (targetIsStandby || !currentIsStandby) {
        return true;
    }
    return controlIsAllowed;
}

// A mismatch must force standby even when startup mode is BREW; a matching
// controller follows the configured startup mode.
inline bool shouldActivateStandbyOnReady(bool mismatch, bool startupModeIsStandby) { return mismatch || startupModeIsStandby; }

// PRO-671/673: what Controller::onSystemInfo does to the mode for one SystemInfo.
enum class SystemInfoModeAction {
    None,           // leave the mode alone
    StartupStandby, // first SystemInfo since boot: apply startup standby (or mismatch-forced standby)
    ForceStandby,   // later SystemInfo is mismatched while not in standby: force standby
    RestoreStartup, // matching SystemInfo after a mismatch-forced standby: restore the startup mode
};

// `loaded`                 - a SystemInfo was already handled since boot.
// `mismatchForcedStandby`  - the current standby was forced by a mismatch (Controller flag; NOT
//                            set by an ordinary disconnect / user / timeout standby).
// An ordinary reconnect while in STANDBY must never auto-restore the startup mode.
inline SystemInfoModeAction systemInfoModeAction(bool loaded, bool mismatchForcedStandby, bool mismatch, bool modeIsStandby,
                                                 bool startupModeIsStandby) {
    if (!loaded) {
        return shouldActivateStandbyOnReady(mismatch, startupModeIsStandby) ? SystemInfoModeAction::StartupStandby
                                                                            : SystemInfoModeAction::None;
    }
    if (mismatch) {
        return modeIsStandby ? SystemInfoModeAction::None : SystemInfoModeAction::ForceStandby;
    }
    if (mismatchForcedStandby && modeIsStandby && !startupModeIsStandby) {
        return SystemInfoModeAction::RestoreStartup;
    }
    return SystemInfoModeAction::None;
}

// PRO-674: how one accepted Controller::setMode() updates mismatchForcedStandby.
//   Clear    - any ordinary mode change (restore, user stop/wake, error, timeout)
//              ends a mismatch-forced standby. The default.
//   Preserve - the link-drop standby in onConnectionChanged(false): a drop is not a
//              new standby cause, so the flag is left exactly as it is. NOT a
//              save/clear/restore: nothing is written, so a concurrent explicit
//              STANDBY (WebUI/relay task) that clears the flag is never undone.
//   Set      - onSystemInfo ForceStandby/StartupStandby: written inside setMode()
//              right after the mode, to the SystemInfo's mismatch value.
enum class MismatchFlagUpdate { Clear, Preserve, Set };

// New flag value after an accepted setMode(); `current` is the flag's value at the
// time of the write and is returned unchanged for Preserve (=> no store needed).
inline bool mismatchForcedAfterModeChange(MismatchFlagUpdate update, bool current, bool mismatch) {
    switch (update) {
    case MismatchFlagUpdate::Preserve:
        return current;
    case MismatchFlagUpdate::Set:
        return mismatch;
    case MismatchFlagUpdate::Clear:
    default:
        return false;
    }
}

// PRO-674 (item 3): what DefaultUI does with the screen on CONTROLLER_BLUETOOTH_CONNECT.
// The decision follows the Controller's ACTUAL mode, never the configured startup
// mode or the mismatch flag: the standby screen is only left when the Controller has
// really left STANDBY (RestoreStartup already ran setMode() before CONNECT fired), and
// a Controller still in STANDBY keeps the standby screen. Any non-standby screen is
// left alone: every entry INTO standby already switches the screen via
// CONTROLLER_MODE_CHANGE, so a non-standby screen while the Controller is STANDBY is
// a screen the user navigated to (menu/settings) and must not be yanked away.
enum class ReconnectScreenAction {
    None,           // leave the screen alone
    StayStandby,    // controller is STANDBY and the standby screen is up: (re)arm the dim timer
    ShowModeScreen, // controller left STANDBY but the standby screen is still up
};

inline ReconnectScreenAction reconnectScreenAction(bool screenIsStandby, bool controllerModeIsStandby) {
    if (!screenIsStandby) {
        return ReconnectScreenAction::None;
    }
    return controllerModeIsStandby ? ReconnectScreenAction::StayStandby : ReconnectScreenAction::ShowModeScreen;
}

// Standby kicker text (spacemono_14 is uppercase-only). The side with the lower
// protocol version must be updated (upstream :677-678).
inline const char *mismatchKickerMessage(uint32_t controllerVersion, uint32_t localVersion) {
    return controllerVersion > localVersion ? "VERSION MISMATCH \xC2\xB7 UPDATE DISPLAY"
                                            : "VERSION MISMATCH \xC2\xB7 UPDATE CONTROLLER";
}

} // namespace controller_link

#endif // CONTROLLER_LINK_POLICY_H
