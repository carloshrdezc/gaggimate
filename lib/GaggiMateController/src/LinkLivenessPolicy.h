#ifndef LINK_LIVENESS_POLICY_H
#define LINK_LIVENESS_POLICY_H

// PRO-655 R1: controller link-liveness failsafe decisions, kept pure so they can be
// host-tested (test/test_link_liveness_policy). The controller only acts on framed
// NanoPbComm traffic (ping + Boiler/Pump/Relay control feed handlePing()); with no
// such traffic for longer than the timeout, heater/pump/valve/alt are forced off.
// That holds whatever the peer is (legacy NimBLEComm display, mismatched display,
// or none), because nothing but framed RX-char payloads ever calls handlePing().

namespace link_liveness {

// Same arithmetic as Carlos's pre-PRO-655 GaggiMateController::loop():
// `lastPingTime < now && (now - lastPingTime) / 1000 > PING_TIMEOUT_SECONDS`
// (integer seconds, strict >, no trip when lastPing is at/after now).
inline bool pingTimedOut(unsigned long nowMs, unsigned long lastPingMs, double timeoutSeconds) {
    return lastPingMs<nowMs &&static_cast<double>((nowMs - lastPingMs) / 1000)> timeoutSeconds;
}

// Upstream v1.9: on the healthy -> timeout transition, force a BLE disconnect so a
// wedged GATT link is rebuilt, but never while a controller OTA is in progress and
// never repeatedly while already timed out (loop() re-enters every 250 ms).
inline bool shouldDropLinkOnTimeout(bool alreadyTimedOut, bool otaUpdating) { return !alreadyTimedOut && !otaUpdating; }

} // namespace link_liveness

#endif // LINK_LIVENESS_POLICY_H
