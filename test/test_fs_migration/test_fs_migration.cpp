#include <unity.h>

#include <display/core/FsMigration.h>

// PRO-218 — host unit tests for the SPIFFS->LittleFS migration DECISION logic.
//
// These pin the branch behavior of decideFsMigration() without any hardware:
//   - once-only marker prevents re-running (idempotency)
//   - a valid LittleFS (fresh install / already migrated) never triggers
//     migration or data loss
//   - a pre-PRO-212 SPIFFS device picks the rescue path, choosing SD-backed
//     full preservation when SD is present and RAM-profiles-only (deferring
//     /h) when it is not
//   - nothing mounted falls through to a clean format and never bricks
//
// NOTE (scope): the actual data-PRESERVATION acceptance criterion (real
// profiles/shots survive a real SPIFFS->LittleFS upgrade on hardware) cannot
// be reproduced here — that needs Carlos's device. These tests only verify the
// decision state machine that drives the device-side copy/format/restore.

void setUp(void) {}
void tearDown(void) {}

// (1) Once-only guard: marker present => AlreadyMigrated, never migrate again,
// never move data, do not rewrite the marker.
void test_marker_present_is_idempotent(void) {
    FsMigrationInputs in;
    in.markerPresent = true;
    // Even with SPIFFS mountable and user data present, the marker wins.
    in.spiffsMounted = true;
    in.spiffsHasProfiles = true;
    in.spiffsHasHistory = true;
    in.sdCardAvailable = true;

    FsMigrationPlan plan = decideFsMigration(in);

    TEST_ASSERT_EQUAL(static_cast<int>(FsMigrationAction::AlreadyMigrated), static_cast<int>(plan.action));
    TEST_ASSERT_EQUAL(static_cast<int>(FsStagingTarget::None), static_cast<int>(plan.staging));
    TEST_ASSERT_FALSE(plan.historyDeferred);
    TEST_ASSERT_FALSE(plan.writeMarker); // marker already exists
}

// (1b) Marker takes priority even over a clean LittleFS mount (defensive).
void test_marker_present_beats_clean_littlefs(void) {
    FsMigrationInputs in;
    in.markerPresent = true;
    in.littleFsMountedClean = true;

    FsMigrationPlan plan = decideFsMigration(in);

    TEST_ASSERT_EQUAL(static_cast<int>(FsMigrationAction::AlreadyMigrated), static_cast<int>(plan.action));
    TEST_ASSERT_FALSE(plan.writeMarker);
}

// (2) Fresh install / already-migrated with valid LittleFS but no marker yet:
// use as-is, write the marker, NEVER format or migrate.
void test_clean_littlefs_no_marker_uses_as_is(void) {
    FsMigrationInputs in;
    in.markerPresent = false;
    in.littleFsMountedClean = true;

    FsMigrationPlan plan = decideFsMigration(in);

    TEST_ASSERT_EQUAL(static_cast<int>(FsMigrationAction::UseLittleFsAsIs), static_cast<int>(plan.action));
    TEST_ASSERT_EQUAL(static_cast<int>(FsStagingTarget::None), static_cast<int>(plan.staging));
    TEST_ASSERT_FALSE(plan.historyDeferred);
    TEST_ASSERT_TRUE(plan.writeMarker); // stamp it so future boots short-circuit
}

// (2b) Fresh install must NOT trigger a spurious migration even if a SPIFFS
// probe would also (spuriously) report mountable — a clean LittleFS wins over
// SPIFFS because branch (2) precedes branch (3).
void test_clean_littlefs_wins_over_spiffs(void) {
    FsMigrationInputs in;
    in.littleFsMountedClean = true;
    in.spiffsMounted = true; // should be ignored
    in.spiffsHasProfiles = true;

    FsMigrationPlan plan = decideFsMigration(in);

    TEST_ASSERT_EQUAL(static_cast<int>(FsMigrationAction::UseLittleFsAsIs), static_cast<int>(plan.action));
    TEST_ASSERT_EQUAL(static_cast<int>(FsStagingTarget::None), static_cast<int>(plan.staging));
}

// (3a) Pre-PRO-212 SPIFFS device WITH SD card: user /p and /h already live on
// the SD card (untouched by the internal-partition reformat), so the internal
// partition just gets a clean LittleFS — no RAM staging.
void test_spiffs_device_with_sd_no_staging_needed(void) {
    FsMigrationInputs in;
    in.markerPresent = false;
    in.littleFsMountedClean = false; // LittleFS mount failed (incompatible format)
    in.spiffsMounted = true;
    in.spiffsHasProfiles = true;
    in.spiffsHasHistory = true;
    in.sdCardAvailable = true;

    FsMigrationPlan plan = decideFsMigration(in);

    TEST_ASSERT_EQUAL(static_cast<int>(FsMigrationAction::MigrateFromSpiffs), static_cast<int>(plan.action));
    TEST_ASSERT_EQUAL(static_cast<int>(FsStagingTarget::None), static_cast<int>(plan.staging));
    TEST_ASSERT_FALSE(plan.historyDeferred); // data already safe on SD
    TEST_ASSERT_TRUE(plan.writeMarker);
}

