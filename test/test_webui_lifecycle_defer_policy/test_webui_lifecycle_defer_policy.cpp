#include "display/plugins/WebUiLifecycleDeferPolicy.h"
#include <unity.h>

// PRO-417: host tests for the pure web-server lifecycle defer policy.
//
// Crash context: on nightly-250 the device panic-reboots during WiFi
// ASSOC_LEAVE churn. The symbolicated coredump shows an interrupt-context
// StoreProhibited inside the vendored WPA-supplicant crypto while IDLE0's stack
// is nearly exhausted. The store-to-null is in the closed-source esp_wifi blob,
// so the actionable in-firmware contributor is WebUIPlugin running heavy,
// blocking teardown (stop()/stopRelay(): ~500 ms spin-wait + ws.closeAll() under
// wsMutex) INLINE on the arduino_events WiFi-event task, once per ASSOC_LEAVE,
// while the supplicant is mid-(re)association. The fix moves start()/stop() off
// the WiFi-event task: the event handlers only latch a lifecycle intent and
// loop() (Arduino loop task) drains it — mirroring the mDNS/MQTT/OTA-start defer
// discipline already in this codebase.
//
// The actual task starvation / WDT panic is a runtime FreeRTOS condition and
// cannot be reproduced on host. What these tests pin is the DECISION logic the
// fix encodes: the latch coalescing (last WiFi event wins) and the drain mapping
// (latched intent -> run start()/stop() or nothing). The production wiring in
// WebUIPlugin.cpp (event handlers store an intent; loop() exchange-and-drains it
// on the loop task) is validated by build; static_asserts in the header pin the
// truth table at compile time; these RUN_TESTs make the contract explicit and
// executable in [env:native].

void setUp(void) {}
void tearDown(void) {}

// --- Latch coalescing: last WiFi event wins ---------------------------------

// A connect event latches a Start intent that carries the captive-portal mode
// (AP=1 -> StartAp, AP=0 -> StartStation); a disconnect latches Stop, from idle
// None. PRO-418: the mode is folded into the intent, so one atomic conveys both.
void test_latch_from_none(void) {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WebUiLifecycleIntent::StartStation),
                          static_cast<int>(latchLifecycleIntent(WebUiLifecycleIntent::None, startIntentForApMode(false))));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WebUiLifecycleIntent::StartAp),
                          static_cast<int>(latchLifecycleIntent(WebUiLifecycleIntent::None, startIntentForApMode(true))));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WebUiLifecycleIntent::Stop),
                          static_cast<int>(latchLifecycleIntent(WebUiLifecycleIntent::None, WebUiLifecycleIntent::Stop)));
}

// The core ASSOC_LEAVE-churn case: connect then disconnect arrive before loop()
// drains. Only the final state matters, so Stop must win — the server ends up
// down, exactly as if only the disconnect had happened. And the reverse:
// disconnect then reconnect coalesces to Start (server ends up back up). This is
// what makes coalescing safe: no queue, no lost intermediate work, no thrash of
// N start()/stop() pairs on a storm — just one drain to the final state.
void test_latch_last_event_wins(void) {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WebUiLifecycleIntent::Stop),
                          static_cast<int>(latchLifecycleIntent(WebUiLifecycleIntent::StartStation, WebUiLifecycleIntent::Stop)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WebUiLifecycleIntent::StartAp),
                          static_cast<int>(latchLifecycleIntent(WebUiLifecycleIntent::Stop, startIntentForApMode(true))));
}

// A None "request" is a no-op that preserves any still-pending latch. (The
// production code never stores None from an event handler — loop() clears the
// latch via exchange(None) — but the coalescer must not let a stray None clobber
// a real pending intent.)
void test_latch_none_preserves_pending(void) {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WebUiLifecycleIntent::Stop),
                          static_cast<int>(latchLifecycleIntent(WebUiLifecycleIntent::Stop, WebUiLifecycleIntent::None)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WebUiLifecycleIntent::StartStation),
                          static_cast<int>(latchLifecycleIntent(WebUiLifecycleIntent::StartStation, WebUiLifecycleIntent::None)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WebUiLifecycleIntent::None),
                          static_cast<int>(latchLifecycleIntent(WebUiLifecycleIntent::None, WebUiLifecycleIntent::None)));
}

