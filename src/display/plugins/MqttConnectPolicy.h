#ifndef MQTTCONNECTPOLICY_H
#define MQTTCONNECTPOLICY_H

#include <cstdint>

// PRO-348 (Ref PRO-346 F2): pure decision predicates for MQTTPlugin's
// multi-fire idempotency guard and the off-WiFi-task connect deferral.
//
// Background: PRO-333 made `controller:wifi:connect` fire REPEATEDLY per STA
// session (unconditional STA_GOT_IP + end-of-setupWifi on first connect +
// SoftAP-fallback watchdog re-arm). mDNSPlugin was hardened with an
// `if (started) return;` guard (PRO-333/PRO-334); MQTTPlugin was not, so every
// re-fire RE-ENTERED a blocking retry loop (MQTT_CONNECTION_RETRIES *
// delay(MQTT_CONNECTION_DELAY)) on the arduino_events WiFi task, re-issued
// client.begin() on an already-initialized client, and re-published the whole
// Home Assistant discovery doc.
//
// The fix mirrors WebUIPlugin's deferral model: the event handler does no
// blocking work — it only latches intent (`wantConnect`) when the client is not
// already connected — and the plugin's loop() tick drives ONE non-blocking
// connect attempt per tick (naturally retrying across ticks, no delay() on the
// event task), publishing discovery exactly once per successful (re)connect.
//
// These predicates are the exact gates, lifted into pure, header-only functions
// so they are host-testable in [env:native] without linking the MQTT client /
// WiFi / FreeRTOS.

// Decision for the `controller:wifi:connect` event handler.
//
// Inputs:
//  - clientConnected: client.connected() at event time.
// Returns true IFF the handler should LATCH a pending connect intent
// (wantConnect = true) and return immediately. Returns false when the client is
// already connected — the idempotency guard, analogous to mDNS's
// `if (started) return;`: skip connect() and do NOT re-publish discovery on a
// re-fire.
constexpr bool shouldLatchMqttConnect(bool clientConnected) { return !clientConnected; }

// Action the plugin's loop() tick should take, given the latch, the current
// client connection state, and how many non-blocking attempts have already been
// spent on the current latch. This is a non-blocking, single-step state
// machine: one transition per loop tick.
enum class MqttLoopAction : uint8_t {
    // Latch not set: nothing pending, do nothing.
    None,
    // Latch set and the client is connected: publish discovery exactly once and
    // clear the latch. Covers both the normal "connect attempt just succeeded"
    // path and the race where the client became connected between latching and
    // this tick.
    PublishDiscoveryAndClear,
    // Latch set, not connected, attempt budget remaining: perform ONE
    // non-blocking client.connect() attempt this tick (no delay loop). On
    // failure the latch stays set and the next tick retries — preserving the
    // original retry count without blocking the event task.
    AttemptConnect,
    // Latch set, not connected, attempt budget exhausted: give up and clear the
    // latch, mirroring the original loop returning false after
    // MQTT_CONNECTION_RETRIES failures. A later controller:wifi:connect re-fire
    // re-latches and resets the budget.
    GiveUpAndClear,
};

// attemptsSpent: number of AttemptConnect ticks already performed on the current
//   latch (0 on the first tick after latching).
// maxAttempts: the per-latch attempt budget (MQTT_CONNECTION_RETRIES), preserving
//   the original blocking loop's retry count.
constexpr MqttLoopAction mqttLoopAction(bool wantConnect, bool clientConnected, int attemptsSpent, int maxAttempts) {
    if (!wantConnect)
        return MqttLoopAction::None;
    if (clientConnected)
        return MqttLoopAction::PublishDiscoveryAndClear;
    if (attemptsSpent >= maxAttempts)
        return MqttLoopAction::GiveUpAndClear;
    return MqttLoopAction::AttemptConnect;
}

// Compile-time truth table — pins the no-behavior-change contract so a future
// edit to either predicate fails the firmware compile rather than silently
// changing the connect/discovery gating.

// Idempotency guard: latch only when NOT already connected.
static_assert(shouldLatchMqttConnect(/*clientConnected=*/false), "PRO-348: not connected -> latch a connect intent");
static_assert(!shouldLatchMqttConnect(/*clientConnected=*/true), "PRO-348: already connected -> skip (idempotency guard)");

// loop() state machine (maxAttempts = 5 mirrors MQTT_CONNECTION_RETRIES):
static_assert(mqttLoopAction(/*want=*/false, /*conn=*/false, 0, 5) == MqttLoopAction::None,
              "PRO-348: no latch -> no work");
static_assert(mqttLoopAction(/*want=*/false, /*conn=*/true, 0, 5) == MqttLoopAction::None,
              "PRO-348: no latch -> no work even if connected");
static_assert(mqttLoopAction(/*want=*/true, /*conn=*/true, 0, 5) == MqttLoopAction::PublishDiscoveryAndClear,
              "PRO-348: latched + connected -> publish discovery once and clear");
static_assert(mqttLoopAction(/*want=*/true, /*conn=*/true, 3, 5) == MqttLoopAction::PublishDiscoveryAndClear,
              "PRO-348: connected wins over remaining budget");
static_assert(mqttLoopAction(/*want=*/true, /*conn=*/false, 0, 5) == MqttLoopAction::AttemptConnect,
              "PRO-348: latched + not connected + budget -> single non-blocking attempt");
static_assert(mqttLoopAction(/*want=*/true, /*conn=*/false, 4, 5) == MqttLoopAction::AttemptConnect,
              "PRO-348: last attempt still within budget");
static_assert(mqttLoopAction(/*want=*/true, /*conn=*/false, 5, 5) == MqttLoopAction::GiveUpAndClear,
              "PRO-348: budget exhausted -> give up and clear (re-fire re-latches)");

#endif // MQTTCONNECTPOLICY_H
