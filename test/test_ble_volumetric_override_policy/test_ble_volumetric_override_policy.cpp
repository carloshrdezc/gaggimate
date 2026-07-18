#include "../../src/display/plugins/BLEVolumetricOverridePolicy.h"
#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

// PRO-542: a connected active scale keeps BLE volumetric available, but the
// override must clear as soon as that scale tears down.
void test_connected_scale_teardown_clears_ble_volumetric_override(void) {
    bool overrideEnabled = shouldEnableBleVolumetricOverride(true, true);
    TEST_ASSERT_TRUE(overrideEnabled);

    overrideEnabled = shouldEnableBleVolumetricOverride(false, false);
    TEST_ASSERT_FALSE(overrideEnabled);
}

static int runBleVolumetricOverridePolicyTests() {
    UNITY_BEGIN();
    RUN_TEST(test_connected_scale_teardown_clears_ble_volumetric_override);
    return UNITY_END();
}

#ifdef ARDUINO
void setup() { runBleVolumetricOverridePolicyTests(); }

void loop() {}
#else
int main(int argc, char **argv) { return runBleVolumetricOverridePolicyTests(); }
#endif
