// PRO-411: host tests for the pure OTA update-check guards + backoff math.
//
// The String-typed HTTPS helpers (get_redirect_location /
// get_updated_version_via_txt_file / get_updated_base_url_via_redirect in
// lib/OTA/src/common.cpp) are too coupled to WiFiClientSecure to unit-test on
// the host, so the crash-relevant DECISION logic is extracted into
// src/display/plugins/OtaUpdateCheckPolicy.h and tested here against empty /
// null / malformed inputs — the exact shapes that produced the on-device
// IllegalInstruction crash (empty/NULL String flowing into the WiFi/TLS
// connect path) and that must now be rejected before any WiFi/TLS call.
//
// Header-only + const char* based, so it links on [env:native] via `-I src`
// with the host String shim. No platformio.ini change is required.

#include "../../src/display/plugins/OtaUpdateCheckPolicy.h"
#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

// A null or empty redirect Location is the crash vector — it must be rejected
// so the caller bails before deriving a base_url / calling any WiFi/TLS API.
void test_redirect_location_rejects_null_and_empty(void) {
    TEST_ASSERT_FALSE(otaRedirectLocationValid(nullptr));
    TEST_ASSERT_FALSE(otaRedirectLocationValid(""));
    TEST_ASSERT_TRUE(otaRedirectLocationValid("https://github.com/carloshrdezc/gaggimate/releases/tag/2.0.0"));
    // A single non-null char is enough to be "present"; content validity is the
    // downstream parser's job, not this guard's.
    TEST_ASSERT_TRUE(otaRedirectLocationValid("x"));
}

// A null / empty version string means "resolve produced nothing" and must be
// treated as a failed check, never parsed into a semver_t.
void test_version_rejects_null_and_empty(void) {
    TEST_ASSERT_FALSE(otaVersionValid(nullptr));
    TEST_ASSERT_FALSE(otaVersionValid(""));
    TEST_ASSERT_TRUE(otaVersionValid("2.0.14"));
    TEST_ASSERT_TRUE(otaVersionValid("v2.0.14"));
}

// Leading v/V detection mirrors GitHubOTA::checkForUpdates version handling.
void test_version_leading_v_detection(void) {
    TEST_ASSERT_FALSE(otaVersionHasLeadingV(nullptr));
    TEST_ASSERT_FALSE(otaVersionHasLeadingV(""));
    TEST_ASSERT_TRUE(otaVersionHasLeadingV("v2.0.0"));
    TEST_ASSERT_TRUE(otaVersionHasLeadingV("V2.0.0"));
    TEST_ASSERT_FALSE(otaVersionHasLeadingV("2.0.0"));
    // A bare "v" is a prefix (downstream substring(1) yields an empty string,
    // which otaVersionValid then rejects — the two guards compose).
    TEST_ASSERT_TRUE(otaVersionHasLeadingV("v"));
}

// Backoff: 0 failures returns the base interval unchanged.
void test_backoff_zero_failures_is_base(void) {
    TEST_ASSERT_EQUAL_UINT32(300000u, otaBackoffInterval(300000u, 3600000u, 0u));
}

// Backoff grows exponentially (x2 per failure): first failure keeps base
// (2^0), then doubles each subsequent consecutive failure.
void test_backoff_grows_exponentially(void) {
    TEST_ASSERT_EQUAL_UINT32(300000u, otaBackoffInterval(300000u, 3600000u, 1u));  // 2^0
    TEST_ASSERT_EQUAL_UINT32(600000u, otaBackoffInterval(300000u, 3600000u, 2u));  // 2^1
    TEST_ASSERT_EQUAL_UINT32(1200000u, otaBackoffInterval(300000u, 3600000u, 3u)); // 2^2
    TEST_ASSERT_EQUAL_UINT32(2400000u, otaBackoffInterval(300000u, 3600000u, 4u)); // 2^3
}

