// PRO-11: pure, host-testable coverage for the WebUIPlugin OTA-intent state
// machine extracted from the inline logic in WebUIPlugin.cpp (CAR-178 release
// URL / CAR-377 OTA start), which previously had zero host test coverage
// because WebUIPlugin.cpp is not in the [env:native] build_src_filter.
//
// Covers:
//   - selectOtaComponents: cp=display / cp=controller / absent / empty
//   - postOtaDeferredIntent / postOtaDeferredIntentFlagOnly /
//     drainOtaDeferredIntent: single-in-flight, last-writer-wins coalescing,
//     clear-on-latch, and the contended flag-only fallback
//   - normalizeOtaChannel / resolveOtaReleaseUrl: beta / nightly /
//     tag:<semver> (allowed + not-allowed) / unknown -> latest fallback
//
// Header-only + free of any Arduino-String method (uses const char* /
// std::string), so it links on [env:native] via the existing `-I src` with
// the host String shim, mirroring the OtaChannelSwitchPolicy.h /
// BLEScaleScanPolicy.h precedent in this directory.

#include "../../src/display/plugins/OtaIntentState.h"
#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// selectOtaComponents: cp -> (updateController, updateDisplay)
// ---------------------------------------------------------------------------

void test_select_components_display_flashes_display_only(void) {
    const OtaComponentSelection sel = selectOtaComponents("display");
    TEST_ASSERT_FALSE(sel.updateController);
    TEST_ASSERT_TRUE(sel.updateDisplay);
}

void test_select_components_controller_flashes_controller_only(void) {
    const OtaComponentSelection sel = selectOtaComponents("controller");
    TEST_ASSERT_TRUE(sel.updateController);
    TEST_ASSERT_FALSE(sel.updateDisplay);
}

void test_select_components_absent_flashes_both(void) {
    const OtaComponentSelection sel = selectOtaComponents(nullptr);
    TEST_ASSERT_TRUE(sel.updateController);
    TEST_ASSERT_TRUE(sel.updateDisplay);
}

void test_select_components_empty_flashes_both(void) {
    const OtaComponentSelection sel = selectOtaComponents("");
    TEST_ASSERT_TRUE(sel.updateController);
    TEST_ASSERT_TRUE(sel.updateDisplay);
}

void test_select_components_unrecognized_string_flashes_both(void) {
    // Anything other than exactly "display" or "controller" falls through to
    // the full-update default, mirroring the inline `!= "display" && !=
    // "controller"` mapping (there is no third branch).
    const OtaComponentSelection sel = selectOtaComponents("bogus");
    TEST_ASSERT_TRUE(sel.updateController);
    TEST_ASSERT_TRUE(sel.updateDisplay);
}

// ---------------------------------------------------------------------------
// Deferred single-in-flight intent slot: post / flag-only post / drain
// ---------------------------------------------------------------------------

void test_post_deferred_intent_raises_flag_and_stores_payload(void) {
    const OtaDeferredStringIntent posted = postOtaDeferredIntent("controller");
    TEST_ASSERT_TRUE(posted.pending);
    TEST_ASSERT_EQUAL_STRING("controller", posted.payload.c_str());
}

// Two successful posts before a drain coalesce last-writer-wins: only the
// LATEST payload should ever reach the drain (there is no queue).
void test_post_deferred_intent_last_writer_wins(void) {
    const OtaDeferredStringIntent first = postOtaDeferredIntent("display");
    TEST_ASSERT_TRUE(first.pending);
    TEST_ASSERT_EQUAL_STRING("display", first.payload.c_str());

    // A second post before the drain latches supersedes the first entirely.
    const OtaDeferredStringIntent second = postOtaDeferredIntent("controller");
    TEST_ASSERT_TRUE(second.pending);
    TEST_ASSERT_EQUAL_STRING("controller", second.payload.c_str());
}

// Contended fallback: only the flag is raised; the payload comes back empty
// so the eventual drain falls through to the safe default (full update /
// re-resolve from settings), never carrying a stale or partially-written
// payload.
void test_post_deferred_intent_flag_only_leaves_payload_empty(void) {
    const OtaDeferredStringIntent posted = postOtaDeferredIntentFlagOnly();
    TEST_ASSERT_TRUE(posted.pending);
    TEST_ASSERT_TRUE(posted.payload.empty());
}

// Drain-and-clear: a pending intent yields its payload and the drain result
// mirrors what a freshly-cleared slot reads back as.
void test_drain_deferred_intent_returns_pending_payload(void) {
    const OtaDeferredDrainResult drained = drainOtaDeferredIntent(true, "controller");
    TEST_ASSERT_TRUE(drained.hadPending);
    TEST_ASSERT_EQUAL_STRING("controller", drained.payload.c_str());
}

// Draining a flag-only (contended) post: hadPending is true, but the payload
// is empty, which callers must treat as the safe default (full update).
void test_drain_deferred_intent_flag_only_payload_is_empty(void) {
    const OtaDeferredDrainResult drained = drainOtaDeferredIntent(true, "");
    TEST_ASSERT_TRUE(drained.hadPending);
    TEST_ASSERT_TRUE(drained.payload.empty());
}

// Draining an un-posted (never-latched) slot: no pending intent, and the
// payload is forced to empty regardless of whatever stale value was passed
// in, matching clear-on-latch (a stale payload never leaks into the next
// cycle even if a caller passed a non-empty string here by mistake).
void test_drain_deferred_intent_no_pending_forces_empty_payload(void) {
    const OtaDeferredDrainResult drained = drainOtaDeferredIntent(false, "stale-should-be-ignored");
    TEST_ASSERT_FALSE(drained.hadPending);
    TEST_ASSERT_TRUE(drained.payload.empty());
}

