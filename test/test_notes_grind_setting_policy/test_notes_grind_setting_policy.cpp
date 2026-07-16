#include "../../src/display/plugins/ShotNotesPersistencePolicy.h"
#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

void test_stale_regular_save_preserves_dashboard_fill_written_between_read_and_save(void) {
    // Deterministic interleaving: a normal writer has already read a notes snapshot
    // without grindSetting when Dashboard fills the persisted notes before its save.
    JsonDocument staleRegularPayload;
    staleRegularPayload["doseIn"] = "18";
    staleRegularPayload["grindSetting"] = "";

    JsonDocument persistedAfterDashboardFill;
    persistedAfterDashboardFill["grindSetting"] = "3.2";

    shot_notes::preservePersistedGrindSetting(staleRegularPayload, persistedAfterDashboardFill);

    TEST_ASSERT_EQUAL_STRING("3.2", staleRegularPayload["grindSetting"].as<const char *>());
    TEST_ASSERT_EQUAL_STRING("18", staleRegularPayload["doseIn"].as<const char *>());
}

void test_explicit_shot_history_grind_setting_remains_authoritative(void) {
    JsonDocument explicitShotHistoryPayload;
    explicitShotHistoryPayload["grindSetting"] = "4.0";

    JsonDocument persistedDashboardFill;
    persistedDashboardFill["grindSetting"] = "3.2";

    shot_notes::preservePersistedGrindSetting(explicitShotHistoryPayload, persistedDashboardFill);

    TEST_ASSERT_EQUAL_STRING("4.0", explicitShotHistoryPayload["grindSetting"].as<const char *>());
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_stale_regular_save_preserves_dashboard_fill_written_between_read_and_save);
    RUN_TEST(test_explicit_shot_history_grind_setting_remains_authoritative);
    return UNITY_END();
}
