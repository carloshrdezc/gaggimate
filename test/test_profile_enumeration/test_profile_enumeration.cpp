#include "display/core/ProfileEnumeration.h"
#include "display/core/SdReadRetryPolicy.h"
#include <unity.h>

// PRO-354 (follow-up to PRO-349 audit finding F3): the boot-time ProfileManager
// directory scanners (collectProfileIdMigrations, remintUnsafeProfileIds,
// findFilenameStemForId, listProfiles, and the setup-time id scan) each carried
// their own copy of the PURE name->stem->path math: strip the directory + a
// single trailing ".json" to get the addressable stem, then rebuild the
// canonical path as `dir + "/" + stem + ".json"`. That logic has NO fs::FS* /
// fs::File dependency, so PRO-354 extracted it into ProfileEnumeration.h (one
// inline definition, mirroring how SdReadRetryPolicy.h split the pure
// size/gate decisions out of the FS-coupled read loop). These tests pin the
// extracted contract host-side, with no ESP32/SD/FS backend:
//
//   - filenameStem() round-trips for plain, dotted/multi-extension, and
//     directory-qualified names, plus the no-extension / empty edges.
//   - reconstructProfilePath() is the exact inverse the scanners apply.
//   - isProfileFilename() / reconstructProfileEntries() reproduce the
//     enumeration filter+reconstruct loop the scanners share (non-".json" names
//     skipped), so the loop STRUCTURE is host-testable without openNextFile().
//   - the empty/over-cap SKIP decisions on the enumeration path are still owned
//     by SdReadRetryPolicy.h's sdReadSizeDecision() (reused here, not
//     duplicated) — every enumerated entry routes through that cap.

void setUp(void) {}
void tearDown(void) {}

// --- filenameStem round-trip (criterion: pure path-reconstruction extracted) -

// A plain "<id>.json" name maps to "<id>" and rebuilds to "<id>.json": the
// common case for every freshly-saved profile (profilePath() writes this shape).
void test_stem_plain_name_roundtrips(void) {
    TEST_ASSERT_TRUE(String("foo") == filenameStem("foo.json"));
    TEST_ASSERT_TRUE(String("/p/foo.json") == reconstructProfilePath("/p", "foo.json"));
    // Round-trip: stem -> path -> stem is stable.
    TEST_ASSERT_TRUE(String("foo") == filenameStem(reconstructProfilePath("/p", "foo.json")));
}

// A dotted / multi-extension name keeps everything before the FINAL ".json":
// "my.profile.json" -> "my.profile" -> "my.profile.json". Only one trailing
// ".json" is stripped, so the inner dot survives the round-trip. This is the
// case the issue calls out explicitly.
void test_stem_dotted_multi_extension_roundtrips(void) {
    TEST_ASSERT_TRUE(String("my.profile") == filenameStem("my.profile.json"));
    TEST_ASSERT_TRUE(String("/p/my.profile.json") == reconstructProfilePath("/p", "my.profile.json"));
    // Round-trip preserves the dotted stem.
    TEST_ASSERT_TRUE(String("my.profile.json") == reconstructProfilePath("/p", "my.profile.json").substring(3));
    TEST_ASSERT_TRUE(String("my.profile") == filenameStem(reconstructProfilePath("/p", "my.profile.json")));
    // A name ending in ".json.json" loses exactly one extension.
    TEST_ASSERT_TRUE(String("backup.json") == filenameStem("backup.json.json"));
}

// File::name() can be bare ("abc.json") or directory-qualified
// ("/p/abc.json"); the leading directory is stripped via the last '/', so both
// forms yield the same stem and the same reconstructed path. This is exactly
// why the scanners rebuild the path from the stem rather than trusting name().
void test_stem_directory_qualified_basename_extraction(void) {
    TEST_ASSERT_TRUE(String("abc") == filenameStem("/p/abc.json"));
    TEST_ASSERT_TRUE(String("abc") == filenameStem("abc.json"));
    // Nested directories: only the basename survives.
    TEST_ASSERT_TRUE(String("abc") == filenameStem("/data/p/sub/abc.json"));
    // Reconstruction always re-roots under the SCANNED dir, dropping any
    // directory that was on the entry name (the ESP32-backend hazard).
    TEST_ASSERT_TRUE(String("/p/abc.json") == reconstructProfilePath("/p", "/somewhere/else/abc.json"));
}

// Edge cases: a name without a ".json" extension is returned basename-only
// (nothing stripped), and an empty name stays empty. The scanners never enqueue
// a non-".json" name (see the filter test below), but filenameStem itself must
// not over-strip when there is no extension.
void test_stem_non_json_and_empty_edges(void) {
    // No ".json" extension: basename returned unchanged.
    TEST_ASSERT_TRUE(String("README") == filenameStem("README"));
    TEST_ASSERT_TRUE(String("notes.txt") == filenameStem("notes.txt"));
    TEST_ASSERT_TRUE(String("file") == filenameStem("/p/file"));
    // Empty name -> empty stem.
    TEST_ASSERT_TRUE(filenameStem("").isEmpty());
    // A bare ".json" (no stem) collapses to empty.
    TEST_ASSERT_TRUE(filenameStem(".json").isEmpty());
    TEST_ASSERT_TRUE(filenameStem("/p/.json").isEmpty());
}

