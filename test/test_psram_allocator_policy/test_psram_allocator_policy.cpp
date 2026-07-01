#include "display/plugins/PsramAllocator.h"
#include <unity.h>

// PRO-358: reclaim internal DMA-capable DRAM by routing large NON-DMA buffers
// (the WebSocket reassembly buffer in WebUIPlugin) to external PSRAM. The
// PsramAllocator itself needs a heap_caps allocator (device only), but the
// SIZING DECISION — is an allocation large enough that PSRAM routing is
// worthwhile — is a pure constexpr with no hardware dependency, so it links and
// runs in [env:native] (mirrors test_ota_check_policy / the OtaCheckPolicy.h
// pure-header pattern).
//
// These tests pin the threshold contract the device allocator relies on: small
// allocations stay on the default heap (below the threshold -> false), buffers
// at or above the threshold are routed to PSRAM (true), and the boundary is
// exact. If the threshold constant is ever retuned, these assertions document
// the intended small-vs-large split.

void setUp(void) {}
void tearDown(void) {}

// Below the threshold: not worth a PSRAM round trip.
static void test_small_allocation_not_offloaded(void) {
    TEST_ASSERT_FALSE(psramOffloadWorthwhile(0));
    TEST_ASSERT_FALSE(psramOffloadWorthwhile(1));
    TEST_ASSERT_FALSE(psramOffloadWorthwhile(64));
    TEST_ASSERT_FALSE(psramOffloadWorthwhile(kPsramOffloadMinBytes - 1));
}

// Exactly at the threshold: offload (>= is the contract).
static void test_at_threshold_is_offloaded(void) { TEST_ASSERT_TRUE(psramOffloadWorthwhile(kPsramOffloadMinBytes)); }

// Above the threshold: offload. The real target (multi-KB JSON control-message
// reassembly, up to the 64 KiB reserve / 256 KiB reassembly cap) is far above.
static void test_large_allocation_offloaded(void) {
    TEST_ASSERT_TRUE(psramOffloadWorthwhile(kPsramOffloadMinBytes + 1));
    TEST_ASSERT_TRUE(psramOffloadWorthwhile(4 * 1024));
    TEST_ASSERT_TRUE(psramOffloadWorthwhile(64 * 1024));
    TEST_ASSERT_TRUE(psramOffloadWorthwhile(256 * 1024));
}

// The threshold is a small, sane value: well under a KB (so multi-KB WS messages
// always offload) yet above a trivial handful of bytes (so tiny control-block
// allocations are not forced through slower PSRAM for no headroom benefit).
static void test_threshold_is_sane(void) {
    TEST_ASSERT_TRUE(kPsramOffloadMinBytes >= 64);
    TEST_ASSERT_TRUE(kPsramOffloadMinBytes <= 4096);
}

// The decision is usable in a constant-expression context (constexpr), matching
// the pure-header contract of the other policy headers.
static void test_decision_is_constexpr(void) {
    static_assert(!psramOffloadWorthwhile(0), "zero-byte allocation must not offload");
    static_assert(psramOffloadWorthwhile(kPsramOffloadMinBytes), "at-threshold allocation must offload");
    TEST_PASS();
}

static int runPsramAllocatorPolicyTests() {
    UNITY_BEGIN();
    RUN_TEST(test_small_allocation_not_offloaded);
    RUN_TEST(test_at_threshold_is_offloaded);
    RUN_TEST(test_large_allocation_offloaded);
    RUN_TEST(test_threshold_is_sane);
    RUN_TEST(test_decision_is_constexpr);
    return UNITY_END();
}

#ifdef ARDUINO
void setup() { runPsramAllocatorPolicyTests(); }

void loop() {}
#else
int main(int argc, char **argv) { return runPsramAllocatorPolicyTests(); }
#endif
