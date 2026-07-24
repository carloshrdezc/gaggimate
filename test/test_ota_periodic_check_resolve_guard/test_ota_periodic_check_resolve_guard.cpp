// PRO-560: pure, host-testable coverage for the guard that skips WebUIPlugin's
// periodic background OTA check while a click-driven resolve is in flight.
//
// Background: the periodic update-check (WebUIPlugin::loop, PRO-411/PRO-555) and
// the forced-tag / channel-switch resolve task (otaResolveTask, PRO-13) both
// call ota->checkForUpdates() on the SAME, non-reentrant GitHubOTA instance —
// one on the loop task, one on the resolve task. If the ~5 min periodic interval
// elapses while a resolve is mid-flight AND the resolve takes its non-reuse
// fallback (PRO-556's reuse check missed), both could sit inside
// checkForUpdates() on the same instance at once. This is the long-standing
// PRO-13/PRO-411 interaction race.
//
// The fix expresses the guard as the pure predicate
// otaPeriodicCheckShouldSkipForResolve(OtaResolveState) in
// OtaAsyncResolvePolicy.h and wires it into loop() so the periodic check is
// skipped exactly while otaResolveState == Resolving — mutual exclusion on `ota`
// by construction, no runtime mutex. This suite pins that contract: the periodic
// check IS attempted for every settled state (Idle / ReadyToFlash / Failed) and
// IS skipped only while Resolving.
//
// Header-only + enum based, so it links on [env:native] via `-I src`, mirroring
// the OtaAsyncResolvePolicy.h / OtaResolveReusePolicy.h / OtaUpdateCheckPolicy.h
// precedent. No platformio.ini change is required (test/test_* is auto-discovered).

#include "../../src/display/plugins/OtaAsyncResolvePolicy.h"
#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

// While a resolve is in flight (Resolving) the resolve task may be inside
// ota->checkForUpdates() on its non-reuse fallback path; the periodic loop-task
// check MUST be skipped so the two cannot use the shared instance concurrently.
void test_periodic_check_skipped_while_resolving(void) {
    TEST_ASSERT_TRUE(otaPeriodicCheckShouldSkipForResolve(OtaResolveState::Resolving));
}

// Idle is the common case (no resolve pending): the periodic check runs normally.
void test_periodic_check_runs_when_idle(void) {
    TEST_ASSERT_FALSE(otaPeriodicCheckShouldSkipForResolve(OtaResolveState::Idle));
}

// A resolve that has settled to ReadyToFlash no longer touches `ota` (its task
// has posted its result and exited), so the periodic check is free to run.
void test_periodic_check_runs_when_ready_to_flash(void) {
    TEST_ASSERT_FALSE(otaPeriodicCheckShouldSkipForResolve(OtaResolveState::ReadyToFlash));
}

// Likewise a resolve that has settled to Failed no longer touches `ota`.
void test_periodic_check_runs_when_failed(void) {
    TEST_ASSERT_FALSE(otaPeriodicCheckShouldSkipForResolve(OtaResolveState::Failed));
}

// Mutual-exclusion contract, stated directly: the periodic check is skipped IFF
// the resolve state is Resolving — the ONLY state during which the resolve task
// can be inside checkForUpdates() on the shared instance. Every other state
// leaves the periodic check enabled. This is what guarantees the two paths never
// call checkForUpdates() concurrently.
void test_skip_predicate_is_exactly_resolving(void) {
    const OtaResolveState states[] = {OtaResolveState::Idle, OtaResolveState::Resolving, OtaResolveState::ReadyToFlash,
                                      OtaResolveState::Failed};
    for (OtaResolveState s : states) {
        const bool skipped = otaPeriodicCheckShouldSkipForResolve(s);
        const bool isResolving = (s == OtaResolveState::Resolving);
        TEST_ASSERT_EQUAL(isResolving, skipped);
    }
}

static int runOtaPeriodicCheckResolveGuardTests() {
    UNITY_BEGIN();
    RUN_TEST(test_periodic_check_skipped_while_resolving);
    RUN_TEST(test_periodic_check_runs_when_idle);
    RUN_TEST(test_periodic_check_runs_when_ready_to_flash);
    RUN_TEST(test_periodic_check_runs_when_failed);
    RUN_TEST(test_skip_predicate_is_exactly_resolving);
    return UNITY_END();
}

#ifdef ARDUINO
void setup() { runOtaPeriodicCheckResolveGuardTests(); }

void loop() {}
#else
int main(int argc, char **argv) { return runOtaPeriodicCheckResolveGuardTests(); }
#endif
