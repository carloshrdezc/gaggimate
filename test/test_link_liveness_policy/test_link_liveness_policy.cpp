#include "../../lib/GaggiMateController/src/LinkLivenessPolicy.h"
#include <unity.h>

// PRO-655 R1: controller link-liveness failsafe. GaggiMateController::loop() calls
// handlePingTimeout() (heater setpoint 0, pump 0, valve off, alt off) whenever
// pingTimedOut() is true. Only framed NanoPbComm ping/control payloads refresh the
// ping time, so with no framed traffic (no peer, legacy display, mismatched display
// that suppresses pings) outputs are forced off after PING_TIMEOUT_SECONDS.

static constexpr uint32_t TIMEOUT_S = 20999; // PING_TIMEOUT_MS (same trip point as 20 s integer)

void setUp(void) {}
void tearDown(void) {}

void test_no_trip_within_timeout(void) {
    TEST_ASSERT_FALSE(link_liveness::pingTimedOut(1000, 1000, TIMEOUT_S));
    TEST_ASSERT_FALSE(link_liveness::pingTimedOut(1000 + 20999, 1000, TIMEOUT_S)); // 20 whole s
}

void test_trips_after_timeout_with_no_traffic(void) {
    TEST_ASSERT_TRUE(link_liveness::pingTimedOut(1000 + 21000, 1000, TIMEOUT_S));
    TEST_ASSERT_TRUE(link_liveness::pingTimedOut(10u * 60 * 1000, 0, TIMEOUT_S));
}

void test_trips_from_boot_when_never_pinged(void) {
    // setup() seeds lastPingTime = millis(); a display that never sends framed
    // traffic (legacy NimBLEComm, R2) cannot keep outputs alive.
    const uint32_t bootSeed = 5000;
    TEST_ASSERT_FALSE(link_liveness::pingTimedOut(bootSeed + 10000, bootSeed, TIMEOUT_S));
    TEST_ASSERT_TRUE(link_liveness::pingTimedOut(bootSeed + 25000, bootSeed, TIMEOUT_S));
}

void test_no_trip_when_last_ping_just_after_now_race(void) {
    // NimBLE task stamps lastPing after loop() sampled now: negative interval.
    TEST_ASSERT_FALSE(link_liveness::pingTimedOut(1000, 1005, TIMEOUT_S));
    TEST_ASSERT_FALSE(link_liveness::pingTimedOut(0xFFFFFFF0u, 0x00000005u, TIMEOUT_S));
}

void test_exact_threshold_matches_legacy_integer_seconds(void) {
    TEST_ASSERT_FALSE(link_liveness::pingTimedOut(20999, 0, TIMEOUT_S));
    TEST_ASSERT_TRUE(link_liveness::pingTimedOut(21000, 0, TIMEOUT_S));
}

void test_healthy_link_across_millis_wrap_does_not_trip(void) {
    // Pings every 100 ms straddling the wrap.
    uint32_t last = 0xFFFFFF00u;
    for (uint32_t i = 0; i < 40; i++) {
        const uint32_t now = last + 100u; // wraps naturally
        TEST_ASSERT_FALSE(link_liveness::pingTimedOut(now, last, TIMEOUT_S));
        last = now;
    }
    TEST_ASSERT_FALSE(link_liveness::pingTimedOut(0x00000100u, 0xFFFFFF00u, TIMEOUT_S));
}

void test_drop_just_before_wrap_trips_about_20s_later(void) {
    const uint32_t last = 0xFFFFF000u; // 4096 ms before wrap
    TEST_ASSERT_FALSE(link_liveness::pingTimedOut(last + 20999u, last, TIMEOUT_S));
    TEST_ASSERT_TRUE(link_liveness::pingTimedOut(last + 21000u, last, TIMEOUT_S)); // = 0x00004208
    TEST_ASSERT_TRUE(link_liveness::pingTimedOut(0x00004E20u, last, TIMEOUT_S));
    // Stays tripped for the rest of the (half-range) window, not just the edge.
    TEST_ASSERT_TRUE(link_liveness::pingTimedOut(last + 3600000u, last, TIMEOUT_S));
}

void test_ms_after_is_wrap_safe(void) {
    TEST_ASSERT_TRUE(link_liveness::msAfter(0x00000010u, 0xFFFFFFF0u));
    TEST_ASSERT_FALSE(link_liveness::msAfter(0xFFFFFFF0u, 0x00000010u));
    TEST_ASSERT_FALSE(link_liveness::msAfter(5, 5));
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
    RUN_TEST(test_no_trip_when_last_ping_just_after_now_race);
    RUN_TEST(test_exact_threshold_matches_legacy_integer_seconds);
    RUN_TEST(test_healthy_link_across_millis_wrap_does_not_trip);
    RUN_TEST(test_drop_just_before_wrap_trips_about_20s_later);
    RUN_TEST(test_ms_after_is_wrap_safe);
    RUN_TEST(test_drop_link_only_on_transition_and_not_during_ota);
    return UNITY_END();
}
