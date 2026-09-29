#include "../../lib/GaggiMateController/src/LinkLivenessPolicy.h"
#include <unity.h>

// PRO-655 R1: controller link-liveness failsafe. GaggiMateController::loop() calls
// handlePingTimeout() (heater setpoint 0, pump 0, valve off, alt off) whenever
// pingTimedOut() is true. Only framed NanoPbComm ping/control payloads refresh the
// ping time, so with no framed traffic (no peer, legacy display, mismatched display
// that suppresses pings) outputs are forced off after PING_TIMEOUT_SECONDS.

static constexpr double TIMEOUT_S = 20.0; // PING_TIMEOUT_SECONDS

void setUp(void) {}
void tearDown(void) {}

void test_no_trip_within_timeout(void) {
    TEST_ASSERT_FALSE(link_liveness::pingTimedOut(1000, 1000, TIMEOUT_S));
    TEST_ASSERT_FALSE(link_liveness::pingTimedOut(1000 + 20999, 1000, TIMEOUT_S)); // 20 whole s
}

void test_trips_after_timeout_with_no_traffic(void) {
    TEST_ASSERT_TRUE(link_liveness::pingTimedOut(1000 + 21000, 1000, TIMEOUT_S));
    TEST_ASSERT_TRUE(link_liveness::pingTimedOut(10UL * 60 * 1000, 0, TIMEOUT_S));
}

void test_trips_from_boot_when_never_pinged(void) {
    // setup() seeds lastPingTime = millis(); a display that never sends framed
    // traffic (legacy NimBLEComm, R2) cannot keep outputs alive.
    const unsigned long bootSeed = 5000;
    TEST_ASSERT_FALSE(link_liveness::pingTimedOut(bootSeed + 10000, bootSeed, TIMEOUT_S));
    TEST_ASSERT_TRUE(link_liveness::pingTimedOut(bootSeed + 25000, bootSeed, TIMEOUT_S));
}

void test_no_trip_when_last_ping_not_before_now(void) {
    TEST_ASSERT_FALSE(link_liveness::pingTimedOut(1000, 5000, TIMEOUT_S));
}

void test_drop_link_only_on_transition_and_not_during_ota(void) {
    TEST_ASSERT_TRUE(link_liveness::shouldDropLinkOnTimeout(false, false));
    TEST_ASSERT_FALSE(link_liveness::shouldDropLinkOnTimeout(true, false));
    TEST_ASSERT_FALSE(link_liveness::shouldDropLinkOnTimeout(false, true));
    TEST_ASSERT_FALSE(link_liveness::shouldDropLinkOnTimeout(true, true));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_no_trip_within_timeout);
    RUN_TEST(test_trips_after_timeout_with_no_traffic);
    RUN_TEST(test_trips_from_boot_when_never_pinged);
    RUN_TEST(test_no_trip_when_last_ping_not_before_now);
    RUN_TEST(test_drop_link_only_on_transition_and_not_during_ota);
    return UNITY_END();
}
