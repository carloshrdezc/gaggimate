#ifndef SSLRELAYSTARTUPPOLICY_H
#define SSLRELAYSTARTUPPOLICY_H

#include <cstddef>

// PRO-347 (PRO-346 audit finding F1): internal-DRAM startup gate for the SSL
// cloud-relay path.
//
// The legacy guard bailed out of the SSL relay startup when
// esp_get_free_heap_size() < 60000. On the ESP32-S3 that figure is the COMBINED
// internal + PSRAM pool, dominated by the multi-MB PSRAM region, so the check
// was almost always satisfied even when the small INTERNAL DMA-capable DRAM
// pool (MALLOC_CAP_INTERNAL) was exhausted. relayWs.beginSSL() drives an
// mbedTLS handshake whose in/out content buffers are a large TRANSIENT internal
// allocation (~50 KB); under HomeKit + BLE + WiFi + mDNS that pool is what
// starves, producing `SSL - Memory allocation failed (-32512)` while the
// combined "free heap" still looks healthy. This is the same legacy
// anti-pattern PRO-334 migrated the OTA-TLS check away from (see HeapDiag.h:11
// and kOtaCheckInternalDramFloorBytes in WebUIPlugin.h).
//
// The fix gates the SSL relay startup on the largest contiguous internal-DRAM
// block (gmInternalLargestBlock()) instead of the combined pool. This header
// captures the pure decision — floor + predicate — with no Arduino / heap_caps
// / FreeRTOS dependencies, so it links and runs in [env:native] (mirrors
// SdReadRetryPolicy.h / WiFiFallbackPolicy.h).

// Minimum internal-DRAM largest-free-block (bytes) required before starting the
// SSL cloud-relay client. Set ABOVE the OTA check's 48 KB floor
// (kOtaCheckInternalDramFloorBytes): the relay handshake's mbedTLS content
// buffers "can reach 50 KB" and the original guard's intent was a 60000-byte
// combined-pool margin, both of which argue for a higher INTERNAL-DRAM floor
// than the OTA path. 56 KB leaves headroom above the ~50 KB transient draw
// without demanding so large a contiguous block that a healthy device with
// momentary fragmentation is needlessly refused (which would only mean a
// retriable skip, but a needless one). Deliberately NOT reused from the OTA
// floor — the SSL relay draw is larger.
constexpr size_t kSslRelayInternalDramFloorBytes = 56 * 1024;

// Pre-flight gate: start the SSL relay only when the largest contiguous
// internal-DRAM block clears the floor. Pure function of the measured headroom
// (no hardware deps), so it is host-compilable and unit-testable in [env:native].
constexpr bool sslRelayDramSufficient(size_t largestInternalBlock, size_t floor) { return largestInternalBlock >= floor; }

#endif // SSLRELAYSTARTUPPOLICY_H
