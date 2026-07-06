#include "../../src/display/plugins/StandbyReassertPolicy.h"
#include <unity.h>

// PRO-421: pure predicate for the WebUIPlugin `req:change-mode` standby-reassert
// guard.
//
// shouldSuppressStandbyReassert(newMode, msSinceExplicitStandby) decides whether
// a `req:change-mode` to a non-STANDBY target must be REJECTED because it arrives
// as a stale re-assert right after an explicit STANDBY. This is the firmware's
// authoritative defense against the confirmed bounce: pressing web "Stop Steam"
// with auto-steam enabled makes the dashboard reflexively re-fire change-mode
// STEAM ~150 ms after the STANDBY, which the firmware would otherwise apply and
// bounce the machine back to Steam. Host-testable in [env:native] without
// linking Controller/BLE/LVGL/FreeRTOS.

// Compile-time guarantees of the truth table (mirrors the header's static_asserts).
static_assert(!shouldSuppressStandbyReassert(MODE_STANDBY, 0UL), "STANDBY is never suppressed");
static_assert(shouldSuppressStandbyReassert(MODE_STEAM, 0UL), "immediate STEAM re-assert suppressed");
static_assert(!shouldSuppressStandbyReassert(MODE_STEAM, STANDBY_REASSERT_GUARD_MS), "re-entry at window edge allowed");

void setUp(void) {}
void tearDown(void) {}

// A STANDBY request must ALWAYS apply — you can never get stuck out of Standby,
// regardless of timing.
void test_standby_target_never_suppressed(void) {
    TEST_ASSERT_FALSE(shouldSuppressStandbyReassert(MODE_STANDBY, 0UL));
    TEST_ASSERT_FALSE(shouldSuppressStandbyReassert(MODE_STANDBY, 1UL));
    TEST_ASSERT_FALSE(shouldSuppressStandbyReassert(MODE_STANDBY, STANDBY_REASSERT_GUARD_MS));
    TEST_ASSERT_FALSE(shouldSuppressStandbyReassert(MODE_STANDBY, STANDBY_REASSERT_GUARD_MS + 10000));
}

// The bug: a non-STANDBY re-assert arriving within the guard window right after
// an explicit STANDBY is suppressed. This is the reflexive web auto-steam
// re-fire (change-mode STEAM ~150 ms after Stop-Steam sends STANDBY).
void test_immediate_reassert_suppressed(void) {
    // ~150 ms is the observed live re-fire latency.
    TEST_ASSERT_TRUE(shouldSuppressStandbyReassert(MODE_STEAM, 150UL));
    // Zero-latency (same tick) is suppressed too.
    TEST_ASSERT_TRUE(shouldSuppressStandbyReassert(MODE_STEAM, 0UL));
    // Any non-STANDBY target is guarded, not just STEAM.
    TEST_ASSERT_TRUE(shouldSuppressStandbyReassert(MODE_BREW, 100UL));
    TEST_ASSERT_TRUE(shouldSuppressStandbyReassert(MODE_WATER, 100UL));
    TEST_ASSERT_TRUE(shouldSuppressStandbyReassert(MODE_GRIND, 100UL));
    TEST_ASSERT_TRUE(shouldSuppressStandbyReassert(MODE_MANUAL, 100UL));
    // Just inside the trailing edge of the window: still suppressed.
    TEST_ASSERT_TRUE(shouldSuppressStandbyReassert(MODE_STEAM, STANDBY_REASSERT_GUARD_MS - 1));
}

// A deliberate re-entry (the user pressing Steam again a moment later, or any
// mode change well after the standby) must NOT be blocked — the mode never
// wedges. The window edge itself is inclusive-allowed.
void test_deliberate_reentry_allowed(void) {
    TEST_ASSERT_FALSE(shouldSuppressStandbyReassert(MODE_STEAM, STANDBY_REASSERT_GUARD_MS));
    TEST_ASSERT_FALSE(shouldSuppressStandbyReassert(MODE_STEAM, STANDBY_REASSERT_GUARD_MS + 1));
    TEST_ASSERT_FALSE(shouldSuppressStandbyReassert(MODE_STEAM, STANDBY_REASSERT_GUARD_MS + 5000));
    TEST_ASSERT_FALSE(shouldSuppressStandbyReassert(MODE_BREW, STANDBY_REASSERT_GUARD_MS + 1));
}

// End-to-end contract narrative: an explicit STANDBY, then a stale STEAM
// re-assert inside the window is rejected (Standby stays); a later STEAM press
// outside the window is honored (user can re-enter Steam).
void test_stop_steam_then_bounce_then_deliberate(void) {
    // 1. Stop Steam -> STANDBY applies (never suppressed).
    TEST_ASSERT_FALSE(shouldSuppressStandbyReassert(MODE_STANDBY, STANDBY_REASSERT_GUARD_MS));
    // 2. Reflexive re-fire of STEAM ~150 ms later -> suppressed, Standby wins.
    TEST_ASSERT_TRUE(shouldSuppressStandbyReassert(MODE_STEAM, 150UL));
    // 3. User deliberately re-enters Steam ~2 s later -> honored.
    TEST_ASSERT_FALSE(shouldSuppressStandbyReassert(MODE_STEAM, 2000UL));
}

static int runStandbyReassertPolicyTests() {
    UNITY_BEGIN();
    RUN_TEST(test_standby_target_never_suppressed);
    RUN_TEST(test_immediate_reassert_suppressed);
    RUN_TEST(test_deliberate_reentry_allowed);
    RUN_TEST(test_stop_steam_then_bounce_then_deliberate);
    return UNITY_END();
}

#ifdef ARDUINO
void setup() { runStandbyReassertPolicyTests(); }

void loop() {}
#else
int main(int argc, char **argv) { return runStandbyReassertPolicyTests(); }
#endif
