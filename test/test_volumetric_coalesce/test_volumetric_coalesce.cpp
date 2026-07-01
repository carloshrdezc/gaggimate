#include <unity.h>

#include <display/core/VolumetricCoalescer.h>

// PRO-367: pins the coalesce-latest-on-failed-take behavior of the volumetric
// stop path.
//
// The bug: onVolumetricMeasurement() takes processMutex with a 10 ms fail-fast
// timeout and, on a FAILED take, USED TO silently DROP the scale measurement.
// Under diagnostic-log contention those takes fail, so the running BrewProcess's
// currentVolume never advanced toward the target and the volumetric yield stop
// never fired.
//
// The fix latches the freshest measurement outside the lock (coalesce-latest);
// the next SUCCESSFUL take applies that freshest value. Because the scale weight
// is monotonic cumulative, applying only the newest latched value loses no yield.
//
// These tests exercise the pure kernel (volumetric::Coalescer) that both the
// Controller stop path and the ShotHistory recording handler use.

using volumetric::Coalescer;

void setUp(void) {}
void tearDown(void) {}

// A failed take latches the value; the next successful take applies it. This is
// the core no-lost-weight guarantee: a weight that arrived while the mutex was
// contended is NOT dropped — it is applied on the next take.
void test_failed_take_then_successful_take_applies_latched_value(void) {
    Coalescer c;

    // Simulate a FAILED take: the handler latches but cannot apply.
    c.latch(18.0);
    TEST_ASSERT_TRUE(c.hasPending());

    // Simulate the next SUCCESSFUL take: it must apply the latched value.
    double applied = 0.0;
    TEST_ASSERT_TRUE(c.consumeInto(applied));
    TEST_ASSERT_EQUAL_FLOAT(18.0, applied);
    TEST_ASSERT_FALSE(c.hasPending());
}

// Multiple failed takes in a row must coalesce to the FRESHEST value, not the
// oldest — the newest cumulative weight subsumes every earlier one. The next
// successful take applies 30.0 (the last latch), never 22.0 or 26.0.
void test_multiple_failed_takes_coalesce_to_freshest(void) {
    Coalescer c;

    c.latch(22.0); // dropped-take #1
    c.latch(26.0); // dropped-take #2
    c.latch(30.0); // dropped-take #3

    double applied = 0.0;
    TEST_ASSERT_TRUE(c.consumeInto(applied));
    TEST_ASSERT_EQUAL_FLOAT(30.0, applied); // freshest, no lost weight
    TEST_ASSERT_FALSE(c.hasPending());
}

// The stop-critical scenario end to end: the target is 36 g. A weight of 36 g
// arrives while the take is contended (would previously be DROPPED). On the next
// successful take the coalesced 36 g is applied, so currentVolume reaches the
// target and the yield stop can fire. Before the fix this value was lost and the
// shot never stopped.
void test_target_weight_survives_a_contended_take(void) {
    Coalescer c;
    const double target = 36.0;

    // Weight climbs across contended takes; the take that would carry it to the
    // target times out and latches instead of dropping.
    c.latch(34.0);
    c.latch(target);

    double currentVolume = 0.0;
    TEST_ASSERT_TRUE(c.consumeInto(currentVolume));
    TEST_ASSERT_TRUE(currentVolume >= target); // stop condition now reachable
}

// consumeInto is a no-op when nothing is pending: a successful take that has no
// coalesced value must not clobber the caller's current value with a stale one.
void test_consume_when_nothing_pending_is_noop(void) {
    Coalescer c;

    double v = 12.5;
    TEST_ASSERT_FALSE(c.consumeInto(v));
    TEST_ASSERT_EQUAL_FLOAT(12.5, v); // untouched

    // After a latch+consume, a second consume with nothing pending is also a
    // no-op (the pending flag was cleared by the first consume).
    c.latch(40.0);
    double applied = 0.0;
    TEST_ASSERT_TRUE(c.consumeInto(applied));
    TEST_ASSERT_EQUAL_FLOAT(40.0, applied);

    double v2 = 40.0;
    TEST_ASSERT_FALSE(c.consumeInto(v2));
    TEST_ASSERT_EQUAL_FLOAT(40.0, v2);
}

// The normal (uncontended) path must be unaffected: latch-then-consume on every
// measurement behaves exactly like applying that measurement directly.
void test_uncontended_path_applies_each_measurement(void) {
    Coalescer c;

    const double series[] = {5.0, 11.0, 19.0, 28.0, 36.0};
    double currentVolume = 0.0;
    for (double m : series) {
        c.latch(m);
        double applied = currentVolume;
        TEST_ASSERT_TRUE(c.consumeInto(applied));
        currentVolume = applied;
        TEST_ASSERT_EQUAL_FLOAT(m, currentVolume);
        TEST_ASSERT_FALSE(c.hasPending());
    }
    TEST_ASSERT_EQUAL_FLOAT(36.0, currentVolume);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_failed_take_then_successful_take_applies_latched_value);
    RUN_TEST(test_multiple_failed_takes_coalesce_to_freshest);
    RUN_TEST(test_target_weight_survives_a_contended_take);
    RUN_TEST(test_consume_when_nothing_pending_is_noop);
    RUN_TEST(test_uncontended_path_applies_each_measurement);
    return UNITY_END();
}
