#ifndef MBEDTLSPSRAMALLOCATOR_H
#define MBEDTLSPSRAMALLOCATOR_H

// PRO-569 (Ref PRO-566): install a PSRAM-backed calloc/free for mbedTLS at boot
// so the OTA-check TLS handshake's two ~16.3 KiB record buffers come from
// external PSRAM instead of scarce internal DRAM, clearing the PRO-554 48 KiB
// internal-DRAM floor. See MbedtlsPsramAllocatorPolicy.h for the full rationale
// and the size/overflow contract this module enforces.
//
// Must be called ONCE, early in boot, AFTER the Arduino core has initialised
// PSRAM (any time inside Controller::setup() qualifies) and BEFORE the first TLS
// handshake (the periodic OTA check / channel-switch resolve, both far later).
//
// Idempotent and fail-safe: if PSRAM is unavailable, or the mbedtls platform
// hook is not compiled in, it leaves mbedtls on its default (internal-DRAM)
// allocator and returns false — the PRO-554 pre-flight guard still prevents an
// OOM panic, so the device degrades to the pre-fix behaviour rather than
// crashing. Returns true when the PSRAM allocator was installed.
bool installMbedtlsPsramAllocator();

#endif // MBEDTLSPSRAMALLOCATOR_H
