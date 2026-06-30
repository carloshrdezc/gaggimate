#include "../../src/display/plugins/MqttConnectPolicy.h"
#include <unity.h>

// PRO-348 (Ref PRO-346 F2): pure predicates for MQTTPlugin's multi-fire
// idempotency guard and off-WiFi-task connect deferral.
//
// shouldLatchMqttConnect(clientConnected): the controller:wifi:connect handler
// gate — latch a pending connect intent IFF the client is NOT already connected
// (re-fire while connected => skip; the mDNS `if (started) return;` analog).
//
// mqttLoopAction(wantConnect, clientConnected, attemptsSpent, maxAttempts): the
// loop() tick's non-blocking single-step state machine — None / publish discovery
// once + clear / one connect attempt / give up after the retry budget.
//
// Both are lifted unchanged from the handler/loop logic so they are host-testable
// in [env:native] without linking the MQTT client / WiFi / FreeRTOS.

// Mirror MQTT_CONNECTION_RETRIES (MQTTPlugin.h) without pulling in <MQTT.h>/<WiFi.h>.
static constexpr int kMaxAttempts = 5;

void setUp(void) {}
void tearDown(void) {}

// AC #1: idempotency guard. Re-fire while connected must NOT re-latch (=> skip
// connect() and discovery). Latch only when not connected.
void test_latch_only_when_not_connected(void) {
    TEST_ASSERT_TRUE(shouldLatchMqttConnect(/*connected=*/false));
    TEST_ASSERT_FALSE(shouldLatchMqttConnect(/*connected=*/true));
}

// No latch => loop() does nothing, regardless of connection state.
void test_no_latch_no_work(void) {
    TEST_ASSERT_EQUAL(static_cast<int>(MqttLoopAction::None),
                      static_cast<int>(mqttLoopAction(false, false, 0, kMaxAttempts)));
    TEST_ASSERT_EQUAL(static_cast<int>(MqttLoopAction::None),
                      static_cast<int>(mqttLoopAction(false, true, 0, kMaxAttempts)));
    TEST_ASSERT_EQUAL(static_cast<int>(MqttLoopAction::None),
                      static_cast<int>(mqttLoopAction(false, false, 3, kMaxAttempts)));
}

// AC #1/#3: latched + connected => publish discovery exactly once and clear the
// latch. Connection wins even if attempts remain (covers the "attempt just
// succeeded" path and the latch/connect race).
void test_latched_connected_publishes_and_clears(void) {
    TEST_ASSERT_EQUAL(static_cast<int>(MqttLoopAction::PublishDiscoveryAndClear),
                      static_cast<int>(mqttLoopAction(true, true, 0, kMaxAttempts)));
    TEST_ASSERT_EQUAL(static_cast<int>(MqttLoopAction::PublishDiscoveryAndClear),
                      static_cast<int>(mqttLoopAction(true, true, 3, kMaxAttempts)));
    TEST_ASSERT_EQUAL(static_cast<int>(MqttLoopAction::PublishDiscoveryAndClear),
                      static_cast<int>(mqttLoopAction(true, true, kMaxAttempts, kMaxAttempts)));
}

// AC #2: latched + not connected + budget remaining => ONE non-blocking attempt.
// Spanning ticks (attemptsSpent 0..maxAttempts-1) naturally retries — no delay()
// on the event task, no "never retries" regression.
void test_latched_not_connected_attempts_within_budget(void) {
    for (int spent = 0; spent < kMaxAttempts; spent++) {
        TEST_ASSERT_EQUAL(static_cast<int>(MqttLoopAction::AttemptConnect),
                          static_cast<int>(mqttLoopAction(true, false, spent, kMaxAttempts)));
    }
}

// Budget exhausted (mirrors the original blocking loop giving up after
// MQTT_CONNECTION_RETRIES failures): give up and clear so a later
// controller:wifi:connect re-fire re-latches with a fresh budget.
void test_budget_exhausted_gives_up(void) {
    TEST_ASSERT_EQUAL(static_cast<int>(MqttLoopAction::GiveUpAndClear),
                      static_cast<int>(mqttLoopAction(true, false, kMaxAttempts, kMaxAttempts)));
    TEST_ASSERT_EQUAL(static_cast<int>(MqttLoopAction::GiveUpAndClear),
                      static_cast<int>(mqttLoopAction(true, false, kMaxAttempts + 2, kMaxAttempts)));
}

// End-to-end first-connect walk (AC #3): not-connected latch -> N attempts ->
// connect succeeds -> publish once + clear. No re-publish on a subsequent
// re-fire while connected (handler does not re-latch).
void test_first_connect_sequence(void) {
    // Tick 1..k: attempting (still not connected).
    TEST_ASSERT_EQUAL(static_cast<int>(MqttLoopAction::AttemptConnect),
                      static_cast<int>(mqttLoopAction(true, false, 0, kMaxAttempts)));
    TEST_ASSERT_EQUAL(static_cast<int>(MqttLoopAction::AttemptConnect),
                      static_cast<int>(mqttLoopAction(true, false, 1, kMaxAttempts)));
    // Attempt succeeds: now connected, latch still set -> publish once + clear.
    TEST_ASSERT_EQUAL(static_cast<int>(MqttLoopAction::PublishDiscoveryAndClear),
                      static_cast<int>(mqttLoopAction(true, true, 2, kMaxAttempts)));
    // Latch cleared by loop(): subsequent ticks are no-ops even though connected.
    TEST_ASSERT_EQUAL(static_cast<int>(MqttLoopAction::None),
                      static_cast<int>(mqttLoopAction(false, true, 0, kMaxAttempts)));
    // Re-fire while connected: handler refuses to re-latch (no re-begin/re-publish).
    TEST_ASSERT_FALSE(shouldLatchMqttConnect(true));
}

static int runMqttConnectPolicyTests() {
    UNITY_BEGIN();
    RUN_TEST(test_latch_only_when_not_connected);
    RUN_TEST(test_no_latch_no_work);
    RUN_TEST(test_latched_connected_publishes_and_clears);
    RUN_TEST(test_latched_not_connected_attempts_within_budget);
    RUN_TEST(test_budget_exhausted_gives_up);
    RUN_TEST(test_first_connect_sequence);
    return UNITY_END();
}

#ifdef ARDUINO
void setup() { runMqttConnectPolicyTests(); }

void loop() {}
#else
int main(int argc, char **argv) { return runMqttConnectPolicyTests(); }
#endif
