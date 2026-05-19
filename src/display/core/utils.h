#pragma once
#ifndef UTILS_H
#define UTILS_H
#include <Arduino.h>
#include <memory>

template <typename T, typename... Args> std::unique_ptr<T> make_unique(Args &&...args) {
    return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}

template <typename... Args> std::string string_format(const std::string &format, Args... args) {
    int size_s = std::snprintf(nullptr, 0, format.c_str(), args...) + 1; // Extra space for '\0'
    if (size_s <= 0) {
        throw std::runtime_error("Error during formatting.");
    }
    auto size = static_cast<size_t>(size_s);
    std::unique_ptr<char[]> buf(new char[size]);
    std::snprintf(buf.get(), size, format.c_str(), args...);
    return std::string(buf.get(), buf.get() + size - 1); // We don't want the '\0' inside
}

extern uint8_t randomByte();
extern String generateShortID(uint8_t length = 10);
extern std::vector<String> explode(const String &input, char delim);
extern String implode(const std::vector<String> &strings, String delim);

/**
 * Returns true iff `id` is a safe identifier for use as a profile/bean
 * filename component on SPIFFS. Safe IDs match `^[A-Za-z0-9_-]{1,32}$`.
 *
 * Centralized here so every entry point that accepts an ID over the wire
 * (WebSocket, HTTP) and every parser that reads one from JSON can use the
 * same rule. Rejecting at the boundary prevents path-traversal IDs like
 * `../config` reaching ProfileManager / BeanManager filesystem helpers.
 */
extern bool isSafeId(const String &id);

#endif // UTILS_H
