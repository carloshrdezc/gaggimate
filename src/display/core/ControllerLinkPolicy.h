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
