#pragma once

#include <cstddef>

constexpr size_t kMaxMdnsNameLength = 63;

inline bool isValidMdnsName(const char *name, size_t length) {
    if (name == nullptr || length == 0 || length > kMaxMdnsNameLength || name[0] == '-' || name[length - 1] == '-') {
        return false;
    }

    for (size_t i = 0; i < length; ++i) {
        const char ch = name[i];
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-')) {
            return false;
        }
    }
    return true;
}

template <size_t N> inline bool isValidMdnsName(const char (&name)[N]) { return isValidMdnsName(name, N - 1); }
