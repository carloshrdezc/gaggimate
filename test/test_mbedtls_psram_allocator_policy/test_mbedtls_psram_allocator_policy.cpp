// PRO-569 (Ref PRO-566): pure, host-testable coverage for the mbedTLS PSRAM
// allocator's size/overflow policy (MbedtlsPsramAllocatorPolicy.h).
//
// Background: on classic espressif32 (IDF 4.4.7) mbedtls_ssl_setup() allocates
// two ~16.3 KiB record buffers for the OTA-check TLS handshake from internal
// DRAM (CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC=y baked into the prebuilt lib). The
// PRO-569 fix installs a PSRAM-backed calloc/free via mbedtls_platform_set_calloc_free
// at boot. The on-device wrapper delegates its (n, size) size / zero-request /
// overflow decision to this pure policy so heap_caps_calloc(MALLOC_CAP_SPIRAM)
// is only reached for real, non-overflowing allocations. This suite pins that
// contract. Header-only + free of any Arduino/heap_caps dependency, so it links
// on [env:native] via `-I src`, mirroring the OtaResolveHeapPolicy.h precedent.

#include "../../src/display/core/MbedtlsPsramAllocatorPolicy.h"
#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// Normal allocations — the OTA-check TLS record buffers
// ---------------------------------------------------------------------------

// The exact per-buffer size mbedtls_ssl_setup() requests on this platform
// (MBEDTLS_SSL_{IN,OUT}_BUFFER_LEN = 13 header + 304 overhead + 16384 content).
void test_record_buffer_size_is_exact(void) {
    MbedtlsCallocSize s = mbedtlsCallocSize(1, 16701u);
    TEST_ASSERT_EQUAL_UINT32(16701u, (uint32_t)s.bytes);
    TEST_ASSERT_FALSE(s.zero);
    TEST_ASSERT_FALSE(s.overflow);
    TEST_ASSERT_TRUE(mbedtlsCallocShouldAllocate(1, 16701u));
}

// n*size is order-independent (mbedtls calls calloc(1, len) and calloc(n, 1)).
void test_size_is_commutative(void) {
    TEST_ASSERT_EQUAL_UINT32(16701u, (uint32_t)mbedtlsCallocSize(16701u, 1).bytes);
    TEST_ASSERT_EQUAL_UINT32(32u, (uint32_t)mbedtlsCallocSize(4u, 8u).bytes);
}

void test_small_struct_alloc(void) {
    TEST_ASSERT_EQUAL_UINT32(256u, (uint32_t)mbedtlsCallocSize(1, 256u).bytes);
    TEST_ASSERT_TRUE(mbedtlsCallocShouldAllocate(1, 256u));
}

// ---------------------------------------------------------------------------
// Zero request -> return NULL without touching the heap (mbedtls_calloc contract)
// ---------------------------------------------------------------------------

void test_zero_count_is_zero_request(void) {
    TEST_ASSERT_TRUE(mbedtlsCallocSize(0, 16u).zero);
    TEST_ASSERT_FALSE(mbedtlsCallocShouldAllocate(0, 16u));
}

void test_zero_size_is_zero_request(void) {
    TEST_ASSERT_TRUE(mbedtlsCallocSize(16u, 0).zero);
    TEST_ASSERT_FALSE(mbedtlsCallocShouldAllocate(16u, 0));
}

// ---------------------------------------------------------------------------
// Overflow -> refuse (return NULL); never under-allocate
// ---------------------------------------------------------------------------

void test_multiply_overflow_is_refused(void) {
    MbedtlsCallocSize s = mbedtlsCallocSize(SIZE_MAX, 2u);
    TEST_ASSERT_TRUE(s.overflow);
    TEST_ASSERT_FALSE(mbedtlsCallocShouldAllocate(SIZE_MAX, 2u));
}

void test_max_times_one_is_exact_no_overflow(void) {
    MbedtlsCallocSize s = mbedtlsCallocSize(SIZE_MAX, 1u);
    TEST_ASSERT_FALSE(s.overflow);
    TEST_ASSERT_EQUAL(SIZE_MAX, s.bytes);
    TEST_ASSERT_TRUE(mbedtlsCallocShouldAllocate(SIZE_MAX, 1u));
}

static int runMbedtlsPsramAllocatorPolicyTests() {
    UNITY_BEGIN();
    RUN_TEST(test_record_buffer_size_is_exact);
    RUN_TEST(test_size_is_commutative);
    RUN_TEST(test_small_struct_alloc);
    RUN_TEST(test_zero_count_is_zero_request);
    RUN_TEST(test_zero_size_is_zero_request);
    RUN_TEST(test_multiply_overflow_is_refused);
    RUN_TEST(test_max_times_one_is_exact_no_overflow);
    return UNITY_END();
}

#ifdef ARDUINO
void setup() { runMbedtlsPsramAllocatorPolicyTests(); }

void loop() {}
#else
int main(int argc, char **argv) { return runMbedtlsPsramAllocatorPolicyTests(); }
#endif
