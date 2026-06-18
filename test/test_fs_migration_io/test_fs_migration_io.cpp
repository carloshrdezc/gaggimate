#include <unity.h>

#include <display/core/FsMigrationIo.h>

#include <map>
#include <string>
#include <vector>

// PRO-218 — host unit tests for the byte-moving I/O half of the migration
// (FsMigrationIo.h). This is the coverage gap that let the original
// silent-data-loss seam ship: the stage/restore/verify path had ZERO host
// tests, so unchecked writes, short reads, over-budget skips and fire-and-forget
// restores were invisible. These tests drive the orchestration against an
// in-memory fake filesystem and assert that every failure mode is REPORTED,
// not silently swallowed (review findings #1, #3, #4, #5).

void setUp(void) {}
void tearDown(void) {}

// ---- in-memory fake FS ------------------------------------------------------
//
// Models a source dir tree (for staging) and a destination tree (for restore).
// Knobs let a test inject the exact failure under test: an unwritable path
// (open failure), a short-write cap (ENOSPC), a short-read on a named file, or a
// rename failure — exactly the device-side hazards the reviewers flagged.
class FakeFs : public IMigrationFs {
  public:
    // ---- source (read) fixtures ----
    void addSourceFile(const std::string &dir, const std::string &name, uint32_t size) {
        _src[dir].push_back({name, size});
        _srcBytes[dir + "/" + name] = size;
    }

    // ---- failure injection knobs ----
    std::string unwritablePrefix;    // writeFile returns 0 (open fail) for paths starting with this
    std::string shortReadFile;       // readFile returns size-1 for this base name
    uint32_t writeCap = 0xFFFFFFFFu; // writeFile caps bytes written at this value
    bool renameAlwaysFails = false;  // renameFile returns false

    // ---- IMigrationFs ----
    bool dirExists(const char *dir) override { return _src.count(dir) > 0; }

    bool listDir(const char *dir, std::vector<FsDirEntry> &out) override {
        auto it = _src.find(dir);
        if (it == _src.end()) {
            return false;
        }
        out = it->second;
        return true;
    }

    size_t readFile(const char *dir, const char *name, uint32_t size, std::vector<uint8_t> &out) override {
        uint32_t got = size;
        if (shortReadFile == name && size > 0) {
            got = size - 1; // truncated read
        }
        out.assign(got, 0xAB);
        return got;
    }

    bool makeDirs(const char *) override { return true; }

    size_t writeFile(const char *path, const uint8_t *, size_t len) override {
        std::string p(path);
        if (!unwritablePrefix.empty() && p.rfind(unwritablePrefix, 0) == 0) {
            return 0; // open/write failure
        }
        size_t wrote = len < writeCap ? len : writeCap;
        _dst[p] = wrote; // record the temp file's written size
        return wrote;
    }

    bool renameFile(const char *from, const char *to) override {
        if (renameAlwaysFails) {
            return false;
        }
        auto it = _dst.find(from);
        if (it == _dst.end()) {
            return false;
        }
        _dst[to] = it->second;
        _dst.erase(it);
        return true;
    }

    bool destExists(const char *path) override { return _dst.count(path) > 0; }

    int64_t destSize(const char *path) override {
        auto it = _dst.find(path);
        return it == _dst.end() ? -1 : static_cast<int64_t>(it->second);
    }

  private:
    std::map<std::string, std::vector<FsDirEntry>> _src;
    std::map<std::string, uint32_t> _srcBytes;
    std::map<std::string, size_t> _dst; // path -> bytes written
};

// (1) Happy path: stage two profiles fully, restore + verify all of them.
void test_stage_restore_verify_happy_path(void) {
    FakeFs fs;
    fs.addSourceFile("/p", "a.json", 100);
    fs.addSourceFile("/p", "b.json", 200);

    std::vector<StagedFile> staged;
    StageResult r = stageDir(fs, "/p", "p", 64u * 1024u, staged);

    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_TRUE(r.complete());
    TEST_ASSERT_EQUAL_UINT32(2, r.fileCount);
    TEST_ASSERT_EQUAL_UINT32(0, r.skipped);
    TEST_ASSERT_EQUAL_UINT32(300, r.bytes);

    TEST_ASSERT_EQUAL_UINT32(2, restoreStaged(fs, staged));
    TEST_ASSERT_EQUAL_UINT32(2, verifyRestored(fs, staged));
}

// (2) Over-budget skip is REPORTED, not silently dropped (review #5). The second
// file pushes past the budget — complete() must be false so the caller refuses
// the marker.
void test_over_budget_skip_is_reported(void) {
    FakeFs fs;
    fs.addSourceFile("/h", "small.slog", 100);
    fs.addSourceFile("/h", "big.slog", 5000);

    std::vector<StagedFile> staged;
    StageResult r = stageDir(fs, "/h", "h", 1000u, staged); // budget fits only the first

    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_FALSE(r.complete()); // a skip means NOT fully preserved
    TEST_ASSERT_EQUAL_UINT32(1, r.fileCount);
    TEST_ASSERT_EQUAL_UINT32(1, r.skipped);
}

