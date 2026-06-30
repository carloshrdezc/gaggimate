#include "display/plugins/OtaCheckPolicy.h"
#include <unity.h>

// PRO-345: the OTA version-check defer (PRO-334) was a guaranteed PERMANENT
// starve. On this hardware the normal steady state (HomeKit + BLE + WiFi + mDNS
// all up) sits below the 48 KB internal-DRAM floor, so the check was deferred
// every interval forever: checkForUpdates() never ran, the success-path status
// update never ran, and the web UI was stuck at "Checking..." with no update
// ever offered.
//
// The fix makes the defer RECOVERABLE (forward progress): at/above the preferred
// floor we run every normal interval; below it we still attempt on a longer
// ESCALATED cadence as long as we clear a hard absolute-minimum floor (the OOM
// guard that keeps PRO-334's -32512 protection intact); below that minimum, or
// before the escalated timer matures, we DEFER (and the caller surfaces a
// truthful status instead of "Checking...").
//
// These tests pin the pure decision (the three outcomes + both floors + both
// cadences) the firmware relies on, with no Arduino / heap_caps deps so they
// link and run in [env:native] (mirrors test_ssl_relay_startup_policy).

void setUp(void) {}
void tearDown(void) {}

// Convenience: realistic constants matching the production call. All knobs come
// from OtaCheckPolicy.h — the single source of truth — including the PREFERRED
// fast-path floor (kOtaCheckInternalDramFloorBytes, 48 KB), so this test can no
// longer drift from the firmware value. The policy takes the floors + cadences
// as arguments; we forward the real symbols, exactly as WebUIPlugin does.
static constexpr size_t kPreferred = kOtaCheckInternalDramFloorBytes;              // 48 KB
static constexpr size_t kAbsMin = kOtaCheckAbsoluteMinInternalDramBytes;           // 40 KB
static constexpr unsigned long kNormal = 5UL * 60UL * 1000UL;                       // UPDATE_CHECK_INTERVAL
static constexpr unsigned long kEscalated = kOtaCheckEscalatedRetryIntervalMs;      // 1 hour

static OtaCheckDecision decide(size_t block, unsigned long now, unsigned long lastCheck) {
    return otaCheckDecision(block, now, lastCheck, kNormal, kEscalated, kPreferred, kAbsMin);
}

// ---------------------------------------------------------------------------
// Fast path: at or above the preferred floor -> Run on the normal cadence.
// ---------------------------------------------------------------------------

// Well above the floor and the normal interval has elapsed: run the check.
void test_run_when_well_above_floor(void) {
    TEST_ASSERT_EQUAL(static_cast<int>(OtaCheckDecision::Run),
                      static_cast<int>(decide(kPreferred * 4, /*now=*/kNormal + 2, /*lastCheck=*/1)));
}

// Exactly at the preferred floor: run (>= boundary acts), so a device sitting
// right on the threshold still checks rather than needlessly deferring.
void test_run_at_exactly_preferred_floor(void) {
    TEST_ASSERT_EQUAL(static_cast<int>(OtaCheckDecision::Run),
                      static_cast<int>(decide(kPreferred, /*now=*/kNormal + 2, /*lastCheck=*/1)));
}

// Never run yet (lastCheck == 0) and above the floor: the first check fires
// immediately regardless of `now`.
void test_run_on_first_ever_check(void) {
    TEST_ASSERT_EQUAL(static_cast<int>(OtaCheckDecision::Run),
                      static_cast<int>(decide(kPreferred * 2, /*now=*/0, /*lastCheck=*/0)));
}

// ---------------------------------------------------------------------------
// Skip: not yet time for the normal interval, regardless of memory.
// ---------------------------------------------------------------------------

// Plenty of memory but the normal interval has NOT elapsed: skip (no check, no
// status churn).
void test_skip_before_normal_interval(void) {
    TEST_ASSERT_EQUAL(static_cast<int>(OtaCheckDecision::Skip),
                      static_cast<int>(decide(kPreferred * 4, /*now=*/1000, /*lastCheck=*/900)));
}

// Below the floor but the normal interval has NOT elapsed: still Skip — the
// defer (and its status) only applies once a check would otherwise be due.
void test_skip_below_floor_before_interval(void) {
    TEST_ASSERT_EQUAL(static_cast<int>(OtaCheckDecision::Skip),
                      static_cast<int>(decide(kPreferred - 1, /*now=*/1000, /*lastCheck=*/900)));
}

// ---------------------------------------------------------------------------
// Defer-with-status: below the preferred floor, before the escalated cadence.
// This is the case that used to hang at "Checking..." forever.
// ---------------------------------------------------------------------------

// Just below the preferred floor, normal interval elapsed, escalated cadence NOT
// yet matured: Defer. The caller surfaces a truthful "deferred — low memory"
// status rather than leaving the UI stuck at "Checking...".
void test_defer_just_below_floor_before_escalated(void) {
    const unsigned long lastCheck = 1000;
    const unsigned long now = lastCheck + kNormal + 1; // normal due, escalated not
    TEST_ASSERT_EQUAL(static_cast<int>(OtaCheckDecision::Defer),
                      static_cast<int>(decide(kPreferred - 1, now, lastCheck)));
}

// ---------------------------------------------------------------------------
// OOM guard (PRO-334 -32512 protection stays intact): below the absolute-minimum
// floor we ALWAYS defer, never attempt — even when the escalated cadence has
// matured.
// ---------------------------------------------------------------------------

