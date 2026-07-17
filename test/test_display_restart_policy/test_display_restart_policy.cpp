#include "../../src/display/ui/default/DisplayRestartPolicy.h"
#include <unity.h>

// PRO-539: the physical display restart action is deliberately fail-closed.
// Controller evaluates this policy while holding processMutex. It keeps that
// mutex until ESP.restart(), so a physical-button activation either wins first
// (and is observed as active here) or blocks in startProcess until reset begins.
static_assert(shouldRestartDisplay(false, false, false, false, MODE_BREW, false), "idle brew mode may restart");
static_assert(!shouldRestartDisplay(true, false, false, false, MODE_BREW, false), "active process blocks restart");
static_assert(!shouldRestartDisplay(false, true, false, false, MODE_BREW, false), "firmware update blocks restart");
static_assert(!shouldRestartDisplay(false, false, true, false, MODE_BREW, false), "autotuning blocks restart");
static_assert(!shouldRestartDisplay(false, false, false, true, MODE_BREW, false), "error state blocks restart");
static_assert(!shouldRestartDisplay(false, false, false, false, MODE_WATER, false), "water mode blocks restart conservatively");
static_assert(!shouldRestartDisplay(false, false, false, false, MODE_GRIND, false), "grind mode blocks restart conservatively");
static_assert(!shouldRestartDisplay(false, false, false, false, MODE_BREW, true), "active grinder blocks restart");

void setUp(void) {}
void tearDown(void) {}

void test_allows_idle_brew_mode(void) { TEST_ASSERT_TRUE(shouldRestartDisplay(false, false, false, false, MODE_BREW, false)); }

void test_blocks_an_active_process_including_mutex_timeout(void) {
    TEST_ASSERT_FALSE(shouldRestartDisplay(true, false, false, false, MODE_BREW, false));
}

void test_blocks_a_process_that_activates_before_restart_authorization(void) {
    TEST_ASSERT_FALSE(shouldRestartDisplay(true, false, false, false, MODE_BREW, false));
}

void test_blocks_update_autotune_and_error_states(void) {
    TEST_ASSERT_FALSE(shouldRestartDisplay(false, true, false, false, MODE_BREW, false));
    TEST_ASSERT_FALSE(shouldRestartDisplay(false, false, true, false, MODE_BREW, false));
    TEST_ASSERT_FALSE(shouldRestartDisplay(false, false, false, true, MODE_BREW, false));
}

void test_blocks_water_and_grind_modes_and_an_active_grinder(void) {
    TEST_ASSERT_FALSE(shouldRestartDisplay(false, false, false, false, MODE_WATER, false));
    TEST_ASSERT_FALSE(shouldRestartDisplay(false, false, false, false, MODE_GRIND, false));
    TEST_ASSERT_FALSE(shouldRestartDisplay(false, false, false, false, MODE_BREW, true));
}

static int runDisplayRestartPolicyTests() {
    UNITY_BEGIN();
    RUN_TEST(test_allows_idle_brew_mode);
    RUN_TEST(test_blocks_an_active_process_including_mutex_timeout);
    RUN_TEST(test_blocks_a_process_that_activates_before_restart_authorization);
    RUN_TEST(test_blocks_update_autotune_and_error_states);
    RUN_TEST(test_blocks_water_and_grind_modes_and_an_active_grinder);
    return UNITY_END();
}

#ifdef ARDUINO
void setup() { runDisplayRestartPolicyTests(); }
void loop() {}
#else
int main(int argc, char **argv) { return runDisplayRestartPolicyTests(); }
#endif
