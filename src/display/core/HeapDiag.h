#ifndef HEAPDIAG_H
#define HEAPDIAG_H

// PRO-334: internal DMA-capable DRAM instrumentation + headroom helpers.
//
// On the ESP32-S3 the "free heap" reported by esp_get_free_heap_size() is the
// COMBINED internal + PSRAM pool, dominated by the multi-MB PSRAM region. The
// pool that actually starves under HomeKit + BLE + WiFi + mDNS + TLS is the
// much smaller INTERNAL DRAM region (MALLOC_CAP_INTERNAL) that sdmmc DMA, lwIP
// pbufs and mbedTLS handshake buffers must come from. Reporting and gating on
// the combined figure (as the legacy SSL relay guard did) is why the device
// could show ~6.9 MB free while a 512-byte sdmmc DMA buffer failed with ENOMEM.
//
// These helpers report and reason about the INTERNAL pool specifically:
//   * gmInternalFreeBytes()        - free internal DRAM (MALLOC_CAP_INTERNAL).
//   * gmInternalLargestBlock()     - largest contiguous internal block; this is
//                                    the figure a single DMA/handshake alloc
//                                    must fit into, so it is the meaningful
//                                    "can this allocation succeed" signal.
//   * GM_LOG_INTERNAL_DRAM(tag)    - log both at a named checkpoint.
//
// On the native sim build (GAGGIMATE_SIM) there is no heap_caps allocator; the
// helpers return a large sentinel (so headroom gates never spuriously trip in
// the host UI sim) and the log macro is a no-op.

#include <cstddef>

#if !defined(GAGGIMATE_SIM)
#include <esp_heap_caps.h>
#include <esp_log.h>

inline size_t gmInternalFreeBytes() { return heap_caps_get_free_size(MALLOC_CAP_INTERNAL); }

inline size_t gmInternalLargestBlock() { return heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL); }

// Log internal-DRAM headroom at a named checkpoint. Kept at INFO so it shows on
// the device serial at the default CORE_DEBUG_LEVEL=3 used by the firmware envs.
#define GM_LOG_INTERNAL_DRAM(tag)                                                                                                \
    ESP_LOGI("HeapDiag", "%s: internal DRAM free=%u B, largest block=%u B", (tag), static_cast<unsigned>(gmInternalFreeBytes()), \
             static_cast<unsigned>(gmInternalLargestBlock()))

#else // GAGGIMATE_SIM

// Host sim: no internal/PSRAM split. Return a large value so headroom gates are
// no-ops and the sim WebUI behaves as if memory is plentiful.
inline size_t gmInternalFreeBytes() { return static_cast<size_t>(1) << 20; }
inline size_t gmInternalLargestBlock() { return static_cast<size_t>(1) << 20; }
#define GM_LOG_INTERNAL_DRAM(tag) ((void)0)

#endif // GAGGIMATE_SIM

#endif // HEAPDIAG_H