// PRO-418: the AP mode is atomic with the Start decision. A connect(AP=1)
// followed by a drain must yield an AP start — never a station start paired with
// a stale-or-fresh AP flag from a second atomic. This is the ordering contract
// the enum-fold establishes by construction: because the mode IS the intent, a
// drained StartAp can only ever mean apMode==true and StartStation apMode==false.
void test_ap_mode_rides_the_intent(void) {
    // connect(AP=1) -> drain -> AP start (apMode==true).
    const WebUiLifecycleIntent apLatch = latchLifecycleIntent(WebUiLifecycleIntent::None, startIntentForApMode(true));
    const WebUiLifecycleAction apAction = lifecycleDrainAction(apLatch);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WebUiLifecycleAction::RunStartAp), static_cast<int>(apAction));
    TEST_ASSERT_TRUE(drainActionIsAp(apAction));

    // connect(AP=0) -> drain -> station start (apMode==false).
    const WebUiLifecycleIntent staLatch = latchLifecycleIntent(WebUiLifecycleIntent::None, startIntentForApMode(false));
    const WebUiLifecycleAction staAction = lifecycleDrainAction(staLatch);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WebUiLifecycleAction::RunStartStation), static_cast<int>(staAction));
    TEST_ASSERT_FALSE(drainActionIsAp(staAction));

    // A later connect's mode overwrites an earlier one (last event wins), so the
    // drained mode always matches the FINAL connect — no cross-atomic staleness.
    const WebUiLifecycleIntent flipped = latchLifecycleIntent(startIntentForApMode(false), startIntentForApMode(true));
    TEST_ASSERT_TRUE(drainActionIsAp(lifecycleDrainAction(flipped)));
    const WebUiLifecycleIntent flippedBack = latchLifecycleIntent(startIntentForApMode(true), startIntentForApMode(false));
    TEST_ASSERT_FALSE(drainActionIsAp(lifecycleDrainAction(flippedBack)));
}

// --- Drain mapping: latched intent -> loop() action -------------------------

// No latch -> loop() does no lifecycle work (the common case, every tick).
void test_drain_none_is_noop(void) {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WebUiLifecycleAction::None),
                          static_cast<int>(lifecycleDrainAction(WebUiLifecycleIntent::None)));
}

// A StartStation latch drains to RunStartStation and a StartAp latch to
// RunStartAp (loop() calls start() in the matching mode on the loop task); a
// Stop latch drains to RunStop (loop() calls stop() there). This is the whole
// point of the fix: the heavy call runs on the loop task, never inline on the
// WiFi-event task.
void test_drain_maps_intent_to_action(void) {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WebUiLifecycleAction::RunStartStation),
                          static_cast<int>(lifecycleDrainAction(WebUiLifecycleIntent::StartStation)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WebUiLifecycleAction::RunStartAp),
                          static_cast<int>(lifecycleDrainAction(WebUiLifecycleIntent::StartAp)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WebUiLifecycleAction::RunStop),
                          static_cast<int>(lifecycleDrainAction(WebUiLifecycleIntent::Stop)));
}

// End-to-end at the pure level: simulate a storm of N ASSOC_LEAVE/reassoc edges
// arriving between two loop() drains, then one drain. The coalesced latch
// reflects only the LAST edge, and the drain runs exactly ONE action — never N
// blocking start()/stop() calls. This is the property that keeps the WiFi-event
// task (and, post-fix, the loop task) from doing O(disconnects) heavy teardown.
void test_storm_coalesces_to_single_final_action(void) {
    // Edges as they arrive on the WiFi-event task (associate=Start, leave=Stop).
    // PRO-418: the associate edges carry a mode; alternate AP/STA to prove the
    // coalesced latch reflects the LAST edge's mode too, not just Start-vs-Stop.
    const WebUiLifecycleIntent edges[] = {
        WebUiLifecycleIntent::Stop,  startIntentForApMode(true), WebUiLifecycleIntent::Stop,
        startIntentForApMode(false), WebUiLifecycleIntent::Stop, startIntentForApMode(false),
    };
    WebUiLifecycleIntent latch = WebUiLifecycleIntent::None;
    for (const WebUiLifecycleIntent e : edges) {
        latch = latchLifecycleIntent(latch, e);
    }
    // Final edge was a station connect -> exactly one RunStartStation, regardless
    // of the 6 edges, and apMode resolves to false (the final connect's mode).
    const WebUiLifecycleAction action = lifecycleDrainAction(latch);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WebUiLifecycleAction::RunStartStation), static_cast<int>(action));
    TEST_ASSERT_FALSE(drainActionIsAp(action));

    // If the storm had ended on a leave, the single drained action is RunStop.
    WebUiLifecycleIntent latch2 = WebUiLifecycleIntent::StartStation;
    latch2 = latchLifecycleIntent(latch2, WebUiLifecycleIntent::Stop);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WebUiLifecycleAction::RunStop), static_cast<int>(lifecycleDrainAction(latch2)));
}

static int runWebUiLifecycleDeferPolicyTests() {
    UNITY_BEGIN();
    RUN_TEST(test_latch_from_none);
    RUN_TEST(test_latch_last_event_wins);
    RUN_TEST(test_latch_none_preserves_pending);
    RUN_TEST(test_ap_mode_rides_the_intent);
    RUN_TEST(test_drain_none_is_noop);
    RUN_TEST(test_drain_maps_intent_to_action);
    RUN_TEST(test_storm_coalesces_to_single_final_action);
    return UNITY_END();
}

#ifdef ARDUINO
void setup() { runWebUiLifecycleDeferPolicyTests(); }

void loop() {}
#else
int main(int argc, char **argv) { return runWebUiLifecycleDeferPolicyTests(); }
#endif
