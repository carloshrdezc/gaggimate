#ifndef LINK_LIVENESS_POLICY_H
#define LINK_LIVENESS_POLICY_H

// PRO-655 R1: controller link-liveness failsafe decisions, kept pure so they can be
// host-tested (test/test_link_liveness_policy). The controller only acts on framed
// NanoPbComm traffic (ping + Boiler/Pump/Relay control feed handlePing()); with no
// such traffic for longer than the timeout, heater/pump/valve/alt are forced off.
// That holds whatever the peer is (legacy NimBLEComm display, mismatched display,
// or none), because nothing but framed RX-char payloads ever calls handlePing().

#include <cstdint>

namespace link_liveness {

// PRO-670: how far in the future a ping stamp may be (NimBLE task stamping lastPing
// just AFTER loop() sampled `now`) and still count as "fresh". Kept small (1 s) so
// every genuine gap >= the timeout trips up to the 32-bit horizon (~49.7 d); the old
// upper-half guard swallowed any real gap longer than ~24.8 days.
constexpr uint32_t FUTURE_STAMP_TOLERANCE_MS = 1000u;

// PRO-655 A-P1-1: wrap-safe. Unsigned subtraction survives the ~49.7-day millis()
// wrap. An "elapsed" within FUTURE_STAMP_TOLERANCE_MS of 2^32 means lastPing was
// stamped just AFTER `now` (race) -> not timed out.
// Integer ms, strict >. Callers pass PING_TIMEOUT_MS (20999), which keeps the old
// `(now - last) / 1000 > 20.0` trip point exactly (elapsed >= 21000 ms).
inline bool pingTimedOut(uint32_t nowMs, uint32_t lastPingMs, uint32_t timeoutMs) {
    const uint32_t elapsed = nowMs - lastPingMs;
    if (elapsed > 0xFFFFFFFFu - FUTURE_STAMP_TOLERANCE_MS) {
        return false;
    }
    return elapsed > timeoutMs;
}

// Wrap-safe "a is strictly after b" for millis() stamps (replaces `a > b`).
inline bool msAfter(uint32_t a, uint32_t b) { return static_cast<int32_t>(a - b) > 0; }

// Upstream v1.9: on the healthy -> timeout transition, force a BLE disconnect so a
// wedged GATT link is rebuilt, but never while a controller OTA is in progress and
// never repeatedly while already timed out (loop() re-enters every 250 ms).
inline bool shouldDropLinkOnTimeout(bool alreadyTimedOut, bool otaUpdating) { return !alreadyTimedOut && !otaUpdating; }

} // namespace link_liveness

#endif // LINK_LIVENESS_POLICY_H
