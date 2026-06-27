#include "../../src/display/plugins/ChangeModeDeferPolicy.h"
#include <unity.h>

// PRO-267: pure predicate for the WebUIPlugin `req:change-mode` arming gate.
//
// shouldDeferModeChange(newMode, isExtendedRecording) decides whether the
// handler DEFERS the clear()+setMode() to loop() (true => arm pendingModeChange,
// keeping the BLE scale connected and record() logging so post-stop drips land
// in the yield) vs engages the new mode IMMEDIATELY (false). It is the exact
// inline condition the handler used:
//     newMode != MODE_STANDBY && isExtendedRecording
// extracted unchanged (no behavior change) so it is host-testable in
// [env:native] without linking Controller/BLE/LVGL/FreeRTOS.

// Compile-time guarantees of the truth table (mirrors the header's static_asserts).
static_assert(!shouldDeferModeChange(MODE_STANDBY, true), "STANDBY never defers (settle open)");
static_assert(!shouldDeferModeChange(MODE_STANDBY, false), "STANDBY never defers (no settle)");
static_assert(shouldDeferModeChange(MODE_STEAM, true), "non-standby + settle open -> defer");
static_assert(!shouldDeferModeChange(MODE_STEAM, false), "non-standby + no settle -> engage immediately");

void setUp(void) {}
void tearDown(void) {}

// PRO-265: STANDBY is an explicit user stop and must NEVER defer, regardless of
// whether the post-stop settle window is open.
void test_standby_never_defers(void) {
    TEST_ASSERT_FALSE(shouldDeferModeChange(MODE_STANDBY, true));
    TEST_ASSERT_FALSE(shouldDeferModeChange(MODE_STANDBY, false));
}

// PRO-261: a non-STANDBY target defers IFF the settle window is open.
void test_non_standby_defers_when_settle_open(void) {
    TEST_ASSERT_TRUE(shouldDeferModeChange(MODE_STEAM, true));  // auto-steam
    TEST_ASSERT_TRUE(shouldDeferModeChange(MODE_GRIND, true));  // grind
    TEST_ASSERT_TRUE(shouldDeferModeChange(MODE_MANUAL, true)); // manual
    TEST_ASSERT_TRUE(shouldDeferModeChange(MODE_BREW, true));   // brew
    TEST_ASSERT_TRUE(shouldDeferModeChange(MODE_WATER, true));  // water
}

// No settle window (no scale / flow-estimation / time-based shot, or not from an
// active brew): engage the new mode immediately, no added latency.
void test_non_standby_engages_immediately_without_settle(void) {
    TEST_ASSERT_FALSE(shouldDeferModeChange(MODE_STEAM, false));
    TEST_ASSERT_FALSE(shouldDeferModeChange(MODE_GRIND, false));
    TEST_ASSERT_FALSE(shouldDeferModeChange(MODE_MANUAL, false));
    TEST_ASSERT_FALSE(shouldDeferModeChange(MODE_BREW, false));
    TEST_ASSERT_FALSE(shouldDeferModeChange(MODE_WATER, false));
}

static int runChangeModeDeferPolicyTests() {
    UNITY_BEGIN();
    RUN_TEST(test_standby_never_defers);
    RUN_TEST(test_non_standby_defers_when_settle_open);
    RUN_TEST(test_non_standby_engages_immediately_without_settle);
    return UNITY_END();
}

#ifdef ARDUINO
void setup() { runChangeModeDeferPolicyTests(); }

void loop() {}
#else
int main(int argc, char **argv) { return runChangeModeDeferPolicyTests(); }
#endif