// Backoff is capped at maxInterval and never exceeds it, no matter how many
// consecutive failures accumulate (incl. UINT32_MAX — no shift overflow).
void test_backoff_caps_at_max(void) {
    TEST_ASSERT_EQUAL_UINT32(3600000u, otaBackoffInterval(300000u, 3600000u, 5u));
    TEST_ASSERT_EQUAL_UINT32(3600000u, otaBackoffInterval(300000u, 3600000u, 6u));
    TEST_ASSERT_EQUAL_UINT32(3600000u, otaBackoffInterval(300000u, 3600000u, 100u));
    TEST_ASSERT_EQUAL_UINT32(3600000u, otaBackoffInterval(300000u, 3600000u, 0xFFFFFFFFu));
}

// Degenerate config where base >= max always returns the cap.
void test_backoff_base_ge_max_returns_cap(void) {
    TEST_ASSERT_EQUAL_UINT32(500u, otaBackoffInterval(500u, 500u, 0u));
    TEST_ASSERT_EQUAL_UINT32(500u, otaBackoffInterval(500u, 500u, 10u));
    TEST_ASSERT_EQUAL_UINT32(500u, otaBackoffInterval(1000u, 500u, 3u));
}

// PRO-555: the periodic background OTA check defers (skips this cycle) when the
// largest contiguous free internal-DRAM block is below the mbedtls TLS floor,
// mirroring the resolve path's floor (OtaResolveHeapPolicy.h) but with the
// opposite response — defer/retry rather than fail closed. Sufficient heap ->
// do NOT defer (open the TLS connection this cycle); insufficient heap -> defer.
void test_periodic_check_defers_iff_heap_insufficient(void) {
    // Sufficient: at or above the shared 48 KiB floor -> proceed (do not defer).
    TEST_ASSERT_FALSE(otaPeriodicCheckShouldDefer(kOtaResolveInternalDramFloorBytes)); // inclusive boundary
    TEST_ASSERT_FALSE(otaPeriodicCheckShouldDefer(49153u));
    TEST_ASSERT_FALSE(otaPeriodicCheckShouldDefer(1024u * 1024u));
    // Insufficient: below the floor (incl. a fragmented internal heap with no
    // single large contiguous block) -> defer this cycle, retry next loop.
    TEST_ASSERT_TRUE(otaPeriodicCheckShouldDefer(49151u));
    TEST_ASSERT_TRUE(otaPeriodicCheckShouldDefer(16u * 1024u));
    TEST_ASSERT_TRUE(otaPeriodicCheckShouldDefer(0u));
}

// PRO-555: the defer guard is single-sourced against the resolve path's floor —
// it is exactly the inverse of otaResolveHeapSufficient, so the periodic-check
// and resolve paths can never drift onto different floors.
void test_periodic_check_defer_is_inverse_of_resolve_sufficient(void) {
    const size_t samples[] = {0u, 16u * 1024u, 49151u, 49152u, 49153u, 1024u * 1024u};
    for (size_t s : samples) {
        TEST_ASSERT_EQUAL(!otaResolveHeapSufficient(s), otaPeriodicCheckShouldDefer(s));
    }
}

static int runOtaUpdateCheckPolicyTests() {
    UNITY_BEGIN();
    RUN_TEST(test_redirect_location_rejects_null_and_empty);
    RUN_TEST(test_version_rejects_null_and_empty);
    RUN_TEST(test_version_leading_v_detection);
    RUN_TEST(test_backoff_zero_failures_is_base);
    RUN_TEST(test_backoff_grows_exponentially);
    RUN_TEST(test_backoff_caps_at_max);
    RUN_TEST(test_backoff_base_ge_max_returns_cap);
    RUN_TEST(test_periodic_check_defers_iff_heap_insufficient);
    RUN_TEST(test_periodic_check_defer_is_inverse_of_resolve_sufficient);
    return UNITY_END();
}

#ifdef ARDUINO
void setup() { runOtaUpdateCheckPolicyTests(); }

void loop() {}
#else
int main(int argc, char **argv) { return runOtaUpdateCheckPolicyTests(); }
#endif
