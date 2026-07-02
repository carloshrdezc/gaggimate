// PRO-400 (design: PRO-394 §5a): pure policy for the WebUIPlugin OTA-start
// flash gate. decideOtaFlash(...) decides whether a flash should run the
// upgrade-only guard, force because a pinned tag was confirmed, force because
// the user switched channels (and the new head resolved), or refuse because we
// cannot confirm what we would flash.
//
// Header-only + free of any Arduino-String method (uses const char* + bools),
// so it links on [env:native] via `-I src` with the host String shim.
//
// Truth table (PRO-394 §5a):
//   - within-channel guard KEPT (selected == installed, non-tag) -> UpgradeOnly
//   - tag match / mismatch                        -> ForceMatchTag / Refuse
//   - v-tolerance both directions                 -> ForceMatchTag
//   - beta->latest lower-semver switch            -> ForceChannelSwitch
//   - latest->beta equal-semver re-flash          -> ForceChannelSwitch
//   - nightly resolve-failure                     -> Refuse
//   - empty-installed defensive                   -> UpgradeOnly

#include "../../src/display/plugins/OtaChannelSwitchPolicy.h"
#include <unity.h>

// Compile-time truth table (mirrors the header's static_asserts + adds the
// v-tolerance and equal-semver re-flash cases that are constexpr-evaluable).
static_assert(decideOtaFlash(false, "", true, false, "1.2.3", false) == OtaFlashDecision::UpgradeOnly,
              "within-channel keeps upgrade-only guard");
static_assert(decideOtaFlash(false, "", false, true, "1.2.3", false) == OtaFlashDecision::UpgradeOnly,
              "empty installed -> upgrade-only");
static_assert(decideOtaFlash(false, "", false, false, "1.0.0", false) == OtaFlashDecision::ForceChannelSwitch,
              "channel switch, resolve OK -> force");
static_assert(decideOtaFlash(false, "", false, false, "", false) == OtaFlashDecision::Refuse,
              "channel switch, empty resolve -> refuse");
static_assert(decideOtaFlash(false, "", false, false, "1.0.0", true) == OtaFlashDecision::Refuse,
              "channel switch, resolve failed -> refuse");
static_assert(decideOtaFlash(true, "2.0.8", false, false, "2.0.8", false) == OtaFlashDecision::ForceMatchTag,
              "tag exact match -> force");
static_assert(decideOtaFlash(true, "2.0.8", false, false, "1.9.9", false) == OtaFlashDecision::Refuse, "tag mismatch -> refuse");
static_assert(decideOtaFlash(true, "v1.8.2", false, false, "1.8.2", false) == OtaFlashDecision::ForceMatchTag,
              "tag v-tolerance: pinned has v, resolved does not -> force");
static_assert(decideOtaFlash(true, "1.8.2", false, false, "v1.8.2", false) == OtaFlashDecision::ForceMatchTag,
              "tag v-tolerance: resolved has v, pinned does not -> force");

void setUp(void) {}
void tearDown(void) {}

// Within a channel (selected == installed, non-tag): keep the upgrade-only
// guard. The policy returns UpgradeOnly regardless of the resolved version.
void test_within_channel_keeps_upgrade_only_guard(void) {
    TEST_ASSERT_EQUAL(OtaFlashDecision::UpgradeOnly, decideOtaFlash(false, "", true, false, "1.2.3", false));
    TEST_ASSERT_EQUAL(OtaFlashDecision::UpgradeOnly, decideOtaFlash(false, "", true, false, "9.9.9", false));
    TEST_ASSERT_FALSE(otaDecisionForces(decideOtaFlash(false, "", true, false, "1.2.3", false)));
}

// Empty installedChannel (device missed the migration backfill): defensively
// treat as installed == selected so there is no spurious forced re-flash.
void test_empty_installed_defensive_upgrade_only(void) {
    TEST_ASSERT_EQUAL(OtaFlashDecision::UpgradeOnly, decideOtaFlash(false, "", false, true, "1.2.3", false));
}

// beta -> latest is a LOWER semver switch, and latest -> beta may be an EQUAL
// semver re-flash. Both must force-flash the resolved head of the new channel.
void test_channel_switch_forces_regardless_of_semver_direction(void) {
    // beta(2.1.0-beta) -> latest(2.0.0): lower semver, must force.
    TEST_ASSERT_EQUAL(OtaFlashDecision::ForceChannelSwitch, decideOtaFlash(false, "", false, false, "2.0.0", false));
    // latest -> beta where the resolved head equals the current version: equal
    // semver, must still force (idempotent re-flash advances installedChannel).
    TEST_ASSERT_EQUAL(OtaFlashDecision::ForceChannelSwitch, decideOtaFlash(false, "", false, false, "2.0.0", false));
    TEST_ASSERT_TRUE(shouldForceFlashForChannelSwitch(false, false, "2.0.0", false));
    TEST_ASSERT_TRUE(otaDecisionForces(decideOtaFlash(false, "", false, false, "2.0.0", false)));
}

