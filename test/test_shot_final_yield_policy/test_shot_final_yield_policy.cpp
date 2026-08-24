#include "../../src/display/plugins/ShotFinalYieldPolicy.h"
#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

// PRO-647: a scale can emit its post-shot reset/tare reading while ShotHistory
// is still collecting final drips. Keep the captured yield instead of replacing
// it with that reset-like low reading.
void test_final_yield_holds_through_reset_to_low_measurement(void) {
    float finalYield = 36.4f;

    finalYield = finalShotYieldAfterMeasurement(finalYield, 0.1f);

    TEST_ASSERT_EQUAL_FLOAT(36.4f, finalYield);
}

// Legitimate final drips must remain observable during PRO-587's settle window.
void test_final_yield_increases_for_post_shot_drips(void) {
    float finalYield = 36.4f;

    finalYield = finalShotYieldAfterMeasurement(finalYield, 36.8f);

    TEST_ASSERT_EQUAL_FLOAT(36.8f, finalYield);
}

static int runShotFinalYieldPolicyTests() {
    UNITY_BEGIN();
    RUN_TEST(test_final_yield_holds_through_reset_to_low_measurement);
    RUN_TEST(test_final_yield_increases_for_post_shot_drips);
    return UNITY_END();
}

#ifdef ARDUINO
void setup() { runShotFinalYieldPolicyTests(); }
void loop() {}
#else
int main(int argc, char **argv) { return runShotFinalYieldPolicyTests(); }
#endif
