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
inline bool shouldEnterWaiting(bool alreadyWaiting, bool initialized, bool connected, bool systemInfoReceived,
                               uint32_t elapsedMs, uint32_t timeoutMs) {
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

// Startup-standby activation is skipped on mismatch (upstream :352).
inline bool shouldActivateStandbyOnReady(bool mismatch, bool startupModeIsStandby) { return !mismatch && startupModeIsStandby; }

// Standby kicker text (spacemono_14 is uppercase-only). The side with the lower
// protocol version must be updated (upstream :677-678).
inline const char *mismatchKickerMessage(uint32_t controllerVersion, uint32_t localVersion) {
    return controllerVersion > localVersion ? "VERSION MISMATCH \xC2\xB7 UPDATE DISPLAY"
                                            : "VERSION MISMATCH \xC2\xB7 UPDATE CONTROLLER";
}

} // namespace controller_link

#endif // CONTROLLER_LINK_POLICY_H
