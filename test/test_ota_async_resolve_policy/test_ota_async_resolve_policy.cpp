// PRO-13: pure, host-testable coverage for the WebUIPlugin OTA forced-tag /
// channel-switch async-resolve state machine (OtaAsyncResolvePolicy.h),
// extracted so the WebUIPlugin::loop() `if (updating)` block never blocks
// the Arduino main loop task on `ota->checkForUpdates()` for the pinned-tag /
// channel-switch path.
//
// Covers:
//   - otaResolveStateForDecision: every OtaFlashDecision maps to the right
//     OtaResolveState (Refuse -> Failed; everything else -> ReadyToFlash)
//   - otaResolveTimedOut: boundary (< / == / >), zero-elapsed, and the
//     millis() rollover case
//   - otaResolveResultIsCurrent: matching / stale (older) / mismatched
//     (never-current, e.g. newer) generations
//
// Header-only + free of any Arduino-String method, so it links on
// [env:native] via the existing `-I src` with the host String shim,
// mirroring the OtaChannelSwitchPolicy.h precedent in this directory.

#include "../../src/display/plugins/OtaAsyncResolvePolicy.h"
#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// otaResolveStateForDecision
// ---------------------------------------------------------------------------

void test_state_for_decision_force_match_tag_is_ready_to_flash(void) {
    TEST_ASSERT_EQUAL(OtaResolveState::ReadyToFlash, otaResolveStateForDecision(OtaFlashDecision::ForceMatchTag));
}

void test_state_for_decision_force_channel_switch_is_ready_to_flash(void) {
    TEST_ASSERT_EQUAL(OtaResolveState::ReadyToFlash, otaResolveStateForDecision(OtaFlashDecision::ForceChannelSwitch));
}

// The async resolve path is only ever entered for a genuine tag pin or
// channel switch, so a resolve task never actually produces UpgradeOnly —
// but the mapping is still pinned so a future caller can't silently regress
// this into a Refuse-like outcome.
void test_state_for_decision_upgrade_only_is_ready_to_flash(void) {
    TEST_ASSERT_EQUAL(OtaResolveState::ReadyToFlash, otaResolveStateForDecision(OtaFlashDecision::UpgradeOnly));
}

void test_state_for_decision_refuse_is_failed(void) {
    TEST_ASSERT_EQUAL(OtaResolveState::Failed, otaResolveStateForDecision(OtaFlashDecision::Refuse));
}

// ---------------------------------------------------------------------------
// otaResolveTimedOut
// ---------------------------------------------------------------------------

void test_timed_out_false_before_boundary(void) {
    TEST_ASSERT_FALSE(otaResolveTimedOut(0, 9999, 10000));
}

void test_timed_out_true_exactly_at_boundary(void) {
    TEST_ASSERT_TRUE(otaResolveTimedOut(0, 10000, 10000));
}

void test_timed_out_true_past_boundary(void) {
    TEST_ASSERT_TRUE(otaResolveTimedOut(0, 10001, 10000));
}

void test_timed_out_false_zero_elapsed(void) {
    TEST_ASSERT_FALSE(otaResolveTimedOut(1000, 1000, 10000));
}

// millis() rollover (~49.7 days uptime): startMs just before wraparound,
// nowMs just after. Unsigned subtraction still yields the true (small)
// elapsed duration, so this must NOT report a false timeout.
void test_timed_out_survives_millis_rollover(void) {
    TEST_ASSERT_FALSE(otaResolveTimedOut(4294967295u, 5u, 10000));
}

// The same rollover case, but far enough past the wrap to genuinely exceed
// the timeout budget — must still report timed out.
void test_timed_out_true_after_rollover_past_budget(void) {
    TEST_ASSERT_TRUE(otaResolveTimedOut(4294967295u, 10005u, 10000));
}

// ---------------------------------------------------------------------------
// otaResolveResultIsCurrent
// ---------------------------------------------------------------------------

void test_result_is_current_matching_generation(void) {
    TEST_ASSERT_TRUE(otaResolveResultIsCurrent(3, 3));
    TEST_ASSERT_TRUE(otaResolveResultIsCurrent(0, 0));
}

// A stale (older) generation: the loop task moved on (fresh resolve spawned,
// or the previous one timed out) before this result arrived.
void test_result_is_current_false_for_stale_older_generation(void) {
    TEST_ASSERT_FALSE(otaResolveResultIsCurrent(2, 3));
}

// A mismatched (never-current, e.g. newer) generation should also be
// rejected -- the comparison is exact equality, not "<=".
void test_result_is_current_false_for_mismatched_newer_generation(void) {
    TEST_ASSERT_FALSE(otaResolveResultIsCurrent(4, 3));
}

static int runOtaAsyncResolvePolicyTests() {
    UNITY_BEGIN();
    RUN_TEST(test_state_for_decision_force_match_tag_is_ready_to_flash);
    RUN_TEST(test_state_for_decision_force_channel_switch_is_ready_to_flash);
    RUN_TEST(test_state_for_decision_upgrade_only_is_ready_to_flash);
    RUN_TEST(test_state_for_decision_refuse_is_failed);
    RUN_TEST(test_timed_out_false_before_boundary);
    RUN_TEST(test_timed_out_true_exactly_at_boundary);
    RUN_TEST(test_timed_out_true_past_boundary);
    RUN_TEST(test_timed_out_false_zero_elapsed);
    RUN_TEST(test_timed_out_survives_millis_rollover);
    RUN_TEST(test_timed_out_true_after_rollover_past_budget);
    RUN_TEST(test_result_is_current_matching_generation);
    RUN_TEST(test_result_is_current_false_for_stale_older_generation);
    RUN_TEST(test_result_is_current_false_for_mismatched_newer_generation);
    return UNITY_END();
}

#ifdef ARDUINO
void setup() { runOtaAsyncResolvePolicyTests(); }

void loop() {}
#else
int main(int argc, char **argv) { return runOtaAsyncResolvePolicyTests(); }
#endif
