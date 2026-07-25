#ifndef MBEDTLSPSRAMALLOCATORPOLICY_H
#define MBEDTLSPSRAMALLOCATORPOLICY_H

#include <cstddef>
#include <cstdint>

// PRO-569 (Ref PRO-566): pure, host-testable policy for the boot-time mbedTLS
// PSRAM allocator hook that unblocks the OTA-check TLS handshake's 48 KiB
// internal-DRAM floor on classic espressif32 (Arduino-ESP32 2.0.17 / ESP-IDF
// 4.4.7).
//
// Background: on this platform mbedtls_ssl_setup() allocates TWO contiguous
// record buffers of MBEDTLS_SSL_{IN,OUT}_BUFFER_LEN = 16701 bytes each
// (CONFIG_MBEDTLS_SSL_MAX_CONTENT_LEN=16384, CONFIG_MBEDTLS_ASYMMETRIC_CONTENT_LEN
// unset -> in==out). The prebuilt libmbedtls has CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC=y,
// so its default allocator (esp_mbedtls_mem_calloc) draws those 16.3 KiB blocks
// from internal DRAM — exactly the contiguous allocation the PRO-554
// kOtaResolveInternalDramFloorBytes guard protects. Capping the content length
// to 4 KiB (PRO-364's IDF-5.x fix) is INERT here: the size is a compile-time
// #define baked into the prebuilt lib, and neither custom_sdkconfig nor a
// sdkconfig.defaults/menuconfig rebuild path exists on classic espressif32.
//
// The feasible lever is CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC's runtime equivalent:
// the port config #defines MBEDTLS_PLATFORM_MEMORY + MBEDTLS_PLATFORM_C (no
// CALLOC_MACRO), so mbedtls_calloc is a runtime function pointer defaulting to
// esp_mbedtls_mem_calloc, and libmbedcrypto exports mbedtls_platform_set_calloc_free()
// to swap it. Installing a PSRAM-backed calloc/free at boot (before any TLS
// handshake) routes the two 16.3 KiB record buffers — and all other mbedtls
// scratch — into the 8 MB PSRAM. SSL record buffers are copied in software and
// never DMA'd, so non-DMA PSRAM is correct (same rationale as PRO-568's log
// queue). This removes the internal-DRAM contiguous requirement the floor guards.
//
// This header is Arduino/FreeRTOS/heap_caps-free so the size/overflow logic
// links and unit-tests on [env:native]; the on-device .cpp supplies the actual
// heap_caps_calloc(MALLOC_CAP_SPIRAM). Mirrors the OtaResolveHeapPolicy.h /
// OtaUpdateCheckPolicy.h precedent: every function is a deterministic function
// of its arguments with a compile-time static_assert truth table pinning the
// contract.

// mbedtls_calloc(n, size) semantics: allocate n*size bytes, zero-initialised,
// returning nullptr on n==0 || size==0 (mbedtls treats a zero-size request as a
// no-op that yields NULL). The multiply must be overflow-checked: a malicious or
// corrupt length reaching calloc could otherwise wrap and under-allocate. This
// computes the byte count and flags overflow so the caller returns nullptr
// instead of a short buffer.
struct MbedtlsCallocSize {
    size_t bytes;  // n * size (valid only when !overflow && !zero)
    bool zero;     // true when n==0 or size==0 -> allocate nothing, return NULL
    bool overflow; // true when n*size wraps size_t -> refuse, return NULL
};

constexpr MbedtlsCallocSize mbedtlsCallocSize(size_t n, size_t size) {
    return (n == 0 || size == 0)     ? MbedtlsCallocSize{0, true, false}
           : (n > (SIZE_MAX / size)) ? MbedtlsCallocSize{0, false, true}
                                     : MbedtlsCallocSize{n * size, false, false};
}

// True when the caller should actually attempt the allocation (non-zero,
// non-overflowing). False means return nullptr without touching the heap.
constexpr bool mbedtlsCallocShouldAllocate(size_t n, size_t size) {
    return !mbedtlsCallocSize(n, size).zero && !mbedtlsCallocSize(n, size).overflow;
}

