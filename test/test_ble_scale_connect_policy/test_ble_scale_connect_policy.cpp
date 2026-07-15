#include "../../src/display/plugins/BLEScaleConnectPolicy.h"
#include <unity.h>

static_assert(!isValidBleScaleConnectUuid(""));
static_assert(isValidBleScaleConnectUuid("AA:BB:CC:DD:EE:FF"));

// The `controller == nullptr` path in BLEScalePlugin::connect() is a plugin-state
// guard, not a connect-input validation rule. BLEScalePlugin's update() loop
// already returns before calling connect() when setup() has not assigned a
// controller, so the host-includable policy only covers the pure UUID input gate.

void setUp(void) {}
void tearDown(void) {}

void test_empty_uuid_is_invalid(void) { TEST_ASSERT_FALSE(isValidBleScaleConnectUuid("")); }

void test_nonempty_uuid_is_valid(void) { TEST_ASSERT_TRUE(isValidBleScaleConnectUuid("AA:BB:CC:DD:EE:FF")); }

static int runBleScaleConnectPolicyTests() {
    UNITY_BEGIN();
    RUN_TEST(test_empty_uuid_is_invalid);
    RUN_TEST(test_nonempty_uuid_is_valid);
    return UNITY_END();
}

#ifdef ARDUINO
void setup() { runBleScaleConnectPolicyTests(); }

void loop() {}
#else
int main(int argc, char **argv) { return runBleScaleConnectPolicyTests(); }
#endif
