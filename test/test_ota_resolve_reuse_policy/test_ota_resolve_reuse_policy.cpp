// PRO-556: pure, host-testable coverage for the WebUIPlugin OTA resolve
// periodic-result reuse decision (OtaResolveReusePolicy.h).
//
// Background: WebUIPlugin::otaResolveTask (PRO-13) opens a fresh, independent
// HTTPS/TLS connection via GitHubOTA::checkForUpdates() to resolve a forced-tag
// / channel-switch head. The periodic background check (PRO-411/PRO-555) runs
// checkForUpdates() on the SAME `ota` instance every ~5 min and already caches
// a resolved head. PRO-556 avoids the redundant TLS handshake when a
// sufficiently-fresh, SAME-CHANNEL periodic result already exists; when it does
// not (different channel, too stale, empty, or the periodic check itself
// failed/deferred under PRO-555) the resolve falls back to the existing
// independent, PRO-554-heap-guarded checkForUpdates().
//
// This suite pins the staleness window value, the freshness boundary, the
// channel-match requirement, and the full reuse predicate. Header-only +
// const char* / integer based, so it links on [env:native] via `-I src`,
// mirroring the OtaResolveHeapPolicy.h / OtaUpdateCheckPolicy.h precedent.

#include "../../src/display/plugins/OtaResolveReusePolicy.h"
#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// Staleness window constant
// ---------------------------------------------------------------------------

// Documented default: half the 5-minute base periodic-check interval.
void test_staleness_window_is_half_check_interval(void) { TEST_ASSERT_EQUAL_UINT32(150000u, kOtaResolveReuseStalenessWindowMs); }

// ---------------------------------------------------------------------------
// otaPeriodicResultFresh — freshness boundary (strict-less-than on fresh side)
// ---------------------------------------------------------------------------

void test_fresh_zero_elapsed(void) { TEST_ASSERT_TRUE(otaPeriodicResultFresh(0u, 0u)); }

void test_fresh_one_ms_under_window(void) { TEST_ASSERT_TRUE(otaPeriodicResultFresh(0u, 149999u)); }

void test_fresh_exactly_window_is_stale(void) { TEST_ASSERT_FALSE(otaPeriodicResultFresh(0u, 150000u)); }

void test_fresh_past_window_is_stale(void) { TEST_ASSERT_FALSE(otaPeriodicResultFresh(0u, 150001u)); }

// A millis() rollover between resolve-time and now must NOT read as a huge
// elapsed (false-stale): the unsigned subtraction yields the true small delta.
void test_fresh_survives_millis_rollover(void) {
    TEST_ASSERT_TRUE(otaPeriodicResultFresh(4294967295u, 4u));                     // 5ms elapsed across wrap
    TEST_ASSERT_FALSE(otaPeriodicResultFresh(4294967295u, 4294967295u + 150000u)); // exactly window across wrap
}

// Explicit-window overload is honored (not silently dropped).
void test_fresh_explicit_window(void) {
    TEST_ASSERT_TRUE(otaPeriodicResultFresh(0u, 99u, 100u));
    TEST_ASSERT_FALSE(otaPeriodicResultFresh(0u, 100u, 100u));
}

// ---------------------------------------------------------------------------
// otaResolveCanReusePeriodic — the full reuse decision
// ---------------------------------------------------------------------------

// The happy path: a fresh, successful, same-channel, non-empty result IS reused.
void test_reuse_fresh_same_channel_success(void) {
    TEST_ASSERT_TRUE(otaResolveCanReusePeriodic(/*haveEverChecked=*/true, /*periodicFailed=*/false,
                                                /*periodicVersion=*/"2.0.14", /*periodicChannel=*/"stable",
                                                /*resolveChannel=*/"stable", /*periodicResolvedAtMs=*/0u,
                                                /*nowMs=*/1000u));
}

// A pinned-tag channel string ("tag:<semver>") is matched byte-for-byte too.
void test_reuse_pinned_tag_channel_match(void) {
    TEST_ASSERT_TRUE(otaResolveCanReusePeriodic(true, false, "2.0.8", "tag:2.0.8", "tag:2.0.8", 0u, 1000u));
}

// No periodic result ever existed -> nothing to reuse.
void test_no_reuse_when_never_checked(void) {
    TEST_ASSERT_FALSE(otaResolveCanReusePeriodic(/*haveEverChecked=*/false, false, "2.0.14", "stable", "stable", 0u, 1000u));
}

