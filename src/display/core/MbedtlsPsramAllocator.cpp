// PRO-569 (Ref PRO-566): route mbedTLS allocations to PSRAM at boot. See
// MbedtlsPsramAllocator.h and MbedtlsPsramAllocatorPolicy.h for rationale.
//
// Device-only translation unit: excluded from [env:native] (the policy header is
// unit-tested there) and from the simulator (no real heap_caps / PSRAM). The
// build_src_filter for display envs compiles this; native/sim do not.
#if !defined(GAGGIMATE_SIM) && !defined(NATIVE_BUILD) && (defined(ESP_PLATFORM) || defined(ARDUINO))

#include "MbedtlsPsramAllocator.h"
#include "MbedtlsPsramAllocatorPolicy.h"

#include <Arduino.h>          // psramFound()
#include <esp_heap_caps.h>    // heap_caps_calloc / heap_caps_free / MALLOC_CAP_SPIRAM
#include <mbedtls/platform.h> // mbedtls_platform_set_calloc_free

static const char *const kMbedtlsPsramTag = "MbedtlsPsram";

// PSRAM-backed calloc matching mbedtls_calloc's (n, size) contract. The size /
// overflow / zero-request decision is delegated to the pure, unit-tested policy
// so the contract is pinned by static_asserts; the only thing this wrapper adds
// is the actual heap_caps_calloc(MALLOC_CAP_SPIRAM). heap_caps_calloc already
// zero-initialises and re-checks the multiply, but we gate on the policy first
// so a zero/overflowing request never reaches the allocator.
static void *mbedtlsPsramCalloc(size_t n, size_t size) {
    if (!mbedtlsCallocShouldAllocate(n, size)) {
        return nullptr; // zero request or n*size overflow -> NULL, per mbedtls_calloc
    }
    return heap_caps_calloc(n, size, MALLOC_CAP_SPIRAM);
}

// Free counterpart. heap_caps_free tolerates nullptr and handles PSRAM pointers.
static void mbedtlsPsramFree(void *ptr) { heap_caps_free(ptr); }

bool installMbedtlsPsramAllocator() {
    static bool installed = false;
    if (installed) {
        return true; // idempotent: only swap the hook once
    }

    if (!psramFound()) {
        // No PSRAM on this board: leave mbedtls on its default internal-DRAM
        // allocator. The PRO-554 pre-flight guard still fails the OTA resolve
        // closed instead of panicking, so this degrades safely.
        ESP_LOGW(kMbedtlsPsramTag, "PSRAM not found; mbedTLS stays on internal-DRAM allocator (OTA floor unchanged)");
        return false;
    }

    // Swap mbedtls's runtime calloc/free function pointers (MBEDTLS_PLATFORM_MEMORY
    // is defined in the ESP-IDF mbedtls port config, so mbedtls_calloc is an
    // overridable pointer, not a macro). Every mbedtls allocation after this —
    // including mbedtls_ssl_setup()'s two ~16.3 KiB record buffers — comes from
    // PSRAM. Non-DMA PSRAM is correct: SSL record buffers are copied in software
    // and never DMA'd.
    const int rc = mbedtls_platform_set_calloc_free(mbedtlsPsramCalloc, mbedtlsPsramFree);
    if (rc != 0) {
        ESP_LOGE(kMbedtlsPsramTag, "mbedtls_platform_set_calloc_free failed (rc=%d); staying on default allocator", rc);
        return false;
    }

    installed = true;
    ESP_LOGI(kMbedtlsPsramTag, "mbedTLS allocations routed to PSRAM (OTA-check TLS record buffers off internal DRAM)");
    return true;
}

#endif // device-only
