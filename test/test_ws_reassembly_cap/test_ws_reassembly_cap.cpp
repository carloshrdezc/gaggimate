#include "display/plugins/WsReassemblyPolicy.h"
#include <unity.h>

// PRO-350: WebUIPlugin reassembles a fragmented WebSocket message into a
// per-client std::string (rxBuffers[cid]) by append()-ing each fragment. The
// only pre-existing size check gated the optimistic reserve() at the first
// fragment; the append() was UNCONDITIONAL, so a client declaring a huge
// info->len or streaming many continuation frames grew the buffer without bound
// until allocation failed (bad_alloc / abort) — the real-display analog of the
// sim-only PRO-209 single-frame cap.
//
// The fix routes every append through wsReassemblyWouldExceed(), a PURE
// header-only predicate (no Arduino / network / heap deps) that bounds BOTH the
// declared total (info->len at the first fragment) AND the running reassembled
// total (current buffer bytes + this fragment's bytes). These tests pin that
// contract so a future cap tweak can't silently re-open the unbounded path.

void setUp(void) {}
void tearDown(void) {}

// --- Under-cap: legitimate small control messages append OK (no behavior change)

// A representative small JSON control message (a few KB) on an empty buffer is
// well under the cap and must be accepted. This is the common case — req:* /
// res:* / evt:* messages — and the guard must not regress it.
void test_small_message_accepted(void) {
    TEST_ASSERT_FALSE(wsReassemblyWouldExceed(/*current=*/0, /*incoming=*/4 * 1024, /*declared=*/4 * 1024));
    // Single byte, single-fragment message.
    TEST_ASSERT_FALSE(wsReassemblyWouldExceed(0, 1, 1));
    // Empty/keepalive frame.
    TEST_ASSERT_FALSE(wsReassemblyWouldExceed(0, 0, 0));
}

// Cumulative fragments that stay under the cap keep being accepted: appending
// onto a partially-filled buffer is fine as long as the running total fits.
void test_cumulative_under_cap_accepted(void) {
    TEST_ASSERT_FALSE(wsReassemblyWouldExceed(/*current=*/200 * 1024, /*incoming=*/40 * 1024, /*declared=*/0));
}

// --- Boundary: exactly at the cap reads, one byte over is refused.

// Pinning the boundary stops a future cap tweak from silently shifting it.
void test_cap_boundary(void) {
    // Running total lands exactly on the cap -> accepted.
    TEST_ASSERT_FALSE(wsReassemblyWouldExceed(0, kWsMaxReassemblyBytes, 0));
    TEST_ASSERT_FALSE(wsReassemblyWouldExceed(kWsMaxReassemblyBytes - 1, 1, 0));
    // One byte over the cap -> refused.
    TEST_ASSERT_TRUE(wsReassemblyWouldExceed(0, kWsMaxReassemblyBytes + 1, 0));
    TEST_ASSERT_TRUE(wsReassemblyWouldExceed(kWsMaxReassemblyBytes, 1, 0));
}

// --- Declared-total cap: a single oversize declared length is rejected up front.

// The KEY PRO-350 invariant for the optimistic path: a client announcing a huge
// info->len at the first fragment is rejected BEFORE a single byte is appended,
// so it can never force a large allocation. (The old 64 KiB reserve() check
// only skipped the reserve(); the append still ran unbounded.)
void test_oversize_declared_total_rejected(void) {
    TEST_ASSERT_TRUE(wsReassemblyWouldExceed(/*current=*/0, /*incoming=*/0, /*declared=*/kWsMaxReassemblyBytes + 1));
    // Mirror the sim's 8 MiB abuse case: an 8 MiB declared total is refused.
    TEST_ASSERT_TRUE(wsReassemblyWouldExceed(0, 0, static_cast<size_t>(8) * 1024 * 1024));
    // Exactly at the cap is allowed.
    TEST_ASSERT_FALSE(wsReassemblyWouldExceed(0, 0, kWsMaxReassemblyBytes));
}

// --- Cumulative crossing: many continuation fragments are rejected AT the
//     crossing fragment, not before and not after.

// The defect this fixes: the reassembled TOTAL across continuation fragments was
// unbounded. Walk fragments that individually fit but cumulatively cross the
// cap; the predicate must reject exactly at the fragment that would cross.
void test_cumulative_fragments_rejected_at_crossing(void) {
    const size_t frag = 64 * 1024; // 64 KiB fragments
    size_t current = 0;
    bool rejected = false;
    for (int i = 0; i < 100; i++) {
        if (wsReassemblyWouldExceed(current, frag, /*declared=*/0)) {
            rejected = true;
            break;
        }
        current += frag;
    }
    TEST_ASSERT_TRUE(rejected);
    // 256 KiB cap / 64 KiB frags: fragments 1..4 fit (total 256 KiB == cap),
    // the 5th (would make 320 KiB) is rejected. So the buffer stalls at the cap.
    TEST_ASSERT_EQUAL_UINT(kWsMaxReassemblyBytes, current);
}

// A fragment that on its own exceeds the remaining headroom is refused even when
// the buffer is only partially full (running-total guard, not per-fragment).
void test_partial_buffer_oversize_fragment_rejected(void) {
    TEST_ASSERT_TRUE(wsReassemblyWouldExceed(/*current=*/200 * 1024, /*incoming=*/100 * 1024, /*declared=*/0));
}

// --- Overflow safety: a near-SIZE_MAX incoming/current must not wrap to a pass.

// The guard is written via headroom subtraction so currentBufBytes + incomingLen
// is never formed; a near-SIZE_MAX value must still be refused, not wrap to a
// small sum that slips under the cap.
void test_overflow_safe(void) {
    TEST_ASSERT_TRUE(wsReassemblyWouldExceed(/*current=*/0, /*incoming=*/static_cast<size_t>(-1), /*declared=*/0));
    TEST_ASSERT_TRUE(wsReassemblyWouldExceed(static_cast<size_t>(-1), 1, 0));
    TEST_ASSERT_TRUE(wsReassemblyWouldExceed(0, 0, static_cast<size_t>(-1)));
}

static int runWsReassemblyCapTests() {
    UNITY_BEGIN();
    RUN_TEST(test_small_message_accepted);
    RUN_TEST(test_cumulative_under_cap_accepted);
    RUN_TEST(test_cap_boundary);
    RUN_TEST(test_oversize_declared_total_rejected);
    RUN_TEST(test_cumulative_fragments_rejected_at_crossing);
    RUN_TEST(test_partial_buffer_oversize_fragment_rejected);
    RUN_TEST(test_overflow_safe);
    return UNITY_END();
}

#ifdef ARDUINO
void setup() { runWsReassemblyCapTests(); }

void loop() {}
#else
int main(int argc, char **argv) { return runWsReassemblyCapTests(); }
#endif