// PRO-555 interaction: a failed/deferred periodic check leaves a stale head and
// does not advance lastUpdateCheck — its result must NOT be reused; the caller
// falls back to the heap-guarded independent handshake.
void test_no_reuse_when_periodic_failed(void) {
    TEST_ASSERT_FALSE(otaResolveCanReusePeriodic(true, /*periodicFailed=*/true, "2.0.14", "stable", "stable", 0u, 1000u));
}

// An empty / null cached version is never trustworthy.
void test_no_reuse_when_version_empty_or_null(void) {
    TEST_ASSERT_FALSE(otaResolveCanReusePeriodic(true, false, "", "stable", "stable", 0u, 1000u));
    TEST_ASSERT_FALSE(otaResolveCanReusePeriodic(true, false, nullptr, "stable", "stable", 0u, 1000u));
}

// The core correctness guard: the periodic check resolved a DIFFERENT channel
// than the one the user just selected — reusing it would resolve against the
// wrong channel's head.
void test_no_reuse_on_channel_mismatch(void) {
    TEST_ASSERT_FALSE(otaResolveCanReusePeriodic(true, false, "2.0.14", "beta", "stable", 0u, 1000u));
    // A plain channel vs a pinned tag are distinct channel strings.
    TEST_ASSERT_FALSE(otaResolveCanReusePeriodic(true, false, "2.0.14", "stable", "tag:2.0.14", 0u, 1000u));
}

// A null/empty channel on either side is not a confident match.
void test_no_reuse_on_empty_channel(void) {
    TEST_ASSERT_FALSE(otaResolveCanReusePeriodic(true, false, "2.0.14", "", "stable", 0u, 1000u));
    TEST_ASSERT_FALSE(otaResolveCanReusePeriodic(true, false, "2.0.14", "stable", "", 0u, 1000u));
    TEST_ASSERT_FALSE(otaResolveCanReusePeriodic(true, false, "2.0.14", nullptr, "stable", 0u, 1000u));
    TEST_ASSERT_FALSE(otaResolveCanReusePeriodic(true, false, "2.0.14", "stable", nullptr, 0u, 1000u));
}

// Everything matches but the result is stale -> refuse (fall back to handshake).
void test_no_reuse_when_stale(void) {
    // Exactly the window old.
    TEST_ASSERT_FALSE(otaResolveCanReusePeriodic(true, false, "2.0.14", "stable", "stable", 0u, 150000u));
    // A 6h backoff-delayed result (PRO-411 max interval) is nowhere near fresh.
    TEST_ASSERT_FALSE(otaResolveCanReusePeriodic(true, false, "2.0.14", "stable", "stable", 0u, 6u * 60u * 60u * 1000u));
}

// Just under the window with everything matching still reuses.
void test_reuse_just_under_window(void) {
    TEST_ASSERT_TRUE(otaResolveCanReusePeriodic(true, false, "2.0.14", "stable", "stable", 0u, 149999u));
}

static int runOtaResolveReusePolicyTests() {
    UNITY_BEGIN();
    RUN_TEST(test_staleness_window_is_half_check_interval);
    RUN_TEST(test_fresh_zero_elapsed);
    RUN_TEST(test_fresh_one_ms_under_window);
    RUN_TEST(test_fresh_exactly_window_is_stale);
    RUN_TEST(test_fresh_past_window_is_stale);
    RUN_TEST(test_fresh_survives_millis_rollover);
    RUN_TEST(test_fresh_explicit_window);
    RUN_TEST(test_reuse_fresh_same_channel_success);
    RUN_TEST(test_reuse_pinned_tag_channel_match);
    RUN_TEST(test_no_reuse_when_never_checked);
    RUN_TEST(test_no_reuse_when_periodic_failed);
    RUN_TEST(test_no_reuse_when_version_empty_or_null);
    RUN_TEST(test_no_reuse_on_channel_mismatch);
    RUN_TEST(test_no_reuse_on_empty_channel);
    RUN_TEST(test_no_reuse_when_stale);
    RUN_TEST(test_reuse_just_under_window);
    return UNITY_END();
}

#ifdef ARDUINO
void setup() { runOtaResolveReusePolicyTests(); }

void loop() {}
#else
int main(int argc, char **argv) { return runOtaResolveReusePolicyTests(); }
#endif
