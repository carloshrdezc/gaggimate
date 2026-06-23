#include "../../src/display/plugins/BLEScaleScanPolicy.h"
#include "../../src/display/plugins/PostStopGracePolicy.h"
#include <display/core/constants.h>
#include <unity.h>

// PRO-248: the BLE scale-alive steam grace and ShotHistory's extended-recording
// window must come from ONE source of truth so they can never diverge again.
// The unified hard cap is the requested 10 s. (The actual derivation of
// STEAM_SCALE_GRACE_PERIOD_MS / EXTENDED_RECORDING_DURATION from this constant is
// proven at compile time by static_asserts in BLEScalePlugin.h and
// ShotHistoryPlugin.h, which fire during the firmware build — those headers pull
// in hardware-only dependencies and are not host-includable.)
static_assert(POST_STOP_GRACE_DURATION_MS == 10000, "PRO-248: unified post-stop grace must be 10s");

static_assert(!shouldScanForBleScaleMode(MODE_STANDBY));
static_assert(!shouldScanForBleScaleMode(MODE_STEAM));
static_assert(!shouldScanForBleScaleMode(MODE_WATER));
static_assert(shouldScanForBleScaleMode(MODE_BREW));
static_assert(shouldScanForBleScaleMode(MODE_GRIND));
static_assert(shouldScanForBleScaleMode(MODE_MANUAL));

void setUp(void) {}
void tearDown(void) {}

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

// Steam grace window opens only when leaving a scanning mode into STEAM.
static_assert(shouldStartSteamScaleGrace(MODE_BREW, MODE_STEAM));
static_assert(shouldStartSteamScaleGrace(MODE_GRIND, MODE_STEAM));
static_assert(shouldStartSteamScaleGrace(MODE_MANUAL, MODE_STEAM));
static_assert(!shouldStartSteamScaleGrace(MODE_STANDBY, MODE_STEAM));
static_assert(!shouldStartSteamScaleGrace(MODE_WATER, MODE_STEAM));
static_assert(!shouldStartSteamScaleGrace(MODE_STEAM, MODE_STEAM));
static_assert(!shouldStartSteamScaleGrace(MODE_BREW, MODE_STANDBY));
static_assert(!shouldStartSteamScaleGrace(MODE_BREW, MODE_WATER));
static_assert(!shouldStartSteamScaleGrace(MODE_BREW, MODE_GRIND));

void test_steam_grace_opens_only_from_scanning_mode_into_steam(void) {
    // From a scanning mode into STEAM -> open the grace window.
    TEST_ASSERT_TRUE(shouldStartSteamScaleGrace(MODE_BREW, MODE_STEAM));
    TEST_ASSERT_TRUE(shouldStartSteamScaleGrace(MODE_GRIND, MODE_STEAM));
    TEST_ASSERT_TRUE(shouldStartSteamScaleGrace(MODE_MANUAL, MODE_STEAM));
}

void test_steam_grace_skipped_when_not_from_scanning_mode(void) {
    // Reaching STEAM from a non-scanning mode: scale was already disconnected.
    TEST_ASSERT_FALSE(shouldStartSteamScaleGrace(MODE_STANDBY, MODE_STEAM));
    TEST_ASSERT_FALSE(shouldStartSteamScaleGrace(MODE_WATER, MODE_STEAM));
    TEST_ASSERT_FALSE(shouldStartSteamScaleGrace(MODE_STEAM, MODE_STEAM));
}

void test_steam_grace_skipped_for_non_steam_destinations(void) {
    // Other transitions out of a scanning mode disconnect immediately, no grace.
    TEST_ASSERT_FALSE(shouldStartSteamScaleGrace(MODE_BREW, MODE_STANDBY));
    TEST_ASSERT_FALSE(shouldStartSteamScaleGrace(MODE_BREW, MODE_WATER));
    TEST_ASSERT_FALSE(shouldStartSteamScaleGrace(MODE_BREW, MODE_GRIND));
}

// A same-mode re-fire is a no-op and must short-circuit before any
// scan/teardown logic (so a redundant STEAM->STEAM event can't collapse an
// in-flight grace window). Distinct modes are real transitions.
static_assert(isRedundantModeChange(MODE_STEAM, MODE_STEAM));
static_assert(isRedundantModeChange(MODE_BREW, MODE_BREW));
static_assert(isRedundantModeChange(MODE_STANDBY, MODE_STANDBY));
static_assert(!isRedundantModeChange(MODE_BREW, MODE_STEAM));
static_assert(!isRedundantModeChange(MODE_BREW, MODE_STANDBY));
static_assert(!isRedundantModeChange(MODE_STEAM, MODE_BREW));

void test_redundant_mode_change_detected_for_same_mode(void) {
    TEST_ASSERT_TRUE(isRedundantModeChange(MODE_STEAM, MODE_STEAM));
    TEST_ASSERT_TRUE(isRedundantModeChange(MODE_BREW, MODE_BREW));
    TEST_ASSERT_TRUE(isRedundantModeChange(MODE_STANDBY, MODE_STANDBY));
}

void test_redundant_mode_change_false_for_real_transitions(void) {
    TEST_ASSERT_FALSE(isRedundantModeChange(MODE_BREW, MODE_STEAM));
    TEST_ASSERT_FALSE(isRedundantModeChange(MODE_BREW, MODE_STANDBY));
    TEST_ASSERT_FALSE(isRedundantModeChange(MODE_STEAM, MODE_BREW));
}

// PRO-248: the unified post-stop grace is the single source of truth for both
// the BLE scale-alive window and ShotHistory's extended-recording window, and is
// the requested 10 s hard cap.
void test_unified_post_stop_grace_is_ten_seconds(void) { TEST_ASSERT_EQUAL_UINT32(10000, POST_STOP_GRACE_DURATION_MS); }

static int runBleScaleScanPolicyTests() {
    UNITY_BEGIN();
    RUN_TEST(test_scale_scan_skips_modes_that_do_not_use_scale_data);
    RUN_TEST(test_scale_scan_runs_for_modes_that_can_use_scale_data);
    RUN_TEST(test_steam_grace_opens_only_from_scanning_mode_into_steam);
    RUN_TEST(test_steam_grace_skipped_when_not_from_scanning_mode);
    RUN_TEST(test_steam_grace_skipped_for_non_steam_destinations);
    RUN_TEST(test_redundant_mode_change_detected_for_same_mode);
    RUN_TEST(test_redundant_mode_change_false_for_real_transitions);
    RUN_TEST(test_unified_post_stop_grace_is_ten_seconds);
    return UNITY_END();
}

#ifdef ARDUINO
void setup() { runBleScaleScanPolicyTests(); }

void loop() {}
#else
int main(int argc, char **argv) { return runBleScaleScanPolicyTests(); }
#endif