// --- Compile-time truth table (pins the contract) -------------------------

// Normal sizes: the two OTA-check TLS record buffers.
static_assert(mbedtlsCallocSize(1, 16701u).bytes == 16701u, "PRO-569: single 16701 B record buffer");
static_assert(!mbedtlsCallocSize(1, 16701u).zero, "PRO-569: 16701 B is a real allocation");
static_assert(!mbedtlsCallocSize(1, 16701u).overflow, "PRO-569: 16701 B does not overflow");
static_assert(mbedtlsCallocSize(16701u, 1).bytes == 16701u, "PRO-569: n*size is commutative for the record buffer");
static_assert(mbedtlsCallocShouldAllocate(1, 16701u), "PRO-569: record buffer should be allocated");

// Typical small mbedtls structs (n==1).
static_assert(mbedtlsCallocSize(1, 256u).bytes == 256u, "PRO-569: 256 B struct alloc");
static_assert(mbedtlsCallocSize(4u, 8u).bytes == 32u, "PRO-569: 4*8 = 32 B array alloc");

// Zero request -> NULL, no heap touch.
static_assert(mbedtlsCallocSize(0, 16u).zero, "PRO-569: n==0 is a zero request");
static_assert(mbedtlsCallocSize(16u, 0).zero, "PRO-569: size==0 is a zero request");
static_assert(!mbedtlsCallocShouldAllocate(0, 16u), "PRO-569: n==0 must not allocate");
static_assert(!mbedtlsCallocShouldAllocate(16u, 0), "PRO-569: size==0 must not allocate");

// Overflow -> refuse (return NULL), never under-allocate.
static_assert(mbedtlsCallocSize(SIZE_MAX, 2u).overflow, "PRO-569: SIZE_MAX*2 overflows");
static_assert(!mbedtlsCallocShouldAllocate(SIZE_MAX, 2u), "PRO-569: overflow must not allocate");
static_assert(mbedtlsCallocSize(SIZE_MAX, 1u).bytes == SIZE_MAX, "PRO-569: SIZE_MAX*1 is exactly representable");
static_assert(!mbedtlsCallocSize(SIZE_MAX, 1u).overflow, "PRO-569: SIZE_MAX*1 does not overflow");
static_assert(mbedtlsCallocShouldAllocate(SIZE_MAX, 1u), "PRO-569: SIZE_MAX*1 is a (huge) valid request");

// --- Install gate (PRO-585) ------------------------------------------------
//
// PRO-585 (Ref PRO-569 / #574): the on-device installMbedtlsPsramAllocator()
// early-returns when PSRAM is absent (psramFound()==false) so mbedTLS stays on
// its default internal-DRAM allocator — degrading safely to pre-fix behaviour,
// with the PRO-554 pre-flight guard still protecting against an OOM panic. That
// psramFound() gate is device-only (Arduino.h), so the decision is extracted
// here as a pure predicate to give the fail-safe path host-test coverage under
// [env:native], mirroring the calloc size/overflow policy above.
//
// Returns true only when the caller should attempt to swap in the PSRAM-backed
// calloc/free: not already installed AND PSRAM is present. The `alreadyInstalled`
// short-circuit keeps installMbedtlsPsramAllocator() idempotent (only swap once).
constexpr bool shouldInstallPsramAllocator(bool alreadyInstalled, bool psramFound) { return !alreadyInstalled && psramFound; }

// Truth table pinning the fail-safe contract.
static_assert(shouldInstallPsramAllocator(false, true), "PRO-585: fresh boot with PSRAM -> install");
static_assert(!shouldInstallPsramAllocator(false, false), "PRO-585: PSRAM absent -> stay on internal-DRAM allocator");
static_assert(!shouldInstallPsramAllocator(true, true), "PRO-585: already installed -> idempotent no-op");
static_assert(!shouldInstallPsramAllocator(true, false), "PRO-585: already installed, no PSRAM -> no-op");

#endif // MBEDTLSPSRAMALLOCATORPOLICY_H
