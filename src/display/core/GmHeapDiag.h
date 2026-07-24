#ifndef GMHEAPDIAG_H
#define GMHEAPDIAG_H

// PRO-566 — diagnostic-only internal-DRAM (MALLOC_CAP_INTERNAL) checkpoint logging.
//
// This header is a self-contained, compile-time-gated instrument for the
// per-subsystem internal-DRAM audit described in PRO-566. It mirrors the shape
// of the older (now reverted, IDF-5.x-era) src/display/core/HeapDiag.h so the
// same "instrument -> flash -> measure -> fix" loop documented in the
// gaggimate-esp32-flashing skill can run on the current
// Arduino-ESP 2.0.17 / ESP-IDF 4.4.7 platform.
//
// WHY A SEPARATE, GATED HEADER (not always-on):
//   * The audit needs the largest *contiguous* free internal-DRAM block at each
//     boot subsystem boundary. That is exactly the number the OTA floor
//     (kOtaResolveInternalDramFloorBytes, 48 KiB) gates on, and the number the
//     stock firmware never logs per-subsystem.
//   * `esp_get_free_heap_size()` / `MALLOC_CAP_DEFAULT` is MISLEADING here: it
//     is combined internal + PSRAM (dominated by 8 MB PSRAM), so it always looks
//     healthy (~7 MB) even when the internal pool's largest contiguous block has
//     collapsed below the TLS floor. Always reason on MALLOC_CAP_INTERNAL.
//   * Everything here compiles to NOTHING unless -DGM_HEAP_DIAG_ENABLED=1 is on
//     the build (see [env:display-heapdiag] in platformio.ini). Production and
//     CI builds pay zero bytes and zero cycles.
//
// It is intentionally header-only and depends only on esp_heap_caps.h + esp_log.h
// (both already on the device include path), so it can be included from any
// display translation unit without a new lib dependency or build_src_filter
// entry, and it is #if-guarded off for the native/sim hosts that have no
// heap_caps API.

#if defined(GM_HEAP_DIAG_ENABLED) && GM_HEAP_DIAG_ENABLED && !defined(GAGGIMATE_SIM)

#include <esp_heap_caps.h>
#include <esp_log.h>

namespace gmheapdiag {

inline size_t internalFreeBytes() { return heap_caps_get_free_size(MALLOC_CAP_INTERNAL); }
inline size_t internalLargestBlock() { return heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL); }

} // namespace gmheapdiag

// Log a single checkpoint: free internal DRAM + largest contiguous internal
// block, both in bytes, under the "GmHeapDiag" ESP_LOG tag so a serial/UDP
// capture can be grepped with `grep GmHeapDiag`. The largest-block value is the
// one that matters for the OTA TLS floor (a single mbedtls record buffer must
// come from one contiguous internal-DRAM allocation).
#define GM_HEAP_DIAG(tag)                                                                                                        \
    do {                                                                                                                         \
        ESP_LOGW("GmHeapDiag", "%-32s internal free=%u largest_block=%u", (tag),                                                 \
                 static_cast<unsigned>(gmheapdiag::internalFreeBytes()),                                                         \
                 static_cast<unsigned>(gmheapdiag::internalLargestBlock()));                                                     \
    } while (0)

// One-shot free-block histogram for the internal-DRAM heap at a checkpoint. Use
// at setup() end to distinguish EXHAUSTION (region ~fully allocated) from
// FRAGMENTATION (free bytes exist but scattered into sub-floor blocks).
#define GM_HEAP_DIAG_INFO()                                                                                                      \
    do {                                                                                                                         \
        heap_caps_print_heap_info(MALLOC_CAP_INTERNAL);                                                                          \
    } while (0)

#else // instrumentation compiled out

#define GM_HEAP_DIAG(tag)                                                                                                        \
    do {                                                                                                                         \
    } while (0)
#define GM_HEAP_DIAG_INFO()                                                                                                      \
    do {                                                                                                                         \
    } while (0)

#endif

#endif // GMHEAPDIAG_H
