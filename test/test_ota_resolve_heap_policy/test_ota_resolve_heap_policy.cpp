// PRO-554: pure, host-testable coverage for the WebUIPlugin OTA resolve
// pre-flight internal-DRAM guard (OtaResolveHeapPolicy.h).
//
// Background: WebUIPlugin::otaResolveTask (PRO-13) opens a fresh, independent
// HTTPS/TLS connection to GitHub via GitHubOTA::checkForUpdates() to resolve a
// forced-tag / channel-switch head. Under current internal-DRAM pressure that
// second TLS handshake can OOM deep in mbedtls's certificate-verify path and
// PANIC (LoadProhibited) instead of failing cleanly. The pre-flight guard reads
// heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) and, when it is below a
// floor, skips the TLS attempt so the resolve fails gracefully instead of
// crashing.
//
// This suite pins the floor value and the comparison boundary. Header-only +
// free of any Arduino/FreeRTOS/heap_caps dependency (the on-device caller reads
// the block size and passes it in), so it links on [env:native] via the
// existing `-I src`, mirroring the OtaAsyncResolvePolicy.h precedent.

#include "../../src/display/plugins/OtaResolveHeapPolicy.h"
#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// Floor constant
// ---------------------------------------------------------------------------

void test_floor_is_48_kib(void) { TEST_ASSERT_EQUAL_UINT32(49152u, kOtaResolveInternalDramFloorBytes); }

// ---------------------------------------------------------------------------
// otaResolveHeapSufficient — default (module) floor
// ---------------------------------------------------------------------------

// Inclusive boundary: exactly the floor free counts as sufficient.
void test_sufficient_exactly_at_floor(void) { TEST_ASSERT_TRUE(otaResolveHeapSufficient(49152u)); }

void test_sufficient_one_byte_above_floor(void) { TEST_ASSERT_TRUE(otaResolveHeapSufficient(49153u)); }

void test_sufficient_large_free_block(void) { TEST_ASSERT_TRUE(otaResolveHeapSufficient(1024u * 1024u)); }

// One byte below the floor: skip the TLS attempt, fail closed.
void test_insufficient_one_byte_below_floor(void) { TEST_ASSERT_FALSE(otaResolveHeapSufficient(49151u)); }

void test_insufficient_zero_free(void) { TEST_ASSERT_FALSE(otaResolveHeapSufficient(0u)); }

// A fragmented internal heap: plenty of total free but no single 48 KiB block.
// This is exactly the case the guard exists for — mbedtls needs a large
// contiguous allocation for the TLS record + cert-verify scratch.
void test_insufficient_fragmented_16_kib_block(void) { TEST_ASSERT_FALSE(otaResolveHeapSufficient(16u * 1024u)); }

// ---------------------------------------------------------------------------
// otaResolveHeapSufficient — explicit floor overload
// ---------------------------------------------------------------------------

void test_explicit_floor_value_equal_is_sufficient(void) { TEST_ASSERT_TRUE(otaResolveHeapSufficient(100u, 100u)); }

void test_explicit_floor_value_below_is_insufficient(void) { TEST_ASSERT_FALSE(otaResolveHeapSufficient(99u, 100u)); }

void test_explicit_floor_value_above_is_sufficient(void) { TEST_ASSERT_TRUE(otaResolveHeapSufficient(101u, 100u)); }

static int runOtaResolveHeapPolicyTests() {
    UNITY_BEGIN();
    RUN_TEST(test_floor_is_48_kib);
    RUN_TEST(test_sufficient_exactly_at_floor);
    RUN_TEST(test_sufficient_one_byte_above_floor);
    RUN_TEST(test_sufficient_large_free_block);
    RUN_TEST(test_insufficient_one_byte_below_floor);
    RUN_TEST(test_insufficient_zero_free);
    RUN_TEST(test_insufficient_fragmented_16_kib_block);
    RUN_TEST(test_explicit_floor_value_equal_is_sufficient);
    RUN_TEST(test_explicit_floor_value_below_is_insufficient);
    RUN_TEST(test_explicit_floor_value_above_is_sufficient);
    return UNITY_END();
}

#ifdef ARDUINO
void setup() { runOtaResolveHeapPolicyTests(); }

void loop() {}
#else
int main(int argc, char **argv) { return runOtaResolveHeapPolicyTests(); }
#endif