// --- Enumeration filter + reconstruct loop (criterion: loop structure tested) -

// isProfileFilename() is the ".json" gate every scanner applies before pushing
// a candidate name. Non-".json" entries (directories, stray files) are skipped.
void test_filter_only_json_names(void) {
    TEST_ASSERT_TRUE(isProfileFilename("foo.json"));
    TEST_ASSERT_TRUE(isProfileFilename("/p/foo.json"));
    TEST_ASSERT_FALSE(isProfileFilename("foo.txt"));
    TEST_ASSERT_FALSE(isProfileFilename("foo"));
    TEST_ASSERT_FALSE(isProfileFilename(""));
    // A name merely CONTAINING ".json" mid-string is not a profile file.
    TEST_ASSERT_FALSE(isProfileFilename("foo.json.bak"));
}

// reconstructProfileEntries() reproduces the shared enumeration loop: it filters
// a list of raw entry names to the ".json" ones and yields, per entry, the stem
// and the canonical reconstructed path. Non-".json" names are dropped, so the
// output count and contents match what the FS-bound scanners enqueue.
void test_enumeration_filter_and_reconstruct(void) {
    std::vector<String> names;
    names.push_back("a.json");          // kept
    names.push_back("my.profile.json"); // kept (dotted)
    names.push_back("/p/b.json");       // kept (directory-qualified)
    names.push_back("notes.txt");       // skipped (not .json)
    names.push_back("");                // skipped (empty)
    names.push_back("dir");             // skipped (no extension)

    std::vector<ProfileEntry> entries = reconstructProfileEntries("/p", names);

    TEST_ASSERT_EQUAL_UINT32(3, static_cast<uint32_t>(entries.size()));

    TEST_ASSERT_TRUE(String("a") == entries[0].stem);
    TEST_ASSERT_TRUE(String("/p/a.json") == entries[0].path);

    TEST_ASSERT_TRUE(String("my.profile") == entries[1].stem);
    TEST_ASSERT_TRUE(String("/p/my.profile.json") == entries[1].path);

    // Directory on the entry name is dropped; path re-rooted under the scan dir.
    TEST_ASSERT_TRUE(String("b") == entries[2].stem);
    TEST_ASSERT_TRUE(String("/p/b.json") == entries[2].path);
}

// An empty name list yields no entries (empty directory case).
void test_enumeration_empty_list(void) {
    std::vector<String> names;
    std::vector<ProfileEntry> entries = reconstructProfileEntries("/p", names);
    TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(entries.size()));
}

// --- Empty / over-cap skip decisions on the enumeration path -----------------
//
// The size cap is owned by SdReadRetryPolicy.h (sdReadSizeDecision) and shared
// by loadProfile() AND every enumeration reader (PRO-349). Re-pin the
// empty/over-cap skip decisions here so this test documents that an enumerated
// entry whose file is empty or oversized is SKIPPED (failed read) rather than
// parsed — the criterion calls for the empty/over-cap skip decisions on the
// enumeration path. (Reused, not duplicated, per the issue.)

void test_enumeration_empty_file_is_skipped(void) { TEST_ASSERT_TRUE(SdReadSizeDecision::kEmpty == sdReadSizeDecision(0)); }

void test_enumeration_oversized_file_is_skipped(void) {
    TEST_ASSERT_TRUE(SdReadSizeDecision::kTooLarge == sdReadSizeDecision(kProfileMaxFileBytes + 1));
    TEST_ASSERT_TRUE(SdReadSizeDecision::kRead == sdReadSizeDecision(kProfileMaxFileBytes));
    TEST_ASSERT_TRUE(SdReadSizeDecision::kRead == sdReadSizeDecision(4 * 1024));
}

static int runProfileEnumerationTests() {
    UNITY_BEGIN();
    RUN_TEST(test_stem_plain_name_roundtrips);
    RUN_TEST(test_stem_dotted_multi_extension_roundtrips);
    RUN_TEST(test_stem_directory_qualified_basename_extraction);
    RUN_TEST(test_stem_non_json_and_empty_edges);
    RUN_TEST(test_filter_only_json_names);
    RUN_TEST(test_enumeration_filter_and_reconstruct);
    RUN_TEST(test_enumeration_empty_list);
    RUN_TEST(test_enumeration_empty_file_is_skipped);
    RUN_TEST(test_enumeration_oversized_file_is_skipped);
    return UNITY_END();
}

#ifdef ARDUINO
void setup() { runProfileEnumerationTests(); }

void loop() {}
#else
int main(int argc, char **argv) { return runProfileEnumerationTests(); }
#endif
