#include "display/plugins/UdpLogTeePolicy.h"
#include <unity.h>

// PRO-334: internal DMA-capable DRAM exhaustion on the display (ESP32-S3,
// Arduino-esp32 3.x / IDF 5.x) under HomeKit + BLE + WiFi + mDNS. The Diagnostic
// UDP log tee broadcasts EVERY INFO+ log line to 255.255.255.255:9999; under
// internal-DRAM pressure the per-line sendto() returns ENOMEM (errno 12), and
// the framework's own "could not send data" log re-enters the tee -> a
// self-amplifying log storm that helps starve lwIP/mDNS and the async web
// server, leaving the web UI unreachable while WiFi still shows "connected".
//
// The fix routes the drain-task send through two pure decisions in
// UdpLogTeePolicy.h (no Arduino/WiFi/FreeRTOS deps, so they link and run in
// [env:native], mirroring SdReadRetryPolicy.h / WiFiFallbackPolicy.h):
//   1. shouldSendUdpLogLine(largestFreeInternalBlock) - drop-when-no-buffer: the
//      drain task only attempts the broadcast when internal DRAM clears a floor;
//      below it the line is DROPPED so the tee yields the internal pool to
//      lwIP/mDNS/HTTP (the reserved-internal-DRAM-floor the issue asks for).
//   2. shouldLogUdpSendFailure(consecutiveFailures) - hard rate-limit on the
//      send-failure diagnostic so the log-storm feedback loop can never form.
//
// These tests pin the decision contract the firmware relies on to guarantee the
// UDP tee can NEVER spiral the internal pool into web-UI-unreachable.

void setUp(void) {}
void tearDown(void) {}

// --- 1. drop-when-no-buffer gate (shouldSendUdpLogLine) -----------------------

// Plenty of internal DRAM: send is attempted (normal operation, tee broadcasts).
void test_send_when_well_above_floor(void) {
    TEST_ASSERT_TRUE(shouldSendUdpLogLine(kUdpLogTeeInternalDramFloorBytes * 16));
}

// Exactly at the floor the send is attempted (>= boundary acts) so a device
// sitting right on the threshold still streams logs rather than going dark.
void test_send_at_exactly_floor(void) { TEST_ASSERT_TRUE(shouldSendUdpLogLine(kUdpLogTeeInternalDramFloorBytes)); }

// One byte below the floor: DROP the line. This is the fail-safe — under
// internal-DRAM pressure we stop poking sendto() (which would ENOMEM-spiral and
// starve lwIP/mDNS/HTTP) and simply drop the best-effort debug line.
void test_drop_just_below_floor(void) { TEST_ASSERT_FALSE(shouldSendUdpLogLine(kUdpLogTeeInternalDramFloorBytes - 1)); }

// Internal pool fully fragmented/exhausted (the captured failure state): always
// drop. This is exactly the condition that made the web UI unreachable.
void test_drop_when_internal_exhausted(void) { TEST_ASSERT_FALSE(shouldSendUdpLogLine(0)); }

// The UDP tee floor must be well BELOW the WebUI OTA-TLS floor: a UDP datagram
// needs far less contiguous internal memory than an mbedTLS handshake, so the
// tee should keep streaming after the heavier TLS check has already deferred.
// (Pinned as a plain bound rather than cross-including WebUIPlugin.h here.)
void test_udp_floor_is_modest(void) {
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(static_cast<unsigned long>(1024),
                                        static_cast<unsigned long>(kUdpLogTeeInternalDramFloorBytes));
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(static_cast<unsigned long>(32 * 1024),
                                     static_cast<unsigned long>(kUdpLogTeeInternalDramFloorBytes));
}

// --- 2. send-failure log rate-limit (shouldLogUdpSendFailure) -----------------

// A zero count is "no failure" — never logs.
void test_no_log_when_no_failure(void) { TEST_ASSERT_FALSE(shouldLogUdpSendFailure(0)); }

// The FIRST failure is always surfaced so the ENOMEM condition is observable on
// serial / the SD sink even though subsequent failures are suppressed.
void test_log_first_failure(void) { TEST_ASSERT_TRUE(shouldLogUdpSendFailure(1)); }

// Failures 2..(period-1) are SUPPRESSED — this is the whole point: a sustained
// failure must not log per dropped packet (each log line re-enters the tee and
// would self-amplify). Spot-check several in the suppressed band.
void test_suppress_failures_between_first_and_period(void) {
    TEST_ASSERT_FALSE(shouldLogUdpSendFailure(2));
    TEST_ASSERT_FALSE(shouldLogUdpSendFailure(3));
    TEST_ASSERT_FALSE(shouldLogUdpSendFailure(kUdpSendFailureLogEvery - 1));
    TEST_ASSERT_FALSE(shouldLogUdpSendFailure(kUdpSendFailureLogEvery + 1));
}

// Exactly one line per period thereafter (a heartbeat that the condition
// persists, without storming).
void test_log_once_per_period(void) {
    TEST_ASSERT_TRUE(shouldLogUdpSendFailure(kUdpSendFailureLogEvery));
    TEST_ASSERT_TRUE(shouldLogUdpSendFailure(kUdpSendFailureLogEvery * 2));
    TEST_ASSERT_TRUE(shouldLogUdpSendFailure(kUdpSendFailureLogEvery * 3));
}

// The KEY anti-storm invariant: across a long run of CONSECUTIVE failures the
// number of emitted log lines stays tiny relative to the failure count. If a
// future tweak made this log per-failure (or per-handful), this assertion fails.
void test_failure_log_rate_is_sparse(void) {
    const unsigned long N = 100000; // simulate 100k consecutive failed sends
    unsigned long logged = 0;
    for (unsigned long i = 1; i <= N; ++i) {
        if (shouldLogUdpSendFailure(i)) {
            ++logged;
        }
    }
    // first failure + one per period => 1 + floor(N/period). For N=100000 and a
    // 256-line period that is 1 + 390 = 391 lines for 100k failures (<0.4%).
    const unsigned long expected = 1 + (N / kUdpSendFailureLogEvery);
    TEST_ASSERT_EQUAL_UINT32(expected, logged);
    // Hard ceiling: well under 1% of the failures ever produce a log line, so
    // the tee can never be the source of a log storm under sustained ENOMEM.
    TEST_ASSERT_LESS_THAN_UINT32(N / 100, logged);
}

// The rate-limit period is bounded and sane: large enough to be sparse, but not
// so large the heartbeat effectively never fires.
void test_failure_log_period_is_sane(void) {
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(static_cast<unsigned long>(16), kUdpSendFailureLogEvery);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(static_cast<unsigned long>(4096), kUdpSendFailureLogEvery);
}

static int runUdpLogTeePolicyTests() {
    UNITY_BEGIN();
    RUN_TEST(test_send_when_well_above_floor);
    RUN_TEST(test_send_at_exactly_floor);
    RUN_TEST(test_drop_just_below_floor);
    RUN_TEST(test_drop_when_internal_exhausted);
    RUN_TEST(test_udp_floor_is_modest);
    RUN_TEST(test_no_log_when_no_failure);
    RUN_TEST(test_log_first_failure);
    RUN_TEST(test_suppress_failures_between_first_and_period);
    RUN_TEST(test_log_once_per_period);
    RUN_TEST(test_failure_log_rate_is_sparse);
    RUN_TEST(test_failure_log_period_is_sane);
    return UNITY_END();
}

#ifdef ARDUINO
void setup() { runUdpLogTeePolicyTests(); }

void loop() {}
#else
int main(int argc, char **argv) { return runUdpLogTeePolicyTests(); }
#endif
