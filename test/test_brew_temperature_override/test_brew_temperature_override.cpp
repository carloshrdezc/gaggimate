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

void test_override_only_replaces_positive_root_temperature() {
    BrewTemperatureOverrideState state{"profile-a", 96.0f, true};
    TEST_ASSERT_EQUAL_FLOAT(96.0f, effectiveBrewTemperature(0.0f, state, "profile-a"));
    TEST_ASSERT_EQUAL_FLOAT(96.0f, effectiveBrewTemperature(-1.0f, state, "profile-a"));
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
    RUN_TEST(test_override_only_replaces_positive_root_temperature);
    RUN_TEST(test_brew_temperature_request_validation);
    RUN_TEST(test_standby_to_brew_reassert_does_not_persist_override);
    return UNITY_END();
}
