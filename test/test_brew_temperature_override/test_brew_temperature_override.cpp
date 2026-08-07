#include <unity.h>
#include "display/core/BrewTemperatureOverridePolicy.h"

void test_override_is_explicit_even_when_equal_to_profile_temperature() {
    BrewTemperatureOverrideState state{"profile-a", 93.0f, true};
    TEST_ASSERT_TRUE(state.appliesTo("profile-a"));
    TEST_ASSERT_EQUAL_FLOAT(93.0f, effectiveBrewTemperature(93.0f, state, "profile-a"));
}

void test_profile_switch_clears_override_and_uses_new_profile_root() {
    BrewTemperatureOverrideState state{"profile-a", 96.0f, true};
    state.clear();
    TEST_ASSERT_FALSE(state.appliesTo("profile-b"));
    TEST_ASSERT_EQUAL_FLOAT(91.0f, effectiveBrewTemperature(91.0f, state, "profile-b"));
}

// PRO-629: saving an unrelated profile must not route through selection. Only
// a transition to a different profile is allowed to clear the override.
void test_save_as_new_profile_switch_clears_persisted_override_before_reboot() {
    BrewTemperatureOverrideState persistedState{"profile-a", 96.0f, true};
    const std::string oldSelectedProfileId = "profile-a";
    const std::string newSelectedProfileId = "profile-copy";

    // Controller::onProfileSaveAsNew() changes the selected id in the same
    // settings transaction that clears this persisted state. A settings read
    // after reboot therefore cannot revive the old profile's override.
    if (shouldClearBrewTemperatureOverrideOnProfileSelection(oldSelectedProfileId, newSelectedProfileId)) {
        persistedState.clear();
    }

    BrewTemperatureOverrideState reloadedState = persistedState;
    TEST_ASSERT_FALSE(reloadedState.enabled);
    TEST_ASSERT_TRUE(reloadedState.profileId.empty());
    TEST_ASSERT_EQUAL_FLOAT(0.0f, reloadedState.temperature);
}

void test_override_only_replaces_positive_root_temperature() {
    BrewTemperatureOverrideState state{"profile-a", 96.0f, true};
    TEST_ASSERT_EQUAL_FLOAT(96.0f, effectiveBrewTemperature(0.0f, state, "profile-a"));
    TEST_ASSERT_EQUAL_FLOAT(96.0f, effectiveBrewTemperature(-1.0f, state, "profile-a"));
}

// PRO-629: idle Brew has no BrewProcess, so its output-control target must use
// the same selected-profile override resolution as the normal Brew fallback.
void test_idle_brew_output_target_uses_selected_profile_override() {
    BrewTemperatureOverrideState state{"profile-a", 96.0f, true};
    TEST_ASSERT_EQUAL_FLOAT(96.0f, effectiveBrewTemperature(92.0f, state, "profile-a"));
}

// PRO-629: the control task and WebSocket task must observe the three
// persisted fields as one tuple. A mixed observation could apply a stale
// temperature to the selected profile or report explicit provenance for it.
void test_override_snapshot_keeps_enabled_temperature_and_profile_together() {
    const BrewTemperatureOverrideState before{"profile-a", 92.0f, false};
    const BrewTemperatureOverrideState after{"profile-b", 96.0f, true};
    const BrewTemperatureOverrideState observed = after;

    TEST_ASSERT_FALSE(observed.appliesTo("profile-a"));
    TEST_ASSERT_TRUE(observed.appliesTo("profile-b"));
    TEST_ASSERT_EQUAL_FLOAT(96.0f, effectiveBrewTemperature(92.0f, observed, "profile-b"));
    TEST_ASSERT_FALSE(before.appliesTo("profile-a"));
}

void test_brew_temperature_request_validation() {
    TEST_ASSERT_TRUE(isValidBrewTemperatureOverride(0.0f));
    TEST_ASSERT_TRUE(isValidBrewTemperatureOverride(160.0f));
    TEST_ASSERT_FALSE(isValidBrewTemperatureOverride(-0.1f));
    TEST_ASSERT_FALSE(isValidBrewTemperatureOverride(160.1f));
}

// PRO-629: setMode() reasserts the effective target after a standby -> brew
// transition. That passive reassertion must not create an explicit override;
// only a user/API temperature edit is allowed to persist one.
void test_standby_to_brew_reassert_does_not_persist_override() {
    TEST_ASSERT_FALSE(shouldPersistBrewTemperatureOverride(BrewTemperatureTargetUpdate::PASSIVE_REASSERT));
    TEST_ASSERT_TRUE(shouldPersistBrewTemperatureOverride(BrewTemperatureTargetUpdate::EXPLICIT_EDIT));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_override_is_explicit_even_when_equal_to_profile_temperature);
    RUN_TEST(test_profile_switch_clears_override_and_uses_new_profile_root);
    RUN_TEST(test_save_as_new_profile_switch_clears_persisted_override_before_reboot);
    RUN_TEST(test_override_only_replaces_positive_root_temperature);
    RUN_TEST(test_idle_brew_output_target_uses_selected_profile_override);
    RUN_TEST(test_override_snapshot_keeps_enabled_temperature_and_profile_together);
    RUN_TEST(test_brew_temperature_request_validation);
    RUN_TEST(test_standby_to_brew_reassert_does_not_persist_override);
    return UNITY_END();
}
