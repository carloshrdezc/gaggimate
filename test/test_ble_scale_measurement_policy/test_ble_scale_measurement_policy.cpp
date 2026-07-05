#include "../../src/display/plugins/BLEScaleMeasurementPolicy.h"
#include <cmath>
#include <unity.h>

// PRO-386: locks in the BLEScalePlugin gate-then-cache ordering.
// BLEScalePlugin::onMeasurement caches a value into `lastWeight` (read by
// getLastWeight()) and forwards it to the controller ONLY after the
// measurement-validity gate passes. That gate is now a single source of truth,
// isValidBleScaleMeasurement (BLEScaleMeasurementPolicy.h), which onMeasurement
// calls before `lastWeight = value`. Because the cache write is strictly
// downstream of this predicate returning true, a false result provably leaves
// getLastWeight() unchanged. This test asserts that contract at the predicate
// level (the required coverage) and, via a tiny host stand-in, at the
// cache-update level.

void setUp(void) {}
void tearDown(void) {}

// Rejected measurements: NaN / +/-inf / out of range. These would NOT update
// lastWeight (the gate returns false, so onMeasurement returns before caching).
void test_invalid_measurements_are_rejected(void) {
    TEST_ASSERT_FALSE(isValidBleScaleMeasurement(NAN));
    TEST_ASSERT_FALSE(isValidBleScaleMeasurement(INFINITY));
    TEST_ASSERT_FALSE(isValidBleScaleMeasurement(-INFINITY));
    TEST_ASSERT_FALSE(isValidBleScaleMeasurement(-1000.0001f)); // just below lower bound
    TEST_ASSERT_FALSE(isValidBleScaleMeasurement(10000.001f));  // just above upper bound
    TEST_ASSERT_FALSE(isValidBleScaleMeasurement(-2000.0f));
    TEST_ASSERT_FALSE(isValidBleScaleMeasurement(20000.0f));
}

// Accepted measurements: finite and in [-1000, 10000], boundaries inclusive
// (matches the production `< -1000` / `> 10000` rejects). These WOULD update
// lastWeight and be forwarded to the controller.
void test_valid_measurements_are_accepted(void) {
    TEST_ASSERT_TRUE(isValidBleScaleMeasurement(0.0f));
    TEST_ASSERT_TRUE(isValidBleScaleMeasurement(18.5f));
    TEST_ASSERT_TRUE(isValidBleScaleMeasurement(-1000.0f)); // lower boundary (inclusive)
    TEST_ASSERT_TRUE(isValidBleScaleMeasurement(10000.0f)); // upper boundary (inclusive)
    TEST_ASSERT_TRUE(isValidBleScaleMeasurement(250.0f));
}

// Host stand-in mirroring onMeasurement's gate-then-cache ordering
// (`if (isValid(v)) cache = v;`) to assert getLastWeight() semantics directly:
// a rejected value must leave the cache untouched; an accepted value updates it.
void test_gate_then_cache_leaves_cache_unchanged_on_reject(void) {
    float cache = 42.0f; // stand-in for BLEScalePlugin::lastWeight

    // A rejected measurement must not touch the cache.
    if (isValidBleScaleMeasurement(NAN)) {
        cache = NAN;
    }
    TEST_ASSERT_EQUAL_FLOAT(42.0f, cache); // getLastWeight() unchanged

    if (isValidBleScaleMeasurement(20000.0f)) {
        cache = 20000.0f;
    }
    TEST_ASSERT_EQUAL_FLOAT(42.0f, cache); // still unchanged

    // An accepted measurement updates the cache.
    if (isValidBleScaleMeasurement(18.5f)) {
        cache = 18.5f;
    }
    TEST_ASSERT_EQUAL_FLOAT(18.5f, cache); // getLastWeight() now reflects the value
}

static int runBleScaleMeasurementPolicyTests() {
    UNITY_BEGIN();
    RUN_TEST(test_invalid_measurements_are_rejected);
    RUN_TEST(test_valid_measurements_are_accepted);
    RUN_TEST(test_gate_then_cache_leaves_cache_unchanged_on_reject);
    return UNITY_END();
}

#ifdef ARDUINO
void setup() { runBleScaleMeasurementPolicyTests(); }

void loop() {}
#else
int main(int argc, char **argv) { return runBleScaleMeasurementPolicyTests(); }
#endif