// Below the hard minimum and escalated cadence elapsed: STILL Defer. We must
// never drive a handshake that would OOM, no matter how long we've waited.
void test_defer_below_absolute_min_even_when_escalated_due(void) {
    const unsigned long lastCheck = 1000;
    const unsigned long now = lastCheck + kEscalated + 1; // escalated due
    TEST_ASSERT_EQUAL(static_cast<int>(OtaCheckDecision::Defer),
                      static_cast<int>(decide(kAbsMin - 1, now, lastCheck)));
}

// Internal pool fully exhausted (largest block 0 — the captured -32512 state),
// escalated cadence elapsed: always Defer.
void test_defer_when_internal_exhausted(void) {
    const unsigned long lastCheck = 1000;
    const unsigned long now = lastCheck + kEscalated + 1;
    TEST_ASSERT_EQUAL(static_cast<int>(OtaCheckDecision::Defer),
                      static_cast<int>(decide(0, now, lastCheck)));
}

// ---------------------------------------------------------------------------
// Forward progress (the heart of PRO-345): below the preferred floor but above
// the absolute minimum, once the escalated cadence matures we make a real
// attempt so the update CAN be discovered. This is what makes the defer
// recoverable rather than a permanent starve.
// ---------------------------------------------------------------------------

// Below the preferred floor, above the absolute minimum, escalated cadence
// elapsed: Run (the opportunistic forward-progress attempt).
void test_run_forward_progress_after_escalated_wait(void) {
    const unsigned long lastCheck = 1000;
    const unsigned long now = lastCheck + kEscalated + 1; // escalated due
    TEST_ASSERT_EQUAL(static_cast<int>(OtaCheckDecision::Run),
                      static_cast<int>(decide(kPreferred - 1, now, lastCheck)));
}

// Exactly at the absolute-minimum floor, escalated cadence elapsed: Run
// (>= boundary acts on the OOM guard too).
void test_run_forward_progress_at_exactly_absolute_min(void) {
    const unsigned long lastCheck = 1000;
    const unsigned long now = lastCheck + kEscalated + 1;
    TEST_ASSERT_EQUAL(static_cast<int>(OtaCheckDecision::Run),
                      static_cast<int>(decide(kAbsMin, now, lastCheck)));
}

// A below-floor device makes forward progress repeatedly: defer until the
// escalated timer matures, then run, then (after running advances lastCheck) it
// defers again until the next escalated window — never a permanent silent hang.
void test_below_floor_cycles_defer_then_run_then_defer(void) {
    const size_t block = kPreferred - 1; // pinned below preferred, above abs-min
    unsigned long lastCheck = 1000;
    // Shortly after the normal interval: defer (escalated not yet matured).
    unsigned long now = lastCheck + kNormal + 1;
    TEST_ASSERT_EQUAL(static_cast<int>(OtaCheckDecision::Defer), static_cast<int>(decide(block, now, lastCheck)));
    // Escalated window matures: run (forward progress).
    now = lastCheck + kEscalated + 1;
    TEST_ASSERT_EQUAL(static_cast<int>(OtaCheckDecision::Run), static_cast<int>(decide(block, now, lastCheck)));
    // The caller advanced lastCheck to `now` after running. Soon after, we defer
    // again until the next escalated window — bounded, recoverable, not a hang.
    lastCheck = now;
    now = lastCheck + kNormal + 1;
    TEST_ASSERT_EQUAL(static_cast<int>(OtaCheckDecision::Defer), static_cast<int>(decide(block, now, lastCheck)));
}

// ---------------------------------------------------------------------------
// Threshold sanity: the floors are defensible and ordered, single source of
// truth (absolute minimum strictly below the preferred floor, and the minimum
// is not absurdly small for a TLS handshake).
// ---------------------------------------------------------------------------
void test_floors_are_ordered_and_defensible(void) {
    // OOM guard sits strictly below the preferred floor so escalated attempts
    // have a window in which to act.
    TEST_ASSERT_TRUE(kAbsMin < kPreferred);
    // The absolute minimum is still a meaningful chunk of contiguous DRAM (the
    // mbedTLS handshake's transient draw is in the low-tens-of-KB range).
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(static_cast<unsigned long>(32 * 1024), static_cast<unsigned long>(kAbsMin));
    // Escalated cadence is much longer than the normal one (no retry-storm).
    TEST_ASSERT_GREATER_THAN_UINT32(static_cast<unsigned long>(kNormal), static_cast<unsigned long>(kEscalated));
}

static int runOtaCheckPolicyTests() {
    UNITY_BEGIN();
    RUN_TEST(test_run_when_well_above_floor);
    RUN_TEST(test_run_at_exactly_preferred_floor);
    RUN_TEST(test_run_on_first_ever_check);
    RUN_TEST(test_skip_before_normal_interval);
    RUN_TEST(test_skip_below_floor_before_interval);
    RUN_TEST(test_defer_just_below_floor_before_escalated);
    RUN_TEST(test_defer_below_absolute_min_even_when_escalated_due);
    RUN_TEST(test_defer_when_internal_exhausted);
    RUN_TEST(test_run_forward_progress_after_escalated_wait);
    RUN_TEST(test_run_forward_progress_at_exactly_absolute_min);
    RUN_TEST(test_below_floor_cycles_defer_then_run_then_defer);
    RUN_TEST(test_floors_are_ordered_and_defensible);
    return UNITY_END();
}

#ifdef ARDUINO
void setup() { runOtaCheckPolicyTests(); }

void loop() {}
#else
int main(int argc, char **argv) { return runOtaCheckPolicyTests(); }
#endif
