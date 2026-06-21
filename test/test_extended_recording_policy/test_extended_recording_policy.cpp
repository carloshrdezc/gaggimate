#include "../../src/display/plugins/ExtendedRecordingPolicy.h"
#include <unity.h>

// PRO-232: the post-stop settle window must open exactly when a live BLE scale
// was the active volumetric source for the just-ended shot — independent of the
// last instantaneous weight sample (which could be 0/stale during the
// BLUETOOTH_GRACE_PERIOD_MS source switch). The old `currentBluetoothWeight > 0`
// precondition let that stale 0 skip the window, releasing the auto-steam gate
// early and cutting the recorded yield short.

// Compile-time guarantees of the truth table.
static_assert(shouldOpenExtendedRecording(true, true, true), "live BLE scale brew must open the settle window");
static_assert(!shouldOpenExtendedRecording(true, true, false), "no live BLE scale -> no settle (steam engages immediately)");
static_assert(!shouldOpenExtendedRecording(false, true, true), "not recording -> never open");
static_assert(!shouldOpenExtendedRecording(true, false, true), "manual end (allowExtended=false) -> never open");

void setUp(void) {}
void tearDown(void) {}

void test_opens_when_ble_scale_live_during_recording(void) {
    // The core PRO-232 fix: open even though no positive weight is required.
    TEST_ASSERT_TRUE(shouldOpenExtendedRecording(true, true, true));
}

void test_no_settle_without_live_ble_scale(void) {
    // Flow-estimation / time-based / no-scale shots: scale not healthy -> no window,
    // so auto-steam engages immediately with no spurious 3s delay.
    TEST_ASSERT_FALSE(shouldOpenExtendedRecording(true, true, false));
}

void test_does_not_open_when_not_recording(void) {
    TEST_ASSERT_FALSE(shouldOpenExtendedRecording(false, true, true));
}

void test_manual_end_path_never_opens(void) {
    // controller:process:end (MODE_MANUAL) calls endRecording(false): must not settle.
    TEST_ASSERT_FALSE(shouldOpenExtendedRecording(true, false, true));
    TEST_ASSERT_FALSE(shouldOpenExtendedRecording(true, false, false));
}

static int runExtendedRecordingPolicyTests() {
    UNITY_BEGIN();
    RUN_TEST(test_opens_when_ble_scale_live_during_recording);
    RUN_TEST(test_no_settle_without_live_ble_scale);
    RUN_TEST(test_does_not_open_when_not_recording);
    RUN_TEST(test_manual_end_path_never_opens);
    return UNITY_END();
}

#ifdef ARDUINO
void setup() { runExtendedRecordingPolicyTests(); }

void loop() {}
#else
int main(int argc, char **argv) { return runExtendedRecordingPolicyTests(); }
#endif
