#include "../../src/display/plugins/VolumetricSourcePolicy.h"
#include <unity.h>

// PRO-4: pure predicate for the mid-shot volumetric source fallback.
//
// shouldFallBackToFlowEstimation(latched, incoming, bluetoothScaleHealthy)
// decides whether Controller::onVolumetricMeasurement() should promote the
// latched source from BLUETOOTH to FLOW_ESTIMATION so the volume reading keeps
// advancing when the BLE scale dies mid-shot. It is a ONE-WAY fall-forward,
// debounced by isBluetoothScaleHealthy() (which only reports unhealthy once the
// 1.5 s grace window has elapsed). Extracted unchanged so it is host-testable in
// [env:native] without linking Controller/BLE/LVGL/FreeRTOS.

using Src = VolumetricMeasurementSource;

// Compile-time truth table (mirrors the header's static_asserts).
static_assert(shouldFallBackToFlowEstimation(Src::BLUETOOTH, Src::FLOW_ESTIMATION, false),
              "BLUETOOTH + unhealthy scale + flow sample -> fall forward");
static_assert(!shouldFallBackToFlowEstimation(Src::BLUETOOTH, Src::FLOW_ESTIMATION, true),
              "healthy scale (within grace) -> no switch");

void setUp(void) {}
void tearDown(void) {}

// The one case that falls back: shot started on BLUETOOTH, the scale has gone
// unhealthy (grace window elapsed), and a flow-estimation sample arrives.
void test_falls_forward_when_bluetooth_dies_midshot(void) {
    TEST_ASSERT_TRUE(shouldFallBackToFlowEstimation(Src::BLUETOOTH, Src::FLOW_ESTIMATION, false));
}

// Debounce: while the scale is still healthy (a BLE measurement landed within
// the grace window, or volumetricOverride is set), do NOT switch — flow samples
// stay rejected and BLUETOOTH remains authoritative.
void test_debounced_while_scale_healthy(void) {
    TEST_ASSERT_FALSE(shouldFallBackToFlowEstimation(Src::BLUETOOTH, Src::FLOW_ESTIMATION, true));
}

// A BLUETOOTH sample never triggers the fallback, healthy or not (that is the
// normal consume path / a stray late BLE read).
void test_bluetooth_sample_never_switches(void) {
    TEST_ASSERT_FALSE(shouldFallBackToFlowEstimation(Src::BLUETOOTH, Src::BLUETOOTH, false));
    TEST_ASSERT_FALSE(shouldFallBackToFlowEstimation(Src::BLUETOOTH, Src::BLUETOOTH, true));
}

// One-way only: a shot latched on FLOW_ESTIMATION never switches to BLUETOOTH
// mid-shot, and re-adopting FLOW_ESTIMATION is a no-op switch (idempotent).
void test_flow_estimation_never_switches_back(void) {
    TEST_ASSERT_FALSE(shouldFallBackToFlowEstimation(Src::FLOW_ESTIMATION, Src::BLUETOOTH, true));
    TEST_ASSERT_FALSE(shouldFallBackToFlowEstimation(Src::FLOW_ESTIMATION, Src::BLUETOOTH, false));
    TEST_ASSERT_FALSE(shouldFallBackToFlowEstimation(Src::FLOW_ESTIMATION, Src::FLOW_ESTIMATION, false));
}

// No shot in progress: never switch.
void test_inactive_never_switches(void) {
    TEST_ASSERT_FALSE(shouldFallBackToFlowEstimation(Src::INACTIVE, Src::FLOW_ESTIMATION, false));
    TEST_ASSERT_FALSE(shouldFallBackToFlowEstimation(Src::INACTIVE, Src::BLUETOOTH, false));
}

static int runVolumetricSourcePolicyTests() {
    UNITY_BEGIN();
    RUN_TEST(test_falls_forward_when_bluetooth_dies_midshot);
    RUN_TEST(test_debounced_while_scale_healthy);
    RUN_TEST(test_bluetooth_sample_never_switches);
    RUN_TEST(test_flow_estimation_never_switches_back);
    RUN_TEST(test_inactive_never_switches);
    return UNITY_END();
}

#ifdef ARDUINO
void setup() { runVolumetricSourcePolicyTests(); }

void loop() {}
#else
int main(int argc, char **argv) { return runVolumetricSourcePolicyTests(); }
#endif