// (3) Short read is REPORTED (review #4/#5): resize(got) used to silently
// truncate. complete() must be false.
void test_short_read_is_reported(void) {
    FakeFs fs;
    fs.addSourceFile("/p", "trunc.json", 500);
    fs.shortReadFile = "trunc.json";

    std::vector<StagedFile> staged;
    StageResult r = stageDir(fs, "/p", "p", 64u * 1024u, staged);

    TEST_ASSERT_TRUE(r.shortRead);
    TEST_ASSERT_FALSE(r.complete());
    TEST_ASSERT_EQUAL_UINT32(1, r.fileCount); // file is staged, but flagged short
}

// (4) Restore open failure is NOT counted as restored (review #4). A write that
// can't open returns 0; restoreStaged must not count it, so the marker is gated.
void test_restore_open_failure_blocks_count(void) {
    FakeFs fs;
    fs.addSourceFile("/p", "a.json", 100);
    fs.addSourceFile("/p", "b.json", 200);
    std::vector<StagedFile> staged;
    stageDir(fs, "/p", "p", 64u * 1024u, staged);

    // Make every restore write fail to open.
    fs.unwritablePrefix = "/p/";

    uint32_t restored = restoreStaged(fs, staged);
    TEST_ASSERT_EQUAL_UINT32(0, restored);          // nothing fully written
    TEST_ASSERT_NOT_EQUAL(staged.size(), restored); // count mismatch -> no marker
}

// (5) Short write (ENOSPC / flash error) is NOT counted, and write-to-temp means
// the truncated bytes never land under the real name (review #4).
void test_short_write_blocks_count_and_leaves_real_name_clean(void) {
    FakeFs fs;
    fs.addSourceFile("/p", "a.json", 500);
    std::vector<StagedFile> staged;
    stageDir(fs, "/p", "p", 64u * 1024u, staged);

    fs.writeCap = 10; // every write truncates to 10 bytes

    uint32_t restored = restoreStaged(fs, staged);
    TEST_ASSERT_EQUAL_UINT32(0, restored); // short write -> not counted

    // The real target name must NOT exist (only the abandoned .tmp does).
    TEST_ASSERT_FALSE(fs.destExists("/p/a.json"));
    TEST_ASSERT_TRUE(fs.destExists("/p/a.json.mig.tmp"));
}

// (6) Rename failure is NOT counted (commit failed -> data not under real name).
void test_rename_failure_blocks_count(void) {
    FakeFs fs;
    fs.addSourceFile("/p", "a.json", 100);
    std::vector<StagedFile> staged;
    stageDir(fs, "/p", "p", 64u * 1024u, staged);

    fs.renameAlwaysFails = true;

    TEST_ASSERT_EQUAL_UINT32(0, restoreStaged(fs, staged));
    TEST_ASSERT_FALSE(fs.destExists("/p/a.json"));
}

// (7) The end-to-end count-mismatch gate (review #1/#2/#3): when one of two
// files fails to restore, verifyRestored != staged.size(), which is the
// condition the runner uses to refuse the once-only marker.
void test_partial_restore_fails_verification_gate(void) {
    FakeFs fs;
    fs.addSourceFile("/p", "good.json", 100);
    fs.addSourceFile("/p", "bad.json", 200);
    std::vector<StagedFile> staged;
    stageDir(fs, "/p", "p", 64u * 1024u, staged);

    // Make only "bad.json" unwritable.
    fs.unwritablePrefix = "/p/bad.json";

    uint32_t restored = restoreStaged(fs, staged);
    uint32_t verified = verifyRestored(fs, staged);

    TEST_ASSERT_EQUAL_UINT32(1, restored);
    TEST_ASSERT_EQUAL_UINT32(1, verified);
    // The gate the runner applies before writeMarker():
    bool markerAllowed = (restored == staged.size()) && (verified == staged.size());
    TEST_ASSERT_FALSE(markerAllowed); // marker is unreachable on a partial restore
}

// (8) Missing source directory stages nothing and is "complete" (nothing to do).
void test_missing_source_dir_is_complete_noop(void) {
    FakeFs fs;
    std::vector<StagedFile> staged;
    StageResult r = stageDir(fs, "/p", "p", 64u * 1024u, staged);
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_TRUE(r.complete());
    TEST_ASSERT_EQUAL_UINT32(0, r.fileCount);
    TEST_ASSERT_EQUAL_size_t(0, staged.size());
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_stage_restore_verify_happy_path);
    RUN_TEST(test_over_budget_skip_is_reported);
    RUN_TEST(test_short_read_is_reported);
    RUN_TEST(test_restore_open_failure_blocks_count);
    RUN_TEST(test_short_write_blocks_count_and_leaves_real_name_clean);
    RUN_TEST(test_rename_failure_blocks_count);
    RUN_TEST(test_partial_restore_fails_verification_gate);
    RUN_TEST(test_missing_source_dir_is_complete_noop);
    return UNITY_END();
}
