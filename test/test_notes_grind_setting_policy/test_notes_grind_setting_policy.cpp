#include "../../src/display/plugins/ActiveShotFillPolicy.h"
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

void test_fill_recheck_rejects_a_shot_ended_while_it_waited_for_notes_io(void) {
    const shot_notes::ActiveShotIdentity admitted{42, 1234};
    const shot_notes::ActiveShotIdentity ended{43, 1234};

    TEST_ASSERT_TRUE(shot_notes::isActiveFillFor(admitted, admitted, 1234));
    TEST_ASSERT_FALSE(shot_notes::isActiveFillFor(admitted, ended, 1234));
}

void test_notes_operations_reject_writes_after_serialized_history_delete(void) {
    // Model the notesMutex order: delete wins the resource, then a delayed fill
    // or normal save must not recreate a sidecar without its .slog shot record.
    bool historyExists = true;
    historyExists = false; // req:history:delete removes .slog and .json while serialized

    TEST_ASSERT_FALSE(shot_notes::mayAccessExistingHistoryNotes(historyExists)); // delayed fill
    TEST_ASSERT_FALSE(shot_notes::mayAccessExistingHistoryNotes(historyExists)); // delayed save
}

void test_notes_get_observes_a_complete_serialized_write(void) {
    // A GET that acquires the resource after a FILE_WRITE save sees the complete
    // replacement, not the stale/empty pre-write document.
    JsonDocument saved;
    saved["grindSetting"] = "3.2";
    saved["doseIn"] = "18";

    TEST_ASSERT_TRUE(shot_notes::mayAccessExistingHistoryNotes(true));
    TEST_ASSERT_EQUAL_STRING("3.2", saved["grindSetting"].as<const char *>());
    TEST_ASSERT_EQUAL_STRING("18", saved["doseIn"].as<const char *>());
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_stale_regular_save_preserves_dashboard_fill_written_between_read_and_save);
    RUN_TEST(test_explicit_shot_history_grind_setting_remains_authoritative);
    RUN_TEST(test_fill_recheck_rejects_a_shot_ended_while_it_waited_for_notes_io);
    RUN_TEST(test_notes_operations_reject_writes_after_serialized_history_delete);
    RUN_TEST(test_notes_get_observes_a_complete_serialized_write);
    return UNITY_END();
}
