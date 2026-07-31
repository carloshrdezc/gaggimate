#ifndef WSREASSEMBLYPOLICY_H
#define WSREASSEMBLYPOLICY_H

#include <cstddef>

// PRO-350: bounded, alloc-safe policy for reassembling fragmented WebSocket
// frames in WebUIPlugin (the real display server). This is the real-display
// analog of the sim-only PRO-209 frame-length cap (sim/web/ESPAsyncWebServer.cpp),
// and the WS counterpart of PRO-349's sdReadSizeDecision() pattern.
//
// WebUIPlugin reassembles a fragmented WS message into a per-client
// std::string (rxBuffers[cid]) by append()-ing each fragment as it arrives.
// Before this policy, the only size check gated the OPTIMISTIC reserve() at
// index==0; the actual append() was UNCONDITIONAL. A client declaring a huge
// info->len, or streaming many continuation frames, grew the buffer without
// bound until allocation failed (bad_alloc / abort).
//
// NOTE the difference from the sim's PRO-209 fix: the sim caps a SINGLE frame's
// declared length. The defect here is the REASSEMBLED TOTAL across continuation
// fragments, so this cap bounds the running total (current bytes + this
// fragment's bytes) AND the declared total (info->len at the first fragment),
// not just one frame.
//
// This header captures the cap as a PURE decision with no Arduino / network /
// heap dependencies, so it links and runs in [env:native] (mirrors
// SdReadRetryPolicy.h / ChangeModeDeferPolicy.h).

// Hard cap (bytes) on a reassembled WebSocket message. The WS control surface
// is exclusively small JSON (req:* / res:* / evt:* messages with a `tp` type
// field — profile saves are the largest at a few KB). 256 KiB is far above any
// legitimate control message yet keeps the worst-case reassembly allocation
// tightly bounded — deliberately tighter than the sim's 8 MiB single-frame
// ceiling, because here the cap bounds the cumulative reassembled total of a
// memory-constrained ESP32-S3 and there is no legitimate multi-MiB message on
// this surface. A message exceeding this is a protocol violation / abuse and is
// dropped (buffer cleared, client closed) rather than accumulated.
constexpr size_t kWsMaxReassemblyBytes = size_t{256} * 1024;

// Pure predicate: would appending `incomingLen` bytes to a buffer that already
// holds `currentBufBytes` bytes exceed `cap`? Also catches a declared total
// (declaredTotalLen, i.e. info->len at the first fragment) that already exceeds
// the cap, so a client announcing a huge length is rejected up front before a
// single byte is appended. Overflow-safe: comparisons are written so the sum
// `currentBufBytes + incomingLen` is never formed in a way that could wrap
// (compare against the remaining headroom instead).
//
// Returns true => the append must be REFUSED (drop the buffer, do not append).
// Returns false => the append is within the cap and may proceed.
constexpr bool wsReassemblyWouldExceed(size_t currentBufBytes, size_t incomingLen, size_t declaredTotalLen,
                                       size_t cap = kWsMaxReassemblyBytes) {
    // Declared total (known at the first fragment) over the cap: reject up front.
    if (declaredTotalLen > cap) {
        return true;
    }
    // Running total would cross the cap. Written as headroom subtraction so we
    // never form currentBufBytes + incomingLen (which could wrap on overflow):
    //   currentBufBytes + incomingLen > cap   <=>   incomingLen > cap - currentBufBytes
    if (currentBufBytes > cap) {
        return true;
    }
    return incomingLen > cap - currentBufBytes;
}

#endif // WSREASSEMBLYPOLICY_H
