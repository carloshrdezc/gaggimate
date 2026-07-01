#ifndef PSRAMALLOCATOR_H
#define PSRAMALLOCATOR_H

// PRO-358: route large, NON-DMA application buffers to external PSRAM
// (MALLOC_CAP_SPIRAM) instead of the scarce internal DMA-capable DRAM pool
// (MALLOC_CAP_INTERNAL).
//
// Background (PRO-334 / PRO-345): on the ESP32-S3 the pool that starves under
// HomeKit + BLE + WiFi + mDNS + TLS is the small INTERNAL DRAM region that
// sdmmc DMA, lwIP pbufs and the mbedTLS handshake buffers MUST allocate from.
// The multi-MB PSRAM region is nearly empty by comparison. The OTA version
// check defers forever because the largest contiguous internal block sits below
// the 48 KB floor (OtaCheckPolicy.h). Every large buffer we can move OFF
// internal DRAM to PSRAM raises that largest-block headroom.
//
// SAFETY: only NON-DMA buffers may be offloaded. sdmmc DMA buffers and lwIP
// pbufs REQUIRE internal DRAM and must NEVER use this allocator. The WebSocket
// reassembly buffer (WebUIPlugin rxBuffers) is a safe target: it holds decoded
// application-layer control-message bytes (small JSON req:/res:/evt: messages)
// that are read by the CPU only — they are never a DMA source/target and never
// handed to a peripheral. Standard STL container storage (its append/grow
// backing) is likewise plain CPU-accessed memory.
//
// This is a standard C++ allocator usable as the Alloc template parameter of
// std::basic_string / std::vector / std::unordered_map, so a container's
// backing store is transparently placed in PSRAM while the container's own API
// is unchanged.
//
// HOST/SIM (GAGGIMATE_SIM) and any non-ESP build have no heap_caps allocator, so
// the allocator degrades to plain ::operator new/delete. The sizing DECISION
// (psramOffloadWorthwhile) is a pure constexpr with no hardware dependency, so
// it links and is unit-tested in [env:native] (mirrors OtaCheckPolicy.h).

#include <cstddef>
#include <limits>
#include <new>

// Minimum allocation size (bytes) at or above which routing to PSRAM is
// worthwhile. Very small allocations (a handful of bytes) are not worth an
// external-RAM round trip: PSRAM access is slower than internal SRAM, the
// internal-DRAM pressure they add is negligible, and the per-allocation
// bookkeeping overhead dominates. The buffers we target for offload
// (WebSocket reassembly of multi-KB JSON control messages) are far above this
// threshold. This is exposed as a pure predicate so the policy can be unit
// tested on the host without a heap_caps allocator (mirrors the pure-header
// decision pattern of OtaCheckPolicy.h / SslRelayStartupPolicy.h).
constexpr size_t kPsramOffloadMinBytes = 512;

// Pure decision: is an allocation of `bytes` large enough that placing it in
// PSRAM (rather than internal DRAM) is worthwhile? Small allocations return
// false (keep them wherever the default allocator puts them); allocations at or
// above the threshold return true. No hardware / heap_caps dependency — host
// compilable and unit-testable in [env:native].
constexpr bool psramOffloadWorthwhile(size_t bytes) { return bytes >= kPsramOffloadMinBytes; }

#if !defined(GAGGIMATE_SIM) && defined(ESP_PLATFORM)

#include <esp_heap_caps.h>

// STL allocator whose storage comes from external PSRAM (MALLOC_CAP_SPIRAM).
// Use ONLY for non-DMA, CPU-accessed buffers (see the SAFETY note above).
//
// Allocations below kPsramOffloadMinBytes fall back to internal-capable memory
// via the default heap_caps behaviour (heap_caps_malloc with MALLOC_CAP_8BIT)
// so tiny control-block allocations (e.g. a container's 1-node bookkeeping) are
// not forced through slower PSRAM for no headroom benefit; the large backing
// store — the whole point of the offload — goes to PSRAM.
template <typename T> class PsramAllocator {
  public:
    using value_type = T;

    PsramAllocator() noexcept = default;
    template <typename U> PsramAllocator(const PsramAllocator<U> &) noexcept {}

    T *allocate(std::size_t n) {
        if (n > std::numeric_limits<std::size_t>::max() / sizeof(T)) {
            throw std::bad_alloc();
        }
        const std::size_t bytes = n * sizeof(T);
        const uint32_t caps = psramOffloadWorthwhile(bytes) ? MALLOC_CAP_SPIRAM : MALLOC_CAP_8BIT;
        void *p = heap_caps_malloc(bytes, caps);
        // If PSRAM is exhausted (or the chip has none), fall back to the general
        // heap so allocation never spuriously fails just because SPIRAM is full.
        if (p == nullptr && caps == MALLOC_CAP_SPIRAM) {
            p = heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
        }
        if (p == nullptr) {
            throw std::bad_alloc();
        }
        return static_cast<T *>(p);
    }

    void deallocate(T *p, std::size_t) noexcept { heap_caps_free(p); }

    template <typename U> bool operator==(const PsramAllocator<U> &) const noexcept { return true; }
    template <typename U> bool operator!=(const PsramAllocator<U> &) const noexcept { return false; }
};

#else // host / sim / non-ESP: no heap_caps allocator available.

// Degrade to the default allocator so display code that names PsramAllocator
// (e.g. the WebUIPlugin reassembly buffer type) still compiles under the sim
// (GAGGIMATE_SIM) and any non-ESP host build. Behaviour is identical to
// std::allocator<T>; the sizing decision above is still exercised by tests.
#include <memory>
template <typename T> using PsramAllocator = std::allocator<T>;

#endif // PSRAM allocator availability

#endif // PSRAMALLOCATOR_H
