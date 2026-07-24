// PRO-563: pure, host-testable coverage for the post-TIMEOUT abandon grace
// window that keeps WebUIPlugin's periodic background OTA check skipped for a
// bounded interval AFTER a click-driven resolve is abandoned on its soft 10s
// timeout.
//
// Background (the residual PRO-560 gap this closes): PRO-560's predicate
// otaPeriodicCheckShouldSkipForResolve() skips the periodic check exactly while
// otaResolveState == Resolving. But per the PRO-13 point-7 design tradeoff
// (WebUIPlugin.h), a resolve abandoned on the soft 10s timeout is NEVER
// force-killed: loop() transitions Resolving -> Failed (one tick) -> Idle, yet
// the abandoned task keeps running ota->checkForUpdates() to completion in the
// background and can still be touching the shared, non-reentrant GitHubOTA
// instance for a few more seconds. In that residual window the plain skip
// predicate returns false and would let the periodic check reopen the exact
// race PRO-560 closed — but only in the narrow case where (a) the resolve
// stalls to the full timeout AND (b) the ~5 min periodic interval also elapses
// in that immediate post-timeout window.
//
// The fix adds otaPeriodicCheckShouldSkipForResolveDuringGrace() and the
// combined otaPeriodicCheckShouldSkip() in OtaAsyncResolvePolicy.h, which loop()
// now calls: the periodic check stays skipped until
// otaResolveStartMs + kOtaResolveTimeoutMs + grace has plausibly elapsed. This
// suite pins that contract, mirroring test_ota_periodic_check_resolve_guard.
//
// Header-only + enum based, so it links on [env:native] via `-I src`, mirroring
// the OtaAsyncResolvePolicy.h precedent. No platformio.ini change is required
// (test/test_* is auto-discovered).

#include "../../src/display/plugins/OtaAsyncResolvePolicy.h"
#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

static constexpr uint32_t kTimeoutMs = 10000; // mirrors WebUIPlugin::kOtaResolveTimeoutMs
static constexpr uint32_t kGraceMs = kOtaResolveAbandonGraceMs;

// ---------------------------------------------------------------------------
// otaPeriodicCheckShouldSkipForResolveDuringGrace(): the grace window itself.
// ---------------------------------------------------------------------------

// A resolve that did NOT time out (an immediate refuse, or one that completed)
// never enters the abandon grace window — its task already posted its result and
// exited, so `ota` is free and the periodic check must not be needlessly skipped.
void test_grace_inactive_for_non_timeout(void) {
    // Even at what would otherwise be inside the window, a false flag means no skip.
    TEST_ASSERT_FALSE(
        otaPeriodicCheckShouldSkipForResolveDuringGrace(/*lastResolveTimedOut=*/false, 0, 11000, kTimeoutMs, kGraceMs));
}

// Right at the timeout boundary (nowMs - startMs == timeoutMs) the abandoned
// task has only just been left running, so the grace window is active.
void test_grace_active_at_timeout_boundary(void) {
    TEST_ASSERT_TRUE(
        otaPeriodicCheckShouldSkipForResolveDuringGrace(/*lastResolveTimedOut=*/true, 0, kTimeoutMs, kTimeoutMs, kGraceMs));
}

// Just before startMs + timeoutMs + graceMs the window is still open: the
// abandoned task might still be inside checkForUpdates().
void test_grace_active_just_before_close(void) {
    TEST_ASSERT_TRUE(otaPeriodicCheckShouldSkipForResolveDuringGrace(/*lastResolveTimedOut=*/true, 0,
                                                                     kTimeoutMs + kGraceMs - 1, kTimeoutMs, kGraceMs));
}

// Exactly at startMs + timeoutMs + graceMs the window has closed — the abandoned
// task's checkForUpdates() has plausibly finished, so the periodic check may run.
void test_grace_closed_at_window_end(void) {
    TEST_ASSERT_FALSE(otaPeriodicCheckShouldSkipForResolveDuringGrace(/*lastResolveTimedOut=*/true, 0,
                                                                      kTimeoutMs + kGraceMs, kTimeoutMs, kGraceMs));
}