// (3b) No SD, /h small enough to fit the RAM budget: preserve BOTH /p and /h.
void test_spiffs_no_sd_small_history_preserves_both(void) {
    FsMigrationInputs in;
    in.markerPresent = false;
    in.littleFsMountedClean = false;
    in.spiffsMounted = true;
    in.spiffsHasProfiles = true;
    in.spiffsHasHistory = true;
    in.sdCardAvailable = false;
    in.ramBudgetBytes = 256u * 1024u;
    in.spiffsHistoryBytes = 40u * 1024u; // fits

    FsMigrationPlan plan = decideFsMigration(in);

    TEST_ASSERT_EQUAL(static_cast<int>(FsMigrationAction::MigrateFromSpiffs), static_cast<int>(plan.action));
    TEST_ASSERT_EQUAL(static_cast<int>(FsStagingTarget::RamProfilesAndHistory), static_cast<int>(plan.staging));
    TEST_ASSERT_FALSE(plan.historyDeferred);
    TEST_ASSERT_TRUE(plan.writeMarker);
}

// (3c) No SD, /h too large for RAM: preserve /p, defer /h (documented fallback).
void test_spiffs_no_sd_large_history_defers_history(void) {
    FsMigrationInputs in;
    in.markerPresent = false;
    in.littleFsMountedClean = false;
    in.spiffsMounted = true;
    in.spiffsHasProfiles = true;
    in.spiffsHasHistory = true;
    in.sdCardAvailable = false;
    in.ramBudgetBytes = 256u * 1024u;
    in.spiffsHistoryBytes = 2u * 1024u * 1024u; // 2 MB — does not fit

    FsMigrationPlan plan = decideFsMigration(in);

    TEST_ASSERT_EQUAL(static_cast<int>(FsMigrationAction::MigrateFromSpiffs), static_cast<int>(plan.action));
    TEST_ASSERT_EQUAL(static_cast<int>(FsStagingTarget::RamProfilesOnly), static_cast<int>(plan.staging));
    TEST_ASSERT_TRUE(plan.historyDeferred); // /h not safe to hold in RAM
    TEST_ASSERT_TRUE(plan.writeMarker);
}

// (3d) SPIFFS mounts but is empty (no /p, no /h): still migrate (format to
// LittleFS) and mark done. No SD, no history => RamProfilesAndHistory branch
// (nothing to copy, but a valid LittleFS image + marker results). No brick.
void test_spiffs_empty_still_migrates_and_marks(void) {
    FsMigrationInputs in;
    in.spiffsMounted = true;
    in.spiffsHasProfiles = false;
    in.spiffsHasHistory = false;
    in.sdCardAvailable = false;
    in.ramBudgetBytes = 256u * 1024u;
    in.spiffsHistoryBytes = 0;

    FsMigrationPlan plan = decideFsMigration(in);

    TEST_ASSERT_EQUAL(static_cast<int>(FsMigrationAction::MigrateFromSpiffs), static_cast<int>(plan.action));
    TEST_ASSERT_EQUAL(static_cast<int>(FsStagingTarget::RamProfilesAndHistory), static_cast<int>(plan.staging));
    TEST_ASSERT_FALSE(plan.historyDeferred);
    TEST_ASSERT_TRUE(plan.writeMarker);
}

// (4) Fail safe: neither LittleFS nor SPIFFS mounts (blank / corrupt). Clean
// format, seed defaults, mark. Never brick.
void test_nothing_mounts_fresh_format(void) {
    FsMigrationInputs in;
    in.markerPresent = false;
    in.littleFsMountedClean = false;
    in.spiffsMounted = false;

    FsMigrationPlan plan = decideFsMigration(in);

    TEST_ASSERT_EQUAL(static_cast<int>(FsMigrationAction::FreshFormat), static_cast<int>(plan.action));
    TEST_ASSERT_EQUAL(static_cast<int>(FsStagingTarget::None), static_cast<int>(plan.staging));
    TEST_ASSERT_FALSE(plan.historyDeferred);
    TEST_ASSERT_TRUE(plan.writeMarker);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_marker_present_is_idempotent);
    RUN_TEST(test_marker_present_beats_clean_littlefs);
    RUN_TEST(test_clean_littlefs_no_marker_uses_as_is);
    RUN_TEST(test_clean_littlefs_wins_over_spiffs);
    RUN_TEST(test_spiffs_device_with_sd_no_staging_needed);
    RUN_TEST(test_spiffs_no_sd_small_history_preserves_both);
    RUN_TEST(test_spiffs_no_sd_large_history_defers_history);
    RUN_TEST(test_spiffs_empty_still_migrates_and_marks);
    RUN_TEST(test_nothing_mounts_fresh_format);
    return UNITY_END();
}
