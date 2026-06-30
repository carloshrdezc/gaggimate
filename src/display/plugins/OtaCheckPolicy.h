#ifndef OTACHECKPOLICY_H
#define OTACHECKPOLICY_H

#include <cstddef>

// PRO-345: recoverable defer policy for the OTA HTTPS version-check.
//
// Background (PRO-334): the OTA version-check drives an mbedTLS handshake whose
// in/out content buffers are a large TRANSIENT internal-DRAM allocation. With
// HomeKit + BLE + WiFi + mDNS up, the small internal DMA-capable DRAM pool
// (MALLOC_CAP_INTERNAL) can be too tight for it, producing
// `SSL - Memory allocation failed (-32512)`. PRO-334 added a floor
// (kOtaCheckInternalDramFloorBytes, 48 KB largest contiguous block) and DEFERRED
// the check below it instead of crashing.
//
// The PRO-334 defer, however, never made forward progress: on this hardware the
// NORMAL steady state (HomeKit + BLE + WiFi + mDNS all up) sits BELOW the 48 KB
// floor, so the check was deferred every interval FOREVER. checkForUpdates()
// never ran, the success-path status update never ran, and the web UI was stuck
// at "Checking..." permanently with an empty latestVersion and no update ever
// offered — indistinguishable from a hung updater, with no recovery path.
//
// This header captures the pure decision that fixes that, with no Arduino /
// heap_caps / FreeRTOS / millis() dependencies, so it links and runs in
// [env:native] (mirrors SslRelayStartupPolicy.h / SdReadRetryPolicy.h). It is
// the SINGLE SOURCE OF TRUTH for the run/defer/skip decision and the floors —
// the WebUIPlugin loop must not duplicate the thresholds.
//
// RECOVERY STRATEGY (PRO-345 acceptance criterion 1 — option "c", a backoff that
// makes real forward progress, NOT blindly lowering the floor):
//
//   * At or above the PREFERRED floor (48 KB): RUN immediately every interval —
//     the fast, healthy path. Unchanged from PRO-334.
//   * Below the preferred floor we still want eventual discovery, so after a
//     longer ESCALATED cadence has elapsed since the last ACTUAL check we make
//     one opportunistic attempt — BUT ONLY when the largest block still clears a
//     hard ABSOLUTE-MINIMUM floor below which a handshake would genuinely OOM.
//     This is the forward-progress path: a device pinned just under the
//     preferred floor at steady state will, after the escalated wait, attempt a
//     real check and can discover + be offered a newer build.
//   * Below the preferred floor and either not yet time for an escalated attempt
//     OR below the absolute-minimum floor: DEFER. Deferral surfaces a distinct,
//     truthful status (handled by the caller) instead of leaving the UI stuck at
//     "Checking...", and it NEVER drives a handshake that would -32512 (PRO-334 /
//     acceptance criterion 3 stays intact: below the absolute minimum we always
//     defer, never attempt).
//
// "Forward progress" here means: a below-preferred-floor device is NOT
// guaranteed to starve forever. It attempts on the escalated cadence, and a
// single successful attempt replaces the deferred status with a real result.

// Absolute-minimum largest contiguous internal-DRAM block (bytes) below which we
// NEVER drive the OTA mbedTLS handshake, even on an escalated attempt. This is
// the hard OOM guard (PRO-334's -32512 protection): the handshake's transient
// internal draw, with the reduced mbedTLS content-length build flags in
// display_common, is empirically in the low-tens-of-KB range; 40 KB is below the
// 48 KB preferred floor (so escalated attempts have room to act) yet still leaves
// enough contiguous internal DRAM that a real attempt has a fighting chance to
// complete rather than certainly OOM. Below 40 KB we always defer.
constexpr size_t kOtaCheckAbsoluteMinInternalDramBytes = static_cast<size_t>(40) * 1024;

// Escalated retry cadence (ms) for the below-preferred-floor forward-progress
// attempt. Much longer than the normal UPDATE_CHECK_INTERVAL so a memory-pinned
// device attempts only occasionally (avoiding any retry-storm against a tight
// allocator) yet still eventually — roughly hourly — drives a real check so the
// update can be discovered. The caller passes the normal interval and this
// escalated interval explicitly; the policy does not hard-wire either.
constexpr unsigned long kOtaCheckEscalatedRetryIntervalMs = 60UL * 60UL * 1000UL; // 1 hour

// The three outcomes of the OTA-check decision. The caller maps:
//   Run   -> drive checkForUpdates(), advance lastCheck, push the real result.
//   Defer -> surface a truthful "deferred — low memory" status, do NOT run TLS,
//            do NOT advance lastCheck (so the escalated timer keeps maturing).
//   Skip  -> not yet time for any check; leave state untouched.
enum class OtaCheckDecision { Skip, Defer, Run };

// Pure decision for the OTA version-check, given the measured internal-DRAM
// largest block and the relevant timing. No hardware/Arduino deps — host
// compilable and unit-testable in [env:native].
//
// Parameters:
//   largestInternalBlock - measured largest contiguous internal-DRAM block.
//   now                  - current monotonic time (millis()).
//   lastCheck            - time of the last ACTUAL check (0 = never run yet).
//   normalInterval       - normal cadence between checks (UPDATE_CHECK_INTERVAL).
//   escalatedInterval    - longer cadence for below-floor forward-progress
//                          attempts (kOtaCheckEscalatedRetryIntervalMs).
//   preferredFloor       - fast-path floor (kOtaCheckInternalDramFloorBytes).
//   absoluteMinFloor     - hard OOM guard (kOtaCheckAbsoluteMinInternalDramBytes).
//
// Decision table (single source of truth):
//   * not yet time for the NORMAL interval (and never-run is treated as "time"):
//       -> Skip.
//   * normal interval elapsed AND block >= preferredFloor:
//       -> Run (the healthy fast path).
//   * normal interval elapsed, block < preferredFloor:
//       - block >= absoluteMinFloor AND escalatedInterval elapsed since lastCheck
//         (or never run): -> Run (forward-progress attempt).
//       - otherwise:                                              -> Defer.
//
// Using millis() unsigned wraparound-safe subtraction (now - lastCheck) matches
// the existing loop() idiom; lastCheck==0 ("never run") forces the first check.
constexpr OtaCheckDecision otaCheckDecision(size_t largestInternalBlock, unsigned long now, unsigned long lastCheck,
                                            unsigned long normalInterval, unsigned long escalatedInterval, size_t preferredFloor,
                                            size_t absoluteMinFloor) {
    const bool neverRun = (lastCheck == 0);
    const bool normalDue = neverRun || (now - lastCheck > normalInterval);
    if (!normalDue) {
        return OtaCheckDecision::Skip;
    }
    if (largestInternalBlock >= preferredFloor) {
        return OtaCheckDecision::Run;
    }
    // Below the preferred floor: only attempt on the escalated cadence, and only
    // when we still clear the hard OOM guard. This is what makes the deferral
    // RECOVERABLE rather than a permanent starve.
    const bool escalatedDue = neverRun || (now - lastCheck > escalatedInterval);
    if (largestInternalBlock >= absoluteMinFloor && escalatedDue) {
        return OtaCheckDecision::Run;
    }
    return OtaCheckDecision::Defer;
}

#endif // OTACHECKPOLICY_H
