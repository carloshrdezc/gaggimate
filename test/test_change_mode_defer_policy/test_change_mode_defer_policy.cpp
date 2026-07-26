#include "../../src/display/plugins/ChangeModeDeferPolicy.h"
#include <unity.h>

// PRO-267 / PRO-587: pure predicate for the WebUIPlugin `req:change-mode` arming
// gate.
//
// shouldDeferModeChange(newMode, isExtendedRecording, automatic) decides whether
// the handler DEFERS the clear()+setMode() to loop() (true => arm
// pendingModeChange, keeping the BLE scale connected and record() logging so
// post-stop drips land in the yield) vs engages the new mode IMMEDIATELY
// (false). It is host-testable in [env:native] without linking
// Controller/BLE/LVGL/FreeRTOS.
//
//   defer = isExtendedRecording && (newMode != MODE_STANDBY || automatic)
//
// The `automatic` dimension (PRO-587) distinguishes the AUTOMATIC post-shot
// standby-on-brew transition (rides the settle window, like Auto-Steam) from an
// EXPLICIT human stop (physical button / web Standby button / HomeKit — never
// defers, PRO-265). With `automatic` absent/false the predicate reduces to the
// original PRO-267 condition `newMode != MODE_STANDBY && isExtendedRecording`.

// Compile-time guarantees of the truth table (mirrors the header's static_asserts).
static_assert(!shouldDeferModeChange(MODE_STANDBY, true), "explicit STANDBY never defers (settle open)");
static_assert(!shouldDeferModeChange(MODE_STANDBY, false), "explicit STANDBY never defers (no settle)");
static_assert(!shouldDeferModeChange(MODE_STANDBY, true, false), "explicit STANDBY never defers (settle open)");
static_assert(shouldDeferModeChange(MODE_STANDBY, true, true), "automatic STANDBY defers while settle open");
static_assert(!shouldDeferModeChange(MODE_STANDBY, false, true), "automatic STANDBY engages immediately (no settle)");
static_assert(shouldDeferModeChange(MODE_STEAM, true), "non-standby + settle open -> defer");
static_assert(!shouldDeferModeChange(MODE_STEAM, false), "non-standby + no settle -> engage immediately");

void setUp(void) {}
void tearDown(void) {}

// PRO-265: an EXPLICIT STANDBY (automatic absent/false) is a human stop and must
// NEVER defer, regardless of whether the post-stop settle window is open. This
// is the fast-stop guarantee for the physical button, web Standby button, and
// HomeKit — it must remain bit-for-bit unchanged.
void test_explicit_standby_never_defers(void) {
    // Default arg (automatic omitted) — the shape every explicit stop uses.
    TEST_ASSERT_FALSE(shouldDeferModeChange(MODE_STANDBY, true));
    TEST_ASSERT_FALSE(shouldDeferModeChange(MODE_STANDBY, false));
    // Explicit spelled out (automatic == false) — identical result.
    TEST_ASSERT_FALSE(shouldDeferModeChange(MODE_STANDBY, true, false));
    TEST_ASSERT_FALSE(shouldDeferModeChange(MODE_STANDBY, false, false));
}

// PRO-587: an AUTOMATIC STANDBY (standby-on-brew) defers IFF the settle window is
// open — it rides the exact same window as Auto-Steam so post-shot drips reach
// the recorded yield. With no window it still engages immediately (no latency).
void test_automatic_standby_defers_only_while_settle_open(void) {
    TEST_ASSERT_TRUE(shouldDeferModeChange(MODE_STANDBY, true, true));
    TEST_ASSERT_FALSE(shouldDeferModeChange(MODE_STANDBY, false, true));
}

// PRO-261: a non-STANDBY target defers IFF the settle window is open. The
// `automatic` flag is irrelevant for non-STANDBY targets — it only ever gates a
// STANDBY target.
void test_non_standby_defers_when_settle_open(void) {
    TEST_ASSERT_TRUE(shouldDeferModeChange(MODE_STEAM, true));  // auto-steam
    TEST_ASSERT_TRUE(shouldDeferModeChange(MODE_GRIND, true));  // grind
    TEST_ASSERT_TRUE(shouldDeferModeChange(MODE_MANUAL, true)); // manual
    TEST_ASSERT_TRUE(shouldDeferModeChange(MODE_BREW, true));   // brew
    TEST_ASSERT_TRUE(shouldDeferModeChange(MODE_WATER, true));  // water
    // Same result whether or not the automatic flag is set.
    TEST_ASSERT_TRUE(shouldDeferModeChange(MODE_STEAM, true, true));
    TEST_ASSERT_TRUE(shouldDeferModeChange(MODE_STEAM, true, false));
}

// No settle window (no scale / flow-estimation / time-based shot, or not from an
// active brew): engage the new mode immediately, no added latency — for every
// target and both flag values.
void test_engages_immediately_without_settle(void) {
    TEST_ASSERT_FALSE(shouldDeferModeChange(MODE_STEAM, false));
    TEST_ASSERT_FALSE(shouldDeferModeChange(MODE_GRIND, false));
    TEST_ASSERT_FALSE(shouldDeferModeChange(MODE_MANUAL, false));
    TEST_ASSERT_FALSE(shouldDeferModeChange(MODE_BREW, false));
    TEST_ASSERT_FALSE(shouldDeferModeChange(MODE_WATER, false));
    TEST_ASSERT_FALSE(shouldDeferModeChange(MODE_STANDBY, false, true));
    TEST_ASSERT_FALSE(shouldDeferModeChange(MODE_STANDBY, false, false));
}

static int runChangeModeDeferPolicyTests() {
    UNITY_BEGIN();
    RUN_TEST(test_explicit_standby_never_defers);
    RUN_TEST(test_automatic_standby_defers_only_while_settle_open);
    RUN_TEST(test_non_standby_defers_when_settle_open);
    RUN_TEST(test_engages_immediately_without_settle);
    return UNITY_END();
}

#ifdef ARDUINO
void setup() { runChangeModeDeferPolicyTests(); }

void loop() {}
#else
int main(int argc, char **argv) { return runChangeModeDeferPolicyTests(); }
#endif
