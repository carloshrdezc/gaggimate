#include "../../src/display/core/StandbyTransitionPolicy.h"
#include <unity.h>

// PRO-278: stop-steam bounces back to Steam mode before staying in Standby.
//
// Root cause: Controller::activateStandby() flipped the mode to MODE_STANDBY
// BEFORE tearing down the running process (setMode then deactivate). Between
// those two calls, mode == MODE_STANDBY while the steam process was still
// active and the mutable `controller:mode:change` event had already fired, so a
// re-assert path could bounce the mode back to MODE_STEAM. The fix reorders to
// deactivate() then setMode(MODE_STANDBY) — matching deactivateStandby(), the
// steam-button release, and WebUIPlugin's req:change-mode STANDBY path.
//
// The full Controller cannot be instantiated on the host ([env:native] does not
// shim BLE/LVGL/FreeRTOS), so this pins the pure ordering contract that
// StandbyTransitionPolicy.h captures and Controller::activateStandby() asserts.

void setUp(void) {}
void tearDown(void) {}

// The safe ordering: deactivate first, then set the standby mode.
void test_deactivate_before_setmode_is_safe(void) {
    TEST_ASSERT_TRUE(standbyOrderingIsSafe(StandbyStep::DEACTIVATE, StandbyStep::SET_MODE));
}

// The buggy ordering this issue fixed: setMode first leaves the bounce window.
void test_setmode_before_deactivate_is_unsafe(void) {
    TEST_ASSERT_FALSE(standbyOrderingIsSafe(StandbyStep::SET_MODE, StandbyStep::DEACTIVATE));
}

// Degenerate orderings (same step twice) are never the real two-step transition
// and must not be reported as safe.
void test_degenerate_orderings_are_unsafe(void) {
    TEST_ASSERT_FALSE(standbyOrderingIsSafe(StandbyStep::DEACTIVATE, StandbyStep::DEACTIVATE));
    TEST_ASSERT_FALSE(standbyOrderingIsSafe(StandbyStep::SET_MODE, StandbyStep::SET_MODE));
}

// The ordering activateStandby() actually performs (mirrored into the policy
// header's ACTIVATE_STANDBY_* constants, which the firmware compile asserts via
// static_assert) must be the safe one. If a future edit reorders the body back
// to setMode-then-deactivate without updating the constants, this fails here;
// if it reorders the constants to match a buggy body, the firmware static_assert
// fails the build.
void test_activate_standby_uses_safe_ordering(void) {
    TEST_ASSERT_TRUE(standbyOrderingIsSafe(ACTIVATE_STANDBY_FIRST_STEP, ACTIVATE_STANDBY_SECOND_STEP));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(StandbyStep::DEACTIVATE), static_cast<int>(ACTIVATE_STANDBY_FIRST_STEP));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(StandbyStep::SET_MODE), static_cast<int>(ACTIVATE_STANDBY_SECOND_STEP));
}

static int runStandbyTransitionPolicyTests() {
    UNITY_BEGIN();
    RUN_TEST(test_deactivate_before_setmode_is_safe);
    RUN_TEST(test_setmode_before_deactivate_is_unsafe);
    RUN_TEST(test_degenerate_orderings_are_unsafe);
    RUN_TEST(test_activate_standby_uses_safe_ordering);
    return UNITY_END();
}

#ifdef ARDUINO
void setup() { runStandbyTransitionPolicyTests(); }

void loop() {}
#else
int main(int argc, char **argv) { return runStandbyTransitionPolicyTests(); }
#endif
