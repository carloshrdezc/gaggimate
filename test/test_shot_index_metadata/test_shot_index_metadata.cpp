#include "../../src/display/plugins/ShotIndexMetadataPolicy.h"
#include <unity.h>

// PRO-277: the post-shot metadata update (ShotHistoryPlugin::updateIndexMetadata)
// now runs under indexMutex so it can reliably find the freshly-appended entry, and
// the field-merge it applies is the pure rule in ShotIndexMetadataPolicy.h. These
// tests pin that merge contract so a future refactor can't silently regress it:
//   - rating always overwritten
//   - volume overwritten ONLY on a positive doseOut override (0 = keep recorded weight)
//   - SHOT_FLAG_HAS_NOTES set (never cleared) when rated; other flags preserved

static ShotIndexEntry makeEntry(uint16_t volume, uint8_t rating, uint8_t flags) {
    ShotIndexEntry e{};
    e.id = 1782495739u;
    e.timestamp = 1782495739u;
    e.duration = 25000u;
    e.volume = volume;
    e.rating = rating;
    e.flags = flags;
    return e;
}

// Compile-time guarantees of the merge truth table.
static_assert(applyIndexMetadata(ShotIndexEntry{}, 4, 0).rating == 4, "rating must be applied");
static_assert(applyIndexMetadata(ShotIndexEntry{}, 0, 360).volume == 360, "positive volume override must apply");

void setUp(void) {}
void tearDown(void) {}

void test_rating_is_always_overwritten(void) {
    ShotIndexEntry e = makeEntry(/*volume*/ 360, /*rating*/ 0, SHOT_FLAG_COMPLETED);
    ShotIndexEntry out = applyIndexMetadata(e, /*rating*/ 5, /*volume*/ 0);
    TEST_ASSERT_EQUAL_UINT8(5, out.rating);
}

void test_zero_volume_override_keeps_recorded_weight(void) {
    // volume == 0 means "no doseOut override" — the recorded final weight (360) must survive.
    ShotIndexEntry e = makeEntry(/*volume*/ 360, /*rating*/ 0, SHOT_FLAG_COMPLETED);
    ShotIndexEntry out = applyIndexMetadata(e, /*rating*/ 3, /*volume*/ 0);
    TEST_ASSERT_EQUAL_UINT16(360, out.volume);
}

void test_positive_volume_override_replaces_weight(void) {
    ShotIndexEntry e = makeEntry(/*volume*/ 360, /*rating*/ 0, SHOT_FLAG_COMPLETED);
    ShotIndexEntry out = applyIndexMetadata(e, /*rating*/ 3, /*volume*/ 420);
    TEST_ASSERT_EQUAL_UINT16(420, out.volume);
}

void test_rated_entry_gains_has_notes_flag_preserving_others(void) {
    ShotIndexEntry e = makeEntry(/*volume*/ 360, /*rating*/ 0, SHOT_FLAG_COMPLETED);
    ShotIndexEntry out = applyIndexMetadata(e, /*rating*/ 4, /*volume*/ 0);
    TEST_ASSERT_TRUE((out.flags & SHOT_FLAG_HAS_NOTES) != 0);
    TEST_ASSERT_TRUE((out.flags & SHOT_FLAG_COMPLETED) != 0); // not clobbered
}

void test_zero_rating_does_not_set_has_notes(void) {
    // A rating of 0 (cleared) must not add the HAS_NOTES flag.
    ShotIndexEntry e = makeEntry(/*volume*/ 360, /*rating*/ 0, SHOT_FLAG_COMPLETED);
    ShotIndexEntry out = applyIndexMetadata(e, /*rating*/ 0, /*volume*/ 0);
    TEST_ASSERT_TRUE((out.flags & SHOT_FLAG_HAS_NOTES) == 0);
}

void test_deleted_flag_is_preserved(void) {
    ShotIndexEntry e = makeEntry(/*volume*/ 360, /*rating*/ 0, SHOT_FLAG_COMPLETED | SHOT_FLAG_DELETED);
    ShotIndexEntry out = applyIndexMetadata(e, /*rating*/ 5, /*volume*/ 420);
    TEST_ASSERT_TRUE((out.flags & SHOT_FLAG_DELETED) != 0);
    TEST_ASSERT_TRUE((out.flags & SHOT_FLAG_HAS_NOTES) != 0);
}

static int runShotIndexMetadataTests() {
    UNITY_BEGIN();
    RUN_TEST(test_rating_is_always_overwritten);
    RUN_TEST(test_zero_volume_override_keeps_recorded_weight);
    RUN_TEST(test_positive_volume_override_replaces_weight);
    RUN_TEST(test_rated_entry_gains_has_notes_flag_preserving_others);
    RUN_TEST(test_zero_rating_does_not_set_has_notes);
    RUN_TEST(test_deleted_flag_is_preserved);
    return UNITY_END();
}

#ifdef ARDUINO
void setup() { runShotIndexMetadataTests(); }

void loop() {}
#else
int main(int argc, char **argv) { return runShotIndexMetadataTests(); }
#endif
