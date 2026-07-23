#ifndef OTARESOLVEHEAPPOLICY_H
#define OTARESOLVEHEAPPOLICY_H

#include <cstddef>

// PRO-554: pure, host-testable pre-flight heap guard for the WebUIPlugin
// forced-tag / channel-switch OTA resolve path (otaResolveTask).
//
// Background: `WebUIPlugin::otaResolveTask` (PRO-13) opens a FRESH, independent
// HTTPS/TLS connection to GitHub via `GitHubOTA::checkForUpdates()` to resolve
// the selected channel's head version. This is IN ADDITION to the periodic
// background OTA check that already runs on the same GitHubOTA instance's
// member `WiFiClientSecure`. Under current internal-DRAM pressure (same failure
// class as PRO-334/PRO-358, partially mitigated by 9ed5f358/42ce929b), that
// additional TLS handshake can trip an internal-DRAM allocation failure deep
// inside mbedtls's certificate-verify path. On this ESP-IDF/mbedtls port the
// OOM handling there is not fully graceful: instead of cleanly returning an
// error, it can panic (`LoadProhibited` in `mbedtls_ssl_check_curve ->
// ssl_parse_certificate_verify -> ... -> WiFiClientSecure::connect`), which
// crash-loops the device on every channel-switch click. HTTPClient logs
// `SSL - Memory allocation failed` immediately before the panic.
//
// The fix is a pre-flight guard: before otaResolveTask fires the TLS handshake,
// check the largest CONTIGUOUS free internal-DRAM block (mbedtls's TLS buffers
// must come from internal DRAM, and the certificate-verify path needs a single
// large contiguous allocation — total free heap is not enough if it's
// fragmented). If it is below a floor, skip the TLS attempt entirely and fail
// the resolve gracefully (routing to OtaResolveState::Failed exactly like the
// existing xTaskCreatePinnedToCore-OOM branch in the loop() Idle case), rather
// than letting mbedtls panic on OOM mid-handshake.
//
// Header-only + free of any Arduino/FreeRTOS/heap_caps dependency: the caller
// (otaResolveTask, on-device) reads
// `heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)` and passes the value
// in, so the floor-comparison logic itself links on [env:native] via the
// existing `-I src` and needs no new build_src_filter entry. Mirrors the
// OtaAsyncResolvePolicy.h / OtaUpdateCheckPolicy.h precedent in this directory:
// every function here is a deterministic function of its arguments, with a
// compile-time static_assert truth table pinning the contract.

// Floor for the largest contiguous free internal-DRAM block below which a fresh
// TLS handshake is considered unsafe and the OTA resolve fails closed instead
// of attempting it.
//
// Sized at 48 KiB, consistent with the ~48KB internal-DRAM headroom figure the
// PRO-358 mitigation work targeted for the TLS/mbedtls path: a full TLS 1.2
// handshake against GitHub (certificate chain receive + verify) needs on the
// order of a 16 KiB record buffer plus several KiB of transient
// certificate-parse / ECC-curve scratch, and that scratch must be a single
// contiguous internal-DRAM allocation. 48 KiB leaves margin above the raw
// handshake requirement so the check is a genuine "there is safe room" gate,
// not a "we are already on the edge" gate that still lets a marginal handshake
// slip through and panic.
static constexpr size_t kOtaResolveInternalDramFloorBytes = 48u * 1024u;

// True when the largest contiguous free internal-DRAM block is at or above the
// floor, i.e. it is safe to attempt the fresh TLS handshake. False means the
// caller must skip checkForUpdates() and fail the resolve gracefully.
//
// The boundary is inclusive (`>=`): exactly `floorBytes` free counts as
// sufficient. `floorBytes` defaults to the module floor so callers can just
// pass the measured block size.
constexpr bool otaResolveHeapSufficient(size_t largestFreeInternalBlock, size_t floorBytes = kOtaResolveInternalDramFloorBytes) {
    return largestFreeInternalBlock >= floorBytes;
}

// Compile-time truth table — pins the contract so a future edit to the floor or
// the comparison fails the firmware compile rather than silently changing the
// pre-flight guard (mirrors the OtaAsyncResolvePolicy.h / OtaUpdateCheckPolicy.h
// precedent in this directory).
static_assert(kOtaResolveInternalDramFloorBytes == 49152u, "PRO-554: internal-DRAM floor is 48 KiB");

static_assert(otaResolveHeapSufficient(49152u), "PRO-554: exactly the floor is sufficient (inclusive boundary)");
static_assert(otaResolveHeapSufficient(49153u), "PRO-554: one byte above the floor is sufficient");
static_assert(otaResolveHeapSufficient(1024u * 1024u), "PRO-554: a large free block is sufficient");
static_assert(!otaResolveHeapSufficient(49151u), "PRO-554: one byte below the floor is insufficient");
static_assert(!otaResolveHeapSufficient(0u), "PRO-554: zero free internal DRAM is insufficient");
static_assert(!otaResolveHeapSufficient(16u * 1024u), "PRO-554: a fragmented 16 KiB block is insufficient");

// Explicit-floor overload behavior (used by the on-device caller only via the
// default, but pinned so the parameter cannot silently be dropped/reordered).
static_assert(otaResolveHeapSufficient(100u, 100u), "PRO-554: value == explicit floor is sufficient");
static_assert(!otaResolveHeapSufficient(99u, 100u), "PRO-554: value below explicit floor is insufficient");

#endif // OTARESOLVEHEAPPOLICY_H