// A channel switch whose new-channel resolve failed or came back empty (e.g.
// nightly resolve-failure): refuse — never flash something we cannot confirm.
void test_channel_switch_resolve_failure_refuses(void) {
    TEST_ASSERT_EQUAL(OtaFlashDecision::Refuse, decideOtaFlash(false, "", false, false, "1.0.0", true));
    TEST_ASSERT_EQUAL(OtaFlashDecision::Refuse, decideOtaFlash(false, "", false, false, "", false));
    TEST_ASSERT_FALSE(shouldForceFlashForChannelSwitch(false, false, "1.0.0", true));
    TEST_ASSERT_FALSE(shouldForceFlashForChannelSwitch(false, false, "", false));
}

// Pinned tag: force only on a confirmed resolved==tag match; refuse otherwise.
void test_tag_match_forces_mismatch_refuses(void) {
    TEST_ASSERT_EQUAL(OtaFlashDecision::ForceMatchTag, decideOtaFlash(true, "2.0.8", false, false, "2.0.8", false));
    TEST_ASSERT_EQUAL(OtaFlashDecision::Refuse, decideOtaFlash(true, "2.0.8", false, false, "1.9.9", false));
    // tag path with a failed / empty resolve -> refuse.
    TEST_ASSERT_EQUAL(OtaFlashDecision::Refuse, decideOtaFlash(true, "2.0.8", false, false, "", true));
    TEST_ASSERT_EQUAL(OtaFlashDecision::Refuse, decideOtaFlash(true, "2.0.8", false, false, "", false));
}

// Leading-`v` tolerance in BOTH directions (mirrors WebUIPlugin's confirm).
void test_tag_v_prefix_tolerance_both_directions(void) {
    TEST_ASSERT_EQUAL(OtaFlashDecision::ForceMatchTag, decideOtaFlash(true, "v1.8.2", false, false, "1.8.2", false));
    TEST_ASSERT_EQUAL(OtaFlashDecision::ForceMatchTag, decideOtaFlash(true, "1.8.2", false, false, "v1.8.2", false));
    TEST_ASSERT_EQUAL(OtaFlashDecision::ForceMatchTag, decideOtaFlash(true, "v1.8.2", false, false, "v1.8.2", false));
    // A genuine mismatch that only shares the v-strip prefix is still a match;
    // a real semver difference is not.
    TEST_ASSERT_EQUAL(OtaFlashDecision::Refuse, decideOtaFlash(true, "v1.8.2", false, false, "1.8.3", false));
}

// The v-tolerant equality helper directly (host strcmp path).
void test_versions_equal_v_tolerant_helper(void) {
    TEST_ASSERT_TRUE(otaVersionsEqualVTolerant("1.2.3", "1.2.3"));
    TEST_ASSERT_TRUE(otaVersionsEqualVTolerant("v1.2.3", "1.2.3"));
    TEST_ASSERT_TRUE(otaVersionsEqualVTolerant("1.2.3", "v1.2.3"));
    TEST_ASSERT_FALSE(otaVersionsEqualVTolerant("1.2.3", "1.2.4"));
    TEST_ASSERT_TRUE(otaVersionsEqualVTolerant("", ""));
    TEST_ASSERT_TRUE(otaVersionsEqualVTolerant(nullptr, nullptr));
}

static int runOtaChannelSwitchPolicyTests() {
    UNITY_BEGIN();
    RUN_TEST(test_within_channel_keeps_upgrade_only_guard);
    RUN_TEST(test_empty_installed_defensive_upgrade_only);
    RUN_TEST(test_channel_switch_forces_regardless_of_semver_direction);
    RUN_TEST(test_channel_switch_resolve_failure_refuses);
    RUN_TEST(test_tag_match_forces_mismatch_refuses);
    RUN_TEST(test_tag_v_prefix_tolerance_both_directions);
    RUN_TEST(test_versions_equal_v_tolerant_helper);
    return UNITY_END();
}

#ifdef ARDUINO
void setup() { runOtaChannelSwitchPolicyTests(); }

void loop() {}
#else
int main(int argc, char **argv) { return runOtaChannelSwitchPolicyTests(); }
#endif
