#ifndef UDPLOGTEEPOLICY_H
#define UDPLOGTEEPOLICY_H

#include <cstddef>

// PRO-334: bounded, ENOMEM-aware policy for the Diagnostic UDP log tee send.
//
// The UDP log tee broadcasts EVERY INFO+ log line to 255.255.255.255:9999. On
// the display (ESP32-S3, Arduino-esp32 3.x / IDF 5.x) with HomeKit + BLE + WiFi
// + mDNS all up, the internal DMA-capable DRAM pool is exhausted (the multi-MB
// "free heap" is PSRAM; lwIP pbufs, the UDP socket, and mDNS answer buffers all
// draw from the much smaller INTERNAL pool). Per the issue's hard evidence the
// failure spirals two ways:
//
//   1. The drain task pokes WiFiUDP::beginPacket()/endPacket() on every line.
//      endPacket() -> sendto() returns ENOMEM (errno 12) when lwIP can't get a
//      pbuf from the starving internal pool. Hammering the send path keeps
//      pressure on the very pool mdns_send and the async web server need, so the
//      web UI goes unreachable while WiFi still shows "connected".
//   2. The framework logs the failed send via log_e("could not send data: %d").
//      That log line is itself ESP_LOG output, so it routes back through THIS
//      tee, gets enqueued, and the drain task tries to send it -> fails again ->
//      logs again: a self-amplifying log storm that accelerates the exhaustion.
//
// This header captures the two pure decisions that make the send path fail-safe,
// with no Arduino / WiFi / FreeRTOS dependencies so it links and runs in
// [env:native] (mirrors SdReadRetryPolicy.h / WiFiFallbackPolicy.h):
//
//   1. shouldSendUdpLogLine() - a drop-when-no-buffer gate: only attempt the UDP
//      send when internal-DRAM headroom is above a floor. Below the floor the
//      drain task DROPS the line (the tee is a best-effort debug stream — a
//      dropped log line is harmless; an ENOMEM spiral that kills the web UI is
//      not). This is the "reserve an internal-DRAM floor" the issue asks for:
//      the tee yields the internal pool to lwIP/mDNS/HTTP under pressure.
//
//   2. shouldLogUdpSendFailure() - rate-limits the send-failure diagnostic so the
//      log-storm feedback loop (failure 2 above) can never form. We surface the
//      FIRST failure (so the condition is observable on serial/SD) and then only
//      one line per kUdpSendFailureLogEvery failures, instead of one per dropped
//      packet. Pure function of the running consecutive-failure count.

// Minimum internal-DRAM largest-free-block (bytes) required before the drain
// task attempts a UDP broadcast. A datagram send needs an lwIP pbuf plus (on the
// first send after a socket teardown) a fresh UDP socket, both from the internal
// pool. Sized as a safety margin above a single ~1.5 KB pbuf so the tee stops
// drawing from the internal pool well before lwIP/mDNS/HTTP themselves would
// fail. The cost of a false "drop" is a missed debug log line (the SD sink, which
// runs first in the drain task, still captures it); the cost of a false "attempt"
// is feeding the ENOMEM spiral that makes the web UI unreachable — so this is
// deliberately conservative. Below the WebUI OTA-TLS floor (48 KiB) since a UDP
// datagram needs far less contiguous memory than an mbedTLS handshake.
constexpr size_t kUdpLogTeeInternalDramFloorBytes = 8 * 1024;

// Rate-limit period for the send-failure diagnostic: after the first failure is
// surfaced, log only one line per this many consecutive failures. Stops the
// "failed send logs an error -> error re-enters the tee -> drain task tries to
// send it -> fails -> logs again" storm from forming, while still leaving the
// condition observable on serial/SD.
constexpr unsigned long kUdpSendFailureLogEvery = 256;

// Drop-when-no-buffer gate: attempt the UDP broadcast only when the largest
// contiguous internal-DRAM block clears the floor. Pure function of the measured
// headroom. Below the floor the caller DROPS the line (best-effort tee).
constexpr bool shouldSendUdpLogLine(size_t largestFreeInternalBlock) {
    return largestFreeInternalBlock >= kUdpLogTeeInternalDramFloorBytes;
}

// Rate-limit the send-failure log. `consecutiveFailures` is the running count of
// back-to-back failed sends INCLUDING the current one (so the first failure is
// 1). Returns true for the first failure and then once per kUdpSendFailureLogEvery
// failures; false otherwise. Pure function of the count so it is trivially
// host-testable and can never itself allocate or block.
constexpr bool shouldLogUdpSendFailure(unsigned long consecutiveFailures) {
    if (consecutiveFailures == 0) {
        return false; // not a failure
    }
    if (consecutiveFailures == 1) {
        return true; // always surface the first failure so the condition is visible
    }
    return (consecutiveFailures % kUdpSendFailureLogEvery) == 0;
}

#endif // UDPLOGTEEPOLICY_H