// Well past the window the periodic check is definitely free again.
void test_grace_closed_well_after_window(void) {
    TEST_ASSERT_FALSE(otaPeriodicCheckShouldSkipForResolveDuringGrace(/*lastResolveTimedOut=*/true, 0,
                                                                      kTimeoutMs + kGraceMs + 60000, kTimeoutMs, kGraceMs));
}

// millis() rollover: startMs just before wraparound, nowMs just after. The
// unsigned subtraction yields the true (small) elapsed duration, so the window
// stays open and does NOT false-close early.
void test_grace_survives_millis_rollover(void) {
    TEST_ASSERT_TRUE(otaPeriodicCheckShouldSkipForResolveDuringGrace(/*lastResolveTimedOut=*/true,
                                                                     /*startMs=*/0xFFFFFFFFu, /*nowMs=*/5u, kTimeoutMs,
                                                                     kGraceMs));
}

// ---------------------------------------------------------------------------
// otaPeriodicCheckShouldSkip(): the combined decision loop() actually calls.
// ---------------------------------------------------------------------------

// An in-flight resolve is skipped (PRO-560 mutual exclusion) regardless of any
// timing / grace inputs — Resolving alone forces the skip.
void test_combined_skips_while_resolving(void) {
    TEST_ASSERT_TRUE(otaPeriodicCheckShouldSkip(
        OtaResolveSnapshot{OtaResolveState::Resolving, /*lastResolveTimedOut=*/false, 0, 0, kTimeoutMs, kGraceMs}));
}

// The whole point of PRO-563: immediately after a timeout the state is Failed
// (for one loop tick) yet the abandoned task may still touch `ota` — the grace
// window keeps the periodic check skipped even though PRO-560's predicate alone
// would let it run.
void test_combined_skips_failed_just_after_timeout(void) {
    // 2s past the 10s timeout -> inside the 5s grace window.
    TEST_ASSERT_TRUE(otaPeriodicCheckShouldSkip(
        OtaResolveSnapshot{OtaResolveState::Failed, /*lastResolveTimedOut=*/true, 0, kTimeoutMs + 2000, kTimeoutMs, kGraceMs}));
    // Sanity: PRO-560's predicate on its own would NOT skip a Failed state —
    // this is exactly the gap the combined predicate closes.
    TEST_ASSERT_FALSE(otaPeriodicCheckShouldSkipForResolve(OtaResolveState::Failed));
}

// loop() flips Failed -> Idle within one tick post-timeout; the grace window must
// span that transition, so an Idle state inside the window is still skipped.
void test_combined_skips_idle_inside_grace_after_timeout(void) {
    TEST_ASSERT_TRUE(otaPeriodicCheckShouldSkip(
        OtaResolveSnapshot{OtaResolveState::Idle, /*lastResolveTimedOut=*/true, 0, kTimeoutMs + 2000, kTimeoutMs, kGraceMs}));
}

// Once the grace window closes, the abandoned task has finished; an Idle state
// lets the periodic check run again — the skip is bounded, not permanent.
void test_combined_runs_idle_after_grace_closes(void) {
    TEST_ASSERT_FALSE(otaPeriodicCheckShouldSkip(
        OtaResolveSnapshot{OtaResolveState::Idle, /*lastResolveTimedOut=*/true, 0, kTimeoutMs + kGraceMs, kTimeoutMs, kGraceMs}));
}

// A NON-timeout Failed (immediate refuse) never gets a grace window: the task
// already exited, so the periodic check runs — no regression to the settled-state
// behavior PRO-560 established.
void test_combined_runs_non_timeout_failed(void) {
    TEST_ASSERT_FALSE(otaPeriodicCheckShouldSkip(
        OtaResolveSnapshot{OtaResolveState::Failed, /*lastResolveTimedOut=*/false, 0, 5000, kTimeoutMs, kGraceMs}));
}

// The plain no-resolve-pending case: Idle, never timed out -> periodic check runs.
void test_combined_runs_when_idle_no_timeout(void) {
    TEST_ASSERT_FALSE(otaPeriodicCheckShouldSkip(
        OtaResolveSnapshot{OtaResolveState::Idle, /*lastResolveTimedOut=*/false, 0, 0, kTimeoutMs, kGraceMs}));
}

