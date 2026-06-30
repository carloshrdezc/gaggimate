#ifndef SDREADRETRYPOLICY_H
#define SDREADRETRYPOLICY_H

#include <cstddef>

// PRO-334: bounded, ENOMEM-aware policy for the async profile-load SD read.
//
// On the display (ESP32-S3, Arduino-esp32 3.x / IDF 5.x) with HomeKit + BLE +
// WiFi + mDNS + TLS all up, the internal DMA-capable DRAM pool is exhausted
// (the multi-MB "free heap" is PSRAM; the internal pool is what sdmmc DMA, lwIP
// and mbedTLS draw from). When the WebUI sends `req:profiles:load`, the handler
// runs INLINE ON THE AsyncTCP task and reads the profile JSON off the SD card.
// If sdmmc_read_sectors can't get its DMA buffer it returns ESP_ERR_NO_MEM
// (0x101) repeatedly; the streaming deserialize keeps asking for bytes and the
// async_tcp task never pets its watchdog -> task-WDT abort -> REBOOT. That
// reboot-on-Brew is the worst symptom of the cluster.
//
// This header captures the two pure decisions that make that path fail-safe,
// with no Arduino / SD / FreeRTOS dependencies so it links and runs in
// [env:native] (mirrors WiFiFallbackPolicy.h):
//
//   1. shouldAttemptSdRead() - a pre-flight gate: only attempt the DMA-backed
//      SD read when internal-DRAM headroom is above a floor. Below the floor we
//      refuse and surface an error to the client immediately rather than poke a
//      starving allocator and risk wedging the async task.
//
//   2. nextSdReadBackoffMs() - a STRICTLY BOUNDED retry schedule for a transient
//      read failure. The caller retries at most kSdReadMaxAttempts times with
//      these short, capped backoffs (yielding to the scheduler between tries so
//      the watchdog is pet), then gives up with a clean error. There is no
//      unbounded spin under any input.

// Minimum internal-DRAM largest-free-block (bytes) required before we attempt a
// profile SD read on the async task. A profile JSON is small (a few KB) but the
// sdmmc DMA descriptor + sector buffer + ArduinoJson document all draw from the
// internal pool; below this floor the read is overwhelmingly likely to ENOMEM,
// so we fail fast and keep the async task responsive instead of thrashing the
// allocator. Sized as a safety margin above a single 512-byte sector DMA plus a
// small JSON working set; tuned conservatively because the cost of a false
// "skip" is a clean retriable error to the client, while the cost of a false
// "attempt" is a potential watchdog reboot.
constexpr size_t kSdReadInternalDramFloorBytes = 16 * 1024;

// Bounded retry schedule for a transient SD read failure on the async task.
constexpr int kSdReadMaxAttempts = 3;

// Upper bound (bytes) on a profile JSON file we will read whole into memory on
// the async task. A real profile is a few KB; this cap stops a corrupt/huge
// file from forcing a large internal allocation (or an unbounded read loop) on
// the memory-starved async path. Files larger than this are treated as a failed
// read (clean error to the client) rather than attempted.
constexpr size_t kProfileMaxFileBytes = 64 * 1024;

// Pre-flight gate: attempt the SD read only when the largest contiguous
// internal-DRAM block clears the floor. Pure function of the measured headroom.
constexpr bool shouldAttemptSdRead(size_t largestFreeInternalBlock) {
    return largestFreeInternalBlock >= kSdReadInternalDramFloorBytes;
}

// Backoff (ms) before retry attempt `attempt` (1-based: attempt 1 is the first
// RETRY, i.e. the second try overall). Short and capped so the total bounded
// wait across all retries stays well under the task-WDT window. Returns 0 for
// out-of-range attempts so the caller's loop is naturally bounded.
constexpr unsigned long nextSdReadBackoffMs(int attempt) {
    if (attempt <= 0 || attempt >= kSdReadMaxAttempts) {
        return 0;
    }
    // 10ms, 20ms — linear, capped. Total worst-case added latency < 30ms.
    return static_cast<unsigned long>(attempt) * 10UL;
}

#endif // SDREADRETRYPOLICY_H
