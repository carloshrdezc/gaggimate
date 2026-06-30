#include "display/core/SdReadRetryPolicy.h"
#include <unity.h>

// PRO-334: internal DMA-capable DRAM exhaustion on the display (ESP32-S3,
// Arduino-esp32 3.x / IDF 5.x) under HomeKit + BLE + WiFi + mDNS + TLS. The
// worst symptom: clicking Brew loads a profile, the WebUI sends
// `req:profiles:load`, and the handler reads the profile JSON off the SD card
// INLINE ON THE AsyncTCP task. When sdmmc_read_sectors can't get its DMA buffer
// it returns ESP_ERR_NO_MEM (0x101) repeatedly; the old streaming parse kept
// asking for bytes so async_tcp never pet its watchdog -> task-WDT abort ->
// REBOOT.
//
// The fix routes the read through a bounded, ENOMEM-aware helper whose two pure
// decisions live in SdReadRetryPolicy.h (no Arduino/SD/FreeRTOS deps, so they
// link and run in [env:native], mirroring WiFiFallbackPolicy.h):
//   1. shouldAttemptSdRead(largestFreeInternalBlock) - a pre-flight gate: skip
//      the DMA-backed read when internal DRAM is below a floor (fail fast with a
//      clean error rather than thrash a starving allocator on the async task).
//   2. nextSdReadBackoffMs(attempt) - a STRICTLY BOUNDED retry schedule so a
//      transient read failure becomes a clean give-up, never an unbounded spin.
//
// These tests pin the decision contract the firmware relies on to guarantee the
// async profile-load path can NEVER wedge the watchdog into a reboot.

void setUp(void) {}
void tearDown(void) {}

// Plenty of internal DRAM: the read is attempted. The floor is the meaningful
// "can a DMA sector + small JSON working set fit" signal, so well above it must
// pass the gate.
void test_attempt_when_well_above_floor(void) { TEST_ASSERT_TRUE(shouldAttemptSdRead(kSdReadInternalDramFloorBytes * 8)); }

// Exactly at the floor the read is attempted (>= boundary acts), so a device
// sitting right on the threshold still serves profiles rather than refusing.
void test_attempt_at_exactly_floor(void) { TEST_ASSERT_TRUE(shouldAttemptSdRead(kSdReadInternalDramFloorBytes)); }

// One byte below the floor: refuse. This is the fail-safe — under exhaustion we
// return a clean error to the client instead of poking the sdmmc DMA allocator
// and risking the async-task WDT reboot.
void test_refuse_just_below_floor(void) { TEST_ASSERT_FALSE(shouldAttemptSdRead(kSdReadInternalDramFloorBytes - 1)); }

// Zero largest block (internal pool fully fragmented/exhausted, the captured
// failure state): always refuse.
void test_refuse_when_internal_exhausted(void) { TEST_ASSERT_FALSE(shouldAttemptSdRead(0)); }

// The backoff schedule is strictly bounded: attempt 0 (the first try, no prior
// failure) has no backoff, and any attempt at or past the max-attempts count
// returns 0 so the caller's retry loop is naturally terminated.
void test_backoff_zero_for_first_and_out_of_range(void) {
    TEST_ASSERT_EQUAL_UINT32(0UL, nextSdReadBackoffMs(0));
    TEST_ASSERT_EQUAL_UINT32(0UL, nextSdReadBackoffMs(-1));
    TEST_ASSERT_EQUAL_UINT32(0UL, nextSdReadBackoffMs(kSdReadMaxAttempts));
    TEST_ASSERT_EQUAL_UINT32(0UL, nextSdReadBackoffMs(kSdReadMaxAttempts + 5));
}

// The in-range retry backoffs are short, positive, and monotonically
// non-decreasing — bounded waits that yield the CPU so the watchdog is pet
// between tries.
void test_backoff_in_range_is_short_and_monotonic(void) {
    unsigned long prev = 0;
    for (int attempt = 1; attempt < kSdReadMaxAttempts; ++attempt) {
        const unsigned long b = nextSdReadBackoffMs(attempt);
        TEST_ASSERT_GREATER_THAN_UINT32(0UL, b);
        TEST_ASSERT_GREATER_OR_EQUAL_UINT32(prev, b);
        prev = b;
    }
}

// The KEY watchdog-safety invariant: the worst-case total time spent in bounded
// backoffs across ALL retries must stay tiny (well under the task-WDT window of
// several seconds), so the bounded-retry policy can never itself be the cause of
// a missed watchdog reset. Sum every in-range backoff and assert a hard ceiling.
void test_total_bounded_backoff_is_tiny(void) {
    unsigned long total = 0;
    for (int attempt = 1; attempt < kSdReadMaxAttempts; ++attempt) {
        total += nextSdReadBackoffMs(attempt);
    }
    // 10 + 20 = 30 ms today; pin a generous-but-still-tiny 200 ms ceiling so a
    // future schedule tweak that accidentally introduces a multi-second wait
    // (which could starve the WDT) fails this test.
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(200UL, total);
}

// Retry budget is bounded and greater than one (so a single transient blip is
// retried, but the loop always terminates).
void test_max_attempts_is_bounded(void) {
    TEST_ASSERT_GREATER_THAN_INT(1, kSdReadMaxAttempts);
    TEST_ASSERT_LESS_OR_EQUAL_INT(5, kSdReadMaxAttempts);
}

// The profile-file size cap is a sane bound: large enough for any real profile
// JSON (a few KB), small enough to stop a corrupt/huge file forcing a big
// internal allocation on the memory-starved async path.
void test_profile_file_cap_is_sane(void) {
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(static_cast<unsigned long>(8 * 1024), static_cast<unsigned long>(kProfileMaxFileBytes));
}

static int runSdReadRetryPolicyTests() {
    UNITY_BEGIN();
    RUN_TEST(test_attempt_when_well_above_floor);
    RUN_TEST(test_attempt_at_exactly_floor);
    RUN_TEST(test_refuse_just_below_floor);
    RUN_TEST(test_refuse_when_internal_exhausted);
    RUN_TEST(test_backoff_zero_for_first_and_out_of_range);
    RUN_TEST(test_backoff_in_range_is_short_and_monotonic);
    RUN_TEST(test_total_bounded_backoff_is_tiny);
    RUN_TEST(test_max_attempts_is_bounded);
    RUN_TEST(test_profile_file_cap_is_sane);
    return UNITY_END();
}

#ifdef ARDUINO
void setup() { runSdReadRetryPolicyTests(); }

void loop() {}
#else
int main(int argc, char **argv) { return runSdReadRetryPolicyTests(); }
#endif
