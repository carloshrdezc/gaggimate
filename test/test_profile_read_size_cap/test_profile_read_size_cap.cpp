#include "display/core/SdReadRetryPolicy.h"
#include <unity.h>

// PRO-349: the boot-time ProfileManager enumeration / id-migration SD readers
// (collectProfileIdMigrations, findFilenameStemForId, remintUnsafeProfileIds)
// previously streamed deserializeJson() straight off an open SD File handle
// with NO internal-DRAM pre-flight gate and NO size cap -- the exact ENOMEM-spin
// pattern PRO-334 fixed for loadProfile(). They now route through the same
// readProfileFileBounded() helper, so the pre-flight gate (shouldAttemptSdRead)
// + size cap (sdReadSizeDecision) + bounded retry apply UNIFORMLY to every
// boot-time profile read.
//
// readProfileFileBounded() itself is FS-coupled (fs::FS*/fs::File) and lives in
// an anonymous namespace, so it cannot link in [env:native] (no SD/FS backend
// in the host shim). The two PURE decisions it is built on are header-only and
// host-linkable, so these tests pin the contract the enumeration path now
// shares with loadProfile():
//
//   1. sdReadSizeDecision(size) - the shared size cap. An oversized/corrupt
//      file is REFUSED (kTooLarge) before any read, so a giant file can never
//      force a large internal allocation on the memory-starved boot path; an
//      empty file is a failed read (kEmpty); a small file reads (kRead). This is
//      the cap the enumeration readers now apply that they previously lacked.
//   2. shouldAttemptSdRead(largestBlock) - the pre-flight gate. Below the
//      internal-DRAM floor the read is refused (documented failure) BEFORE the
//      enumeration parse, exactly like loadProfile(), so a starved boot degrades
//      to "skip + retry next boot" rather than parsing off a thrashing allocator.

void setUp(void) {}
void tearDown(void) {}

// --- Size cap (criterion 2: bounded read, never load an arbitrarily large file)

// A real profile JSON is a few KB: a representative small size reads in one
// bounded pass. This is the common boot-time enumeration case.
void test_size_decision_small_file_reads(void) {
    TEST_ASSERT_TRUE(SdReadSizeDecision::kRead == sdReadSizeDecision(4 * 1024));
    TEST_ASSERT_TRUE(SdReadSizeDecision::kRead == sdReadSizeDecision(1));
}

// Exactly at the cap is still read (<= boundary), one byte over is refused.
// Pinning the boundary stops a future cap tweak from silently shifting it.
void test_size_decision_cap_boundary(void) {
    TEST_ASSERT_TRUE(SdReadSizeDecision::kRead == sdReadSizeDecision(kProfileMaxFileBytes));
    TEST_ASSERT_TRUE(SdReadSizeDecision::kTooLarge == sdReadSizeDecision(kProfileMaxFileBytes + 1));
}

// The KEY PRO-349 invariant for the enumeration path: an oversized/corrupt file
// is rejected by the cap rather than read into memory. Before this refactor the
// enumeration scans streamed such a file off the handle unbounded.
void test_size_decision_oversized_file_refused(void) {
    TEST_ASSERT_TRUE(SdReadSizeDecision::kTooLarge == sdReadSizeDecision(kProfileMaxFileBytes * 4));
    TEST_ASSERT_TRUE(SdReadSizeDecision::kTooLarge == sdReadSizeDecision(static_cast<size_t>(8) * 1024 * 1024));
}

// An empty file is not "read 0 bytes ok" -- it is a failed read the caller skips
// (the enumeration loop continues to the next file), mirroring the loadProfile()
// helper's treatment of a zero-size file.
void test_size_decision_empty_file_is_failed_read(void) { TEST_ASSERT_TRUE(SdReadSizeDecision::kEmpty == sdReadSizeDecision(0)); }

// --- Pre-flight gate (criterion 3: gate-closed degrades gracefully, no parse)

// Below the internal-DRAM floor the gate is closed: the enumeration reader
// returns the documented failure (skip this entry) BEFORE attempting the read or
// the parse, exactly as loadProfile() does. This is the gate the enumeration
// scans previously bypassed entirely.
void test_gate_closed_below_floor_returns_failure(void) {
    TEST_ASSERT_FALSE(shouldAttemptSdRead(kSdReadInternalDramFloorBytes - 1));
    TEST_ASSERT_FALSE(shouldAttemptSdRead(0));
}

// With healthy internal DRAM the enumeration read proceeds, preserving existing
// behavior (same set of profiles enumerated / same id-migration outcomes).
void test_gate_open_when_dram_healthy_proceeds(void) {
    TEST_ASSERT_TRUE(shouldAttemptSdRead(kSdReadInternalDramFloorBytes));
    TEST_ASSERT_TRUE(shouldAttemptSdRead(kSdReadInternalDramFloorBytes * 8));
}

static int runProfileReadSizeCapTests() {
    UNITY_BEGIN();
    RUN_TEST(test_size_decision_small_file_reads);
    RUN_TEST(test_size_decision_cap_boundary);
    RUN_TEST(test_size_decision_oversized_file_refused);
    RUN_TEST(test_size_decision_empty_file_is_failed_read);
    RUN_TEST(test_gate_closed_below_floor_returns_failure);
    RUN_TEST(test_gate_open_when_dram_healthy_proceeds);
    return UNITY_END();
}

#ifdef ARDUINO
void setup() { runProfileReadSizeCapTests(); }

void loop() {}
#else
int main(int argc, char **argv) { return runProfileReadSizeCapTests(); }
#endif
