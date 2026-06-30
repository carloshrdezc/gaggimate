#include "display/core/SslRelayStartupPolicy.h"
#include <unity.h>

// PRO-347 (PRO-346 audit finding F1): the SSL cloud-relay startup gate used to
// read the COMBINED internal+PSRAM heap (esp_get_free_heap_size() < 60000). On
// the ESP32-S3 that figure is PSRAM-dominated (multi-MB) and almost always
// passed even when the small INTERNAL DMA-capable DRAM pool was exhausted. The
// beginSSL() mbedTLS handshake draws ~50 KB from that internal pool, so under
// HomeKit + BLE + WiFi + mDNS it starved and failed with
// `SSL - Memory allocation failed (-32512)` while the combined pool still
// looked healthy. The fix gates on the largest contiguous internal-DRAM block
// (gmInternalLargestBlock()) against a floor, mirroring the PRO-334 OTA-TLS
// precedent.
//
// These tests pin the pure decision (floor + predicate) the firmware relies on,
// with no Arduino / heap_caps deps so they link and run in [env:native]
// (mirrors test_sd_read_retry_policy).

void setUp(void) {}
void tearDown(void) {}

// Plenty of internal DRAM: the SSL relay is admitted. The floor is the
// meaningful "can the ~50 KB mbedTLS handshake fit contiguously" signal, so well
// above it must pass.
void test_admit_when_well_above_floor(void) {
    TEST_ASSERT_TRUE(sslRelayDramSufficient(kSslRelayInternalDramFloorBytes * 4, kSslRelayInternalDramFloorBytes));
}

// Exactly at the floor: admit (>= boundary acts), so a device sitting right on
// the threshold still brings the relay up rather than needlessly refusing.
void test_admit_at_exactly_floor(void) {
    TEST_ASSERT_TRUE(sslRelayDramSufficient(kSslRelayInternalDramFloorBytes, kSslRelayInternalDramFloorBytes));
}

// One byte below the floor: refuse. This is the fail-safe — under internal-DRAM
// pressure we skip the SSL relay instead of driving a handshake that would
// -32512 and keep pressure on the pool lwIP/mDNS/async-web-server need.
void test_refuse_just_below_floor(void) {
    TEST_ASSERT_FALSE(sslRelayDramSufficient(kSslRelayInternalDramFloorBytes - 1, kSslRelayInternalDramFloorBytes));
}

// Zero largest block (internal pool fully fragmented/exhausted — the captured
// failure state): always refuse.
void test_refuse_when_internal_exhausted(void) {
    TEST_ASSERT_FALSE(sslRelayDramSufficient(0, kSslRelayInternalDramFloorBytes));
}

// The SSL relay floor must be a defensible size: above the ~50 KB the mbedTLS
// handshake draws, and strictly higher than the OTA check's 48 KB floor (the
// SSL relay draw is larger, so the original 60000-byte combined intent and the
// ~50 KB handshake both argue for a higher INTERNAL-DRAM floor than OTA's).
void test_floor_is_above_ssl_handshake_draw(void) {
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(static_cast<unsigned long>(50 * 1024),
                                        static_cast<unsigned long>(kSslRelayInternalDramFloorBytes));
    TEST_ASSERT_GREATER_THAN_UINT32(static_cast<unsigned long>(48 * 1024),
                                    static_cast<unsigned long>(kSslRelayInternalDramFloorBytes));
}

// The predicate honors an arbitrary floor argument (it is not hard-wired to the
// constant), so the runtime caller can pass the measured block and the floor
// independently — the boundary is purely largestInternalBlock >= floor.
void test_predicate_is_pure_comparison(void) {
    TEST_ASSERT_TRUE(sslRelayDramSufficient(100, 100));
    TEST_ASSERT_TRUE(sslRelayDramSufficient(101, 100));
    TEST_ASSERT_FALSE(sslRelayDramSufficient(99, 100));
}

static int runSslRelayStartupPolicyTests() {
    UNITY_BEGIN();
    RUN_TEST(test_admit_when_well_above_floor);
    RUN_TEST(test_admit_at_exactly_floor);
    RUN_TEST(test_refuse_just_below_floor);
    RUN_TEST(test_refuse_when_internal_exhausted);
    RUN_TEST(test_floor_is_above_ssl_handshake_draw);
    RUN_TEST(test_predicate_is_pure_comparison);
    return UNITY_END();
}

#ifdef ARDUINO
void setup() { runSslRelayStartupPolicyTests(); }

void loop() {}
#else
int main(int argc, char **argv) { return runSslRelayStartupPolicyTests(); }
#endif
