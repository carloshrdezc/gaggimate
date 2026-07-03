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

// A connect event latches Start; a disconnect latches Stop, from the idle None.
void test_latch_from_none(void) {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WebUiLifecycleIntent::Start),
                          static_cast<int>(latchLifecycleIntent(WebUiLifecycleIntent::None, WebUiLifecycleIntent::Start)));
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
                          static_cast<int>(latchLifecycleIntent(WebUiLifecycleIntent::Start, WebUiLifecycleIntent::Stop)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WebUiLifecycleIntent::Start),
                          static_cast<int>(latchLifecycleIntent(WebUiLifecycleIntent::Stop, WebUiLifecycleIntent::Start)));
}

// A None "request" is a no-op that preserves any still-pending latch. (The
// production code never stores None from an event handler — loop() clears the
// latch via exchange(None) — but the coalescer must not let a stray None clobber
// a real pending intent.)
void test_latch_none_preserves_pending(void) {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WebUiLifecycleIntent::Stop),
                          static_cast<int>(latchLifecycleIntent(WebUiLifecycleIntent::Stop, WebUiLifecycleIntent::None)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WebUiLifecycleIntent::Start),
                          static_cast<int>(latchLifecycleIntent(WebUiLifecycleIntent::Start, WebUiLifecycleIntent::None)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WebUiLifecycleIntent::None),
                          static_cast<int>(latchLifecycleIntent(WebUiLifecycleIntent::None, WebUiLifecycleIntent::None)));
}

// --- Drain mapping: latched intent -> loop() action -------------------------

// No latch -> loop() does no lifecycle work (the common case, every tick).
void test_drain_none_is_noop(void) {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WebUiLifecycleAction::None),
                          static_cast<int>(lifecycleDrainAction(WebUiLifecycleIntent::None)));
}

// A Start latch drains to RunStart (loop() calls start() on the loop task); a
// Stop latch drains to RunStop (loop() calls stop() there). This is the whole
// point of the fix: the heavy call runs on the loop task, never inline on the
// WiFi-event task.
void test_drain_maps_intent_to_action(void) {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WebUiLifecycleAction::RunStart),
                          static_cast<int>(lifecycleDrainAction(WebUiLifecycleIntent::Start)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WebUiLifecycleAction::RunStop),
                          static_cast<int>(lifecycleDrainAction(WebUiLifecycleIntent::Stop)));
}

// End-to-end at the pure level: simulate a storm of N ASSOC_LEAVE/reassoc edges
// arriving between two loop() drains, then one drain. The coalesced latch
// reflects only the LAST edge, and the drain runs exactly ONE action — never N
// blocking start()/stop() calls. This is the property that keeps the WiFi-event
// task (and, post-fix, the loop task) from doing O(disconnects) heavy teardown.
void test_storm_coalesces_to_single_final_action(void) {
    // Edges as they arrive on the WiFi-event task (associate=Start, leave=Stop):
    const WebUiLifecycleIntent edges[] = {
        WebUiLifecycleIntent::Stop,  WebUiLifecycleIntent::Start, WebUiLifecycleIntent::Stop,
        WebUiLifecycleIntent::Start, WebUiLifecycleIntent::Stop,  WebUiLifecycleIntent::Start,
    };
    WebUiLifecycleIntent latch = WebUiLifecycleIntent::None;
    for (const WebUiLifecycleIntent e : edges) {
        latch = latchLifecycleIntent(latch, e);
    }
    // Final edge was Start -> exactly one RunStart, regardless of the 6 edges.
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WebUiLifecycleAction::RunStart), static_cast<int>(lifecycleDrainAction(latch)));

    // If the storm had ended on a leave, the single drained action is RunStop.
    WebUiLifecycleIntent latch2 = WebUiLifecycleIntent::Start;
    latch2 = latchLifecycleIntent(latch2, WebUiLifecycleIntent::Stop);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WebUiLifecycleAction::RunStop), static_cast<int>(lifecycleDrainAction(latch2)));
}

static int runWebUiLifecycleDeferPolicyTests() {
    UNITY_BEGIN();
    RUN_TEST(test_latch_from_none);
    RUN_TEST(test_latch_last_event_wins);
    RUN_TEST(test_latch_none_preserves_pending);
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
