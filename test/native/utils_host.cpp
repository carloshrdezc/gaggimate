// Host (native) implementations of a few free functions that the firmware
// logic under test links against but whose production definitions live in
// translation units that cannot compile off-target (utils.cpp pulls in
// esp_random(), ESP.getEfuseMac(), micros(), randomSeed() ... all ESP-IDF).
//
// We re-implement ONLY the pure, hardware-independent helpers that the code
// under test actually needs. Logic must match the production source in
// src/display/core/utils.cpp; keep them in sync. Today that is just
// isSafeId(), used by parseProfile().
//
// Note: <stdexcept> is included before utils.h because utils.h defines a
// string_format() template that throws std::runtime_error without including
// <stdexcept> itself. The ESP32 toolchain pulls it in transitively; the host
// compiler (GCC) is stricter, so we satisfy it here without touching the
// production header.
#include <stdexcept>

#include <display/core/utils.h>

bool isSafeId(const String &id) {
    const size_t len = id.length();
    if (len == 0 || len > 32) {
        return false;
    }
    for (size_t i = 0; i < len; ++i) {
        const char c = id[i];
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!ok) {
            return false;
        }
    }
    return true;
}