// ReadyToFlash (a confirmed resolve) still runs the check — its task posted and
// exited, and it was never a timeout, so no grace applies.
void test_combined_runs_when_ready_to_flash(void) {
    TEST_ASSERT_FALSE(otaPeriodicCheckShouldSkip(
        OtaResolveSnapshot{OtaResolveState::ReadyToFlash, /*lastResolveTimedOut=*/false, 0, 0, kTimeoutMs, kGraceMs}));
}

// End-to-end timeline walk of the exact PRO-563 scenario: a resolve starts,
// stalls to the soft timeout, is abandoned (Resolving->Failed->Idle), and the
// periodic interval elapses right afterward. Assert the periodic check is skipped
// throughout the danger window and only re-enabled once the grace has elapsed.
void test_combined_timeout_abandon_timeline(void) {
    const uint32_t startMs = 100000;

    // t = startMs: resolve just spawned, in flight -> skipped (PRO-560).
    TEST_ASSERT_TRUE(otaPeriodicCheckShouldSkip(
        OtaResolveSnapshot{OtaResolveState::Resolving, false, startMs, startMs, kTimeoutMs, kGraceMs}));

    // t = startMs + 9.9s: still resolving, not yet timed out -> skipped.
    TEST_ASSERT_TRUE(otaPeriodicCheckShouldSkip(
        OtaResolveSnapshot{OtaResolveState::Resolving, false, startMs, startMs + 9900, kTimeoutMs, kGraceMs}));

    // t = startMs + 10s: timeout fires; loop() sets Failed and the timeout flag.
    // The abandoned task may still be inside checkForUpdates() -> skipped (grace).
    TEST_ASSERT_TRUE(otaPeriodicCheckShouldSkip(
        OtaResolveSnapshot{OtaResolveState::Failed, true, startMs, startMs + kTimeoutMs, kTimeoutMs, kGraceMs}));

    // t = startMs + 10s + 1 tick: Failed drained to Idle; still inside grace -> skipped.
    TEST_ASSERT_TRUE(otaPeriodicCheckShouldSkip(
        OtaResolveSnapshot{OtaResolveState::Idle, true, startMs, startMs + kTimeoutMs + 2, kTimeoutMs, kGraceMs}));

    // t = startMs + 14.999s: still inside grace -> skipped (the periodic interval
    // elapsing here must NOT trigger a concurrent checkForUpdates()).
    TEST_ASSERT_TRUE(otaPeriodicCheckShouldSkip(
        OtaResolveSnapshot{OtaResolveState::Idle, true, startMs, startMs + kTimeoutMs + kGraceMs - 1, kTimeoutMs, kGraceMs}));

    // t = startMs + 15s: grace closed; abandoned task has plausibly finished ->
    // periodic check runs again.
    TEST_ASSERT_FALSE(otaPeriodicCheckShouldSkip(
        OtaResolveSnapshot{OtaResolveState::Idle, true, startMs, startMs + kTimeoutMs + kGraceMs, kTimeoutMs, kGraceMs}));
}

static int runOtaPeriodicCheckResolveGraceTests() {
    UNITY_BEGIN();
    RUN_TEST(test_grace_inactive_for_non_timeout);
    RUN_TEST(test_grace_active_at_timeout_boundary);
    RUN_TEST(test_grace_active_just_before_close);
    RUN_TEST(test_grace_closed_at_window_end);
    RUN_TEST(test_grace_closed_well_after_window);
    RUN_TEST(test_grace_survives_millis_rollover);
    RUN_TEST(test_combined_skips_while_resolving);
    RUN_TEST(test_combined_skips_failed_just_after_timeout);
    RUN_TEST(test_combined_skips_idle_inside_grace_after_timeout);
    RUN_TEST(test_combined_runs_idle_after_grace_closes);
    RUN_TEST(test_combined_runs_non_timeout_failed);
    RUN_TEST(test_combined_runs_when_idle_no_timeout);
    RUN_TEST(test_combined_runs_when_ready_to_flash);
    RUN_TEST(test_combined_timeout_abandon_timeline);
    return UNITY_END();
}

#ifdef ARDUINO
void setup() { runOtaPeriodicCheckResolveGraceTests(); }

void loop() {}
#else
int main(int argc, char **argv) { return runOtaPeriodicCheckResolveGraceTests(); }
#endif
