#ifndef WEBUILIFECYCLEDEFERPOLICY_H
#define WEBUILIFECYCLEDEFERPOLICY_H

#include <cstdint>

// PRO-417: pure decision predicates for deferring WebUIPlugin's server
// start()/stop() OFF the WiFi-event (arduino_events) task and onto the Arduino
// loop task.
//
// Background / crash evidence: on nightly-250 (2.0.15-250-g8780c419) the device
// panic-reboots during WiFi disassociation churn (13 disconnects observed, ALL
// Reason: ASSOC_LEAVE). The symbolicated coredump shows an interrupt-context
// StoreProhibited (excvaddr 0x0) taken inside the vendored WPA-supplicant
// constant-time crypto (const_time_select_bin) while BOTH idle tasks are in that
// path and IDLE0's stack is nearly exhausted (USED/FREE 608/404) -> the panic
// handler (esp_task_wdt / panic_abort) fires. The store-to-null lives in the
// closed-source esp_wifi/wpa_supplicant blob (espressif32@6.12.0 / IDF 4.4), so
// there is no in-repo line to patch there. The actionable in-firmware
// contributor is the WiFi-event handler doing HEAVY, BLOCKING teardown inline
// while the supplicant is mid-(re)association:
//
//   WebUIPlugin::setup() registered
//     on("controller:wifi:disconnect", { stop(); })   // ran INLINE on arduino_events
//     on("controller:wifi:connect",    { start(); })  // ran INLINE on arduino_events
//
//   stop() -> stopRelay() spin-waits up to ~500 ms (vTaskDelay) for the relay
//   task to self-delete AND ws.closeAll() walks the client list under wsMutex
//   (contending with the AsyncTCP task). All of that executed synchronously on
//   the arduino_events WiFi-event task, inside the WiFi event dispatch, once per
//   ASSOC_LEAVE. An ASSOC_LEAVE storm therefore repeatedly stalls the WiFi event
//   queue and hogs core 0 (where arduino_events, the WiFi driver, and IDLE0 all
//   live) exactly while the supplicant is renegotiating -> starvation window.
//
// This is the SAME class of bug the codebase already fixed elsewhere by moving
// blocking work off the WiFi-event task: mDNSPlugin (PRO-333/334), MQTTPlugin
// (PRO-348, see MqttConnectPolicy.h), and WebUIPlugin's own OTA-start / release-
// URL / mode-change intents (CAR-377 / CAR-178 / PRO-261), which all LATCH intent
// on the event task and DRAIN it on loop(). WebUIPlugin's server lifecycle was
// the last event-task handler still doing heavy inline work. This policy brings
// it in line: the event handlers only latch a desired lifecycle target; loop()
// (Arduino loop task) drains it and runs the actual start()/stop().
//
// Kept pure (header-only, no Arduino / FreeRTOS / WiFi headers) so [env:native]
// can cover the latch/drain state machine without linking the server stack.

// The lifecycle target most recently requested by a WiFi event. `None` means no
// change is pending.
//
// PRO-418: the connect event's captive-portal mode (AP vs STA) is folded INTO
// this single enum (StartStation / StartAp) rather than carried in a second
// `pendingApMode` atomic. With one atomic there is no cross-atomic ordering
// question: the loop task can never observe a fresh Start intent paired with a
// stale AP flag, because the AP mode IS the intent. This removes the two-atomic
// happens-before hazard flagged on PR #410 by construction.
enum class WebUiLifecycleIntent : uint8_t {
    None,         // nothing pending
    StartStation, // controller:wifi:connect (AP=0) -> bring the web server up in station mode
    StartAp,      // controller:wifi:connect (AP=1) -> bring the web server up in captive-portal (AP) mode
    Stop,         // controller:wifi:disconnect      -> tear the web server down
};

// What loop()'s drain should do this tick, given the currently-latched intent.
// PRO-418: RunStart splits into station / AP so the drain carries the mode the
// server must come up in, without a second atomic.
enum class WebUiLifecycleAction : uint8_t {
    None,            // no latch -> do nothing
    RunStartStation, // latch was StartStation -> run start() in station mode, then clear
    RunStartAp,      // latch was StartAp      -> run start() in AP (captive-portal) mode, then clear
    RunStop,         // latch was Stop         -> run stop() on the loop task, then clear
};

// Map a connect event's "AP" flag to the matching Start intent. Keeps the AP
// decision at the single point where the event is latched (PRO-418).
constexpr WebUiLifecycleIntent startIntentForApMode(bool apMode) {
    return apMode ? WebUiLifecycleIntent::StartAp : WebUiLifecycleIntent::StartStation;
}

// Latch coalescing: a WiFi event asks for a new lifecycle target. The LAST
// event wins, because WiFi (dis)connects are edge events and only the final
// state matters — if a connect then a disconnect arrive before loop() drains,
// the server should end up stopped (and vice-versa). So a new request simply
// overwrites any still-pending one; no queue, no ordering to preserve.
//
// Returns the intent that should now be latched. `requested` is the event's
// target (Start or Stop); passing None is a no-op that keeps the current latch.
constexpr WebUiLifecycleIntent latchLifecycleIntent(WebUiLifecycleIntent current, WebUiLifecycleIntent requested) {
    return requested == WebUiLifecycleIntent::None ? current : requested;
}

