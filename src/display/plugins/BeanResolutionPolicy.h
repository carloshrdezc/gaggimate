#pragma once
#ifndef BEAN_RESOLUTION_POLICY_H
#define BEAN_RESOLUTION_POLICY_H

// PRO-422: pure, dependency-light bean-resolution policy.
//
// A shot record durably carries the bean it was pulled with as a (beanId,
// beanName) pair written into the shot notes at capture time (see
// ShotHistoryPlugin). Resolving that record back to a live bean must prefer the
// stable id and fall back to a normalized name match only when no id was
// recorded (older shots, or beans selected before the device stored ids).
//
// This header is intentionally free of Arduino / ESP / FS / ArduinoJson deps so
// it can be compiled straight into the native (host) unit-test env. The
// firmware call site (findBeanIndexForNotes) and the host test share this one
// source of truth for the matching order.

#include <string>
#include <vector>

namespace bean_resolution {

// Lowercase + trim, matching ShotHistoryPlugin::normalizeBeanName so id-less
// name matches behave identically on device and in tests.
inline std::string normalizeName(std::string value) {
    size_t begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return "";
    }
    size_t end = value.find_last_not_of(" \t\r\n");
    value = value.substr(begin, end - begin + 1);
    for (char &c : value) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return value;
}

// A minimal (id, name) view of a bean, so the policy needs no BeanEntry/FS dep.
struct BeanRef {
    std::string id;
    std::string name;
};

// Id-first, name-second resolver. Returns the index into `beans` of the first
// match, or -1 when neither the recorded id nor the recorded name matches.
//
// A non-empty recorded id is tried first; if it matches nothing (bean deleted
// since the shot, or the id came from another device) we fall back to a
// normalized name match. This preserves the pre-PRO-422 behavior of
// findBeanIndexForNotes (which also fell through to name matching) so bean-usage
// accounting is unchanged — the only new capability is that a recorded id, when
// present, is consulted before any name guess.
inline int resolveBeanIndex(const std::string &recordedBeanId, const std::string &recordedBeanName,
                            const std::vector<BeanRef> &beans) {
    if (!recordedBeanId.empty()) {
        for (size_t i = 0; i < beans.size(); ++i) {
            if (beans[i].id == recordedBeanId) {
                return static_cast<int>(i);
            }
        }
    }

    const std::string wanted = normalizeName(recordedBeanName);
    if (wanted.empty()) {
        return -1;
    }
    for (size_t i = 0; i < beans.size(); ++i) {
        if (normalizeName(beans[i].name) == wanted) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

} // namespace bean_resolution

#endif // BEAN_RESOLUTION_POLICY_H
