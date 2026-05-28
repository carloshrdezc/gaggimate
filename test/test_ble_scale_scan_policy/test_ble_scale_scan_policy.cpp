#include "../../src/display/plugins/BLEScaleScanPolicy.h"
#include <display/core/constants.h>
#include <unity.h>

static_assert(!shouldScanForBleScaleMode(MODE_STANDBY));
static_assert(!shouldScanForBleScaleMode(MODE_STEAM));
static_assert(!shouldScanForBleScaleMode(MODE_WATER));
static_assert(shouldScanForBleScaleMode(MODE_BREW));
static_assert(shouldScanForBleScaleMode(MODE_GRIND));
static_assert(shouldScanForBleScaleMode(MODE_MANUAL));

void test_scale_scan_skips_modes_that_do_not_use_scale_data(void) {
    TEST_ASSERT_FALSE(shouldScanForBleScaleMode(MODE_STANDBY));
    TEST_ASSERT_FALSE(shouldScanForBleScaleMode(MODE_STEAM));
    TEST_ASSERT_FALSE(shouldScanForBleScaleMode(MODE_WATER));
}

void test_scale_scan_runs_for_modes_that_can_use_scale_data(void) {
    TEST_ASSERT_TRUE(shouldScanForBleScaleMode(MODE_BREW));
    TEST_ASSERT_TRUE(shouldScanForBleScaleMode(MODE_GRIND));
    TEST_ASSERT_TRUE(shouldScanForBleScaleMode(MODE_MANUAL));
}

static int runBleScaleScanPolicyTests() {
    UNITY_BEGIN();
    RUN_TEST(test_scale_scan_skips_modes_that_do_not_use_scale_data);
    RUN_TEST(test_scale_scan_runs_for_modes_that_can_use_scale_data);
    return UNITY_END();
}

#ifdef ARDUINO
void setup() { runBleScaleScanPolicyTests(); }

void loop() {}
#else
int main(int argc, char **argv) { return runBleScaleScanPolicyTests(); }
#endif