// Drain decision for loop(): map the latched intent to the action to take. This
// runs unconditionally near the top of loop() (before the `if (!serverRunning)
// return;` guard) so a queued Start is never stranded while the server is down.
constexpr WebUiLifecycleAction lifecycleDrainAction(WebUiLifecycleIntent latched) {
    switch (latched) {
    case WebUiLifecycleIntent::StartStation:
        return WebUiLifecycleAction::RunStartStation;
    case WebUiLifecycleIntent::StartAp:
        return WebUiLifecycleAction::RunStartAp;
    case WebUiLifecycleIntent::Stop:
        return WebUiLifecycleAction::RunStop;
    case WebUiLifecycleIntent::None:
    default:
        return WebUiLifecycleAction::None;
    }
}

// The AP (captive-portal) mode start() must apply for a given drain action.
// Only meaningful for the two RunStart* actions; other actions never read it.
// PRO-418: the drain reads this instead of a separate pendingApMode atomic.
constexpr bool drainActionIsAp(WebUiLifecycleAction action) { return action == WebUiLifecycleAction::RunStartAp; }

// Compile-time truth table — pins the coalesce + drain contract so a future edit
// fails the firmware compile rather than silently changing lifecycle gating.

// Start intent carries the AP flag (PRO-418): AP=1 -> StartAp, AP=0 -> StartStation.
static_assert(startIntentForApMode(true) == WebUiLifecycleIntent::StartAp, "PRO-418: connect AP=1 latches StartAp");
static_assert(startIntentForApMode(false) == WebUiLifecycleIntent::StartStation, "PRO-418: connect AP=0 latches StartStation");

// Latch: last event wins; None never clobbers a real pending intent.
static_assert(latchLifecycleIntent(WebUiLifecycleIntent::None, WebUiLifecycleIntent::StartStation) ==
                  WebUiLifecycleIntent::StartStation,
              "PRO-417: connect (STA) latches Start");
static_assert(latchLifecycleIntent(WebUiLifecycleIntent::None, WebUiLifecycleIntent::StartAp) == WebUiLifecycleIntent::StartAp,
              "PRO-418: connect (AP) latches StartAp");
static_assert(latchLifecycleIntent(WebUiLifecycleIntent::None, WebUiLifecycleIntent::Stop) == WebUiLifecycleIntent::Stop,
              "PRO-417: disconnect latches Stop");
static_assert(latchLifecycleIntent(WebUiLifecycleIntent::StartStation, WebUiLifecycleIntent::Stop) == WebUiLifecycleIntent::Stop,
              "PRO-417: connect-then-disconnect before drain -> Stop wins (final state)");
static_assert(latchLifecycleIntent(WebUiLifecycleIntent::Stop, WebUiLifecycleIntent::StartAp) == WebUiLifecycleIntent::StartAp,
              "PRO-418: disconnect-then-connect(AP) before drain -> StartAp wins (final state incl. mode)");
static_assert(latchLifecycleIntent(WebUiLifecycleIntent::StartStation, WebUiLifecycleIntent::StartAp) ==
                  WebUiLifecycleIntent::StartAp,
              "PRO-418: a later connect's AP mode overwrites the earlier Start's mode (last event wins)");
static_assert(latchLifecycleIntent(WebUiLifecycleIntent::Stop, WebUiLifecycleIntent::None) == WebUiLifecycleIntent::Stop,
              "PRO-417: a None request preserves the pending latch");

// Drain: latch maps to the matching run action; None is a no-op; RunStart* carries AP mode.
static_assert(lifecycleDrainAction(WebUiLifecycleIntent::None) == WebUiLifecycleAction::None,
              "PRO-417: no latch -> no lifecycle work");
static_assert(lifecycleDrainAction(WebUiLifecycleIntent::StartStation) == WebUiLifecycleAction::RunStartStation,
              "PRO-418: StartStation latch -> run start() in station mode on loop task");
static_assert(lifecycleDrainAction(WebUiLifecycleIntent::StartAp) == WebUiLifecycleAction::RunStartAp,
              "PRO-418: StartAp latch -> run start() in AP mode on loop task");
static_assert(lifecycleDrainAction(WebUiLifecycleIntent::Stop) == WebUiLifecycleAction::RunStop,
              "PRO-417: Stop latch -> run stop() on loop task");

// AP-mode resolution: only the AP start action is AP; station-start and stop are not (PRO-418).
static_assert(drainActionIsAp(WebUiLifecycleAction::RunStartAp), "PRO-418: RunStartAp -> apMode true");
static_assert(!drainActionIsAp(WebUiLifecycleAction::RunStartStation), "PRO-418: RunStartStation -> apMode false");
static_assert(!drainActionIsAp(WebUiLifecycleAction::RunStop), "PRO-418: RunStop -> apMode false (unused)");

#endif // WEBUILIFECYCLEDEFERPOLICY_H