// End-to-end: post then drain round-trips the payload exactly.
void test_post_then_drain_round_trips_payload(void) {
    const OtaDeferredStringIntent posted = postOtaDeferredIntent("display");
    const OtaDeferredDrainResult drained = drainOtaDeferredIntent(posted.pending, posted.payload);
    TEST_ASSERT_TRUE(drained.hadPending);
    TEST_ASSERT_EQUAL_STRING("display", drained.payload.c_str());
}

// ---------------------------------------------------------------------------
// normalizeOtaChannel / resolveOtaReleaseUrl
// ---------------------------------------------------------------------------

static const char *const kStableVersions[] = {"2.0.15", "2.0.14", "2.0.13"};
static const size_t kStableVersionsCount = sizeof(kStableVersions) / sizeof(kStableVersions[0]);
static const std::string kReleaseUrlBase = "https://github.com/carloshrdezc/gaggimate/releases/";

void test_normalize_channel_beta_and_nightly_pass_through(void) {
    TEST_ASSERT_EQUAL_STRING("beta", normalizeOtaChannel("beta", kStableVersions, kStableVersionsCount).c_str());
    TEST_ASSERT_EQUAL_STRING("nightly", normalizeOtaChannel("nightly", kStableVersions, kStableVersionsCount).c_str());
}

void test_normalize_channel_allowed_tag_passes_through(void) {
    TEST_ASSERT_EQUAL_STRING("tag:2.0.14", normalizeOtaChannel("tag:2.0.14", kStableVersions, kStableVersionsCount).c_str());
}

void test_normalize_channel_disallowed_tag_falls_back_to_latest(void) {
    TEST_ASSERT_EQUAL_STRING("latest", normalizeOtaChannel("tag:9.9.9", kStableVersions, kStableVersionsCount).c_str());
}

void test_normalize_channel_unknown_value_falls_back_to_latest(void) {
    TEST_ASSERT_EQUAL_STRING("latest", normalizeOtaChannel("bogus", kStableVersions, kStableVersionsCount).c_str());
    TEST_ASSERT_EQUAL_STRING("latest", normalizeOtaChannel("", kStableVersions, kStableVersionsCount).c_str());
}

void test_resolve_release_url_beta_and_nightly(void) {
    TEST_ASSERT_EQUAL_STRING((kReleaseUrlBase + "tag/beta").c_str(),
                             resolveOtaReleaseUrl("beta", kReleaseUrlBase, kStableVersions, kStableVersionsCount).c_str());
    TEST_ASSERT_EQUAL_STRING((kReleaseUrlBase + "tag/nightly").c_str(),
                             resolveOtaReleaseUrl("nightly", kReleaseUrlBase, kStableVersions, kStableVersionsCount).c_str());
}

void test_resolve_release_url_allowed_tag(void) {
    TEST_ASSERT_EQUAL_STRING((kReleaseUrlBase + "tag/2.0.14").c_str(),
                             resolveOtaReleaseUrl("tag:2.0.14", kReleaseUrlBase, kStableVersions, kStableVersionsCount).c_str());
}

void test_resolve_release_url_disallowed_tag_falls_back_to_latest(void) {
    TEST_ASSERT_EQUAL_STRING((kReleaseUrlBase + "latest").c_str(),
                             resolveOtaReleaseUrl("tag:9.9.9", kReleaseUrlBase, kStableVersions, kStableVersionsCount).c_str());
}

void test_resolve_release_url_unknown_channel_falls_back_to_latest(void) {
    TEST_ASSERT_EQUAL_STRING((kReleaseUrlBase + "latest").c_str(),
                             resolveOtaReleaseUrl("bogus", kReleaseUrlBase, kStableVersions, kStableVersionsCount).c_str());
    TEST_ASSERT_EQUAL_STRING((kReleaseUrlBase + "latest").c_str(),
                             resolveOtaReleaseUrl("", kReleaseUrlBase, kStableVersions, kStableVersionsCount).c_str());
}

static int runOtaIntentStateTests() {
    UNITY_BEGIN();
    RUN_TEST(test_select_components_display_flashes_display_only);
    RUN_TEST(test_select_components_controller_flashes_controller_only);
    RUN_TEST(test_select_components_absent_flashes_both);
    RUN_TEST(test_select_components_empty_flashes_both);
    RUN_TEST(test_select_components_unrecognized_string_flashes_both);
    RUN_TEST(test_post_deferred_intent_raises_flag_and_stores_payload);
    RUN_TEST(test_post_deferred_intent_last_writer_wins);
    RUN_TEST(test_post_deferred_intent_flag_only_leaves_payload_empty);
    RUN_TEST(test_drain_deferred_intent_returns_pending_payload);
    RUN_TEST(test_drain_deferred_intent_flag_only_payload_is_empty);
    RUN_TEST(test_drain_deferred_intent_no_pending_forces_empty_payload);
    RUN_TEST(test_post_then_drain_round_trips_payload);
    RUN_TEST(test_normalize_channel_beta_and_nightly_pass_through);
    RUN_TEST(test_normalize_channel_allowed_tag_passes_through);
    RUN_TEST(test_normalize_channel_disallowed_tag_falls_back_to_latest);
    RUN_TEST(test_normalize_channel_unknown_value_falls_back_to_latest);
    RUN_TEST(test_resolve_release_url_beta_and_nightly);
    RUN_TEST(test_resolve_release_url_allowed_tag);
    RUN_TEST(test_resolve_release_url_disallowed_tag_falls_back_to_latest);
    RUN_TEST(test_resolve_release_url_unknown_channel_falls_back_to_latest);
    return UNITY_END();
}

#ifdef ARDUINO
void setup() { runOtaIntentStateTests(); }

void loop() {}
#else
int main(int argc, char **argv) { return runOtaIntentStateTests(); }
#endif
