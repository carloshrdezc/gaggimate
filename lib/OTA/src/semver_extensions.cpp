
#include <Arduino.h>

#include <cerrno>
#include <climits>
#include <cstdlib>
#include <sstream>
#include <vector>

#include "semver.h"

using namespace std;

vector<string> split(const string &s, char delim) {
    vector<string> result;
    stringstream ss(s);
    string item;

    while (getline(ss, item, delim)) {
        result.push_back(item);
    }

    return result;
}

// Parse a single semver core component (major/minor/patch) with std::strtol.
// A token is valid only if it is non-empty AND strtol consumes the ENTIRE
// token (end pointer at the terminating '\0'), so non-numeric ("abc"), empty,
// or trailing-garbage ("12x") tokens are rejected instead of silently coerced
// to 0 as the old atoi() did. ERANGE / int-range overflow is also rejected.
static bool parse_component(const string &tok, int &out) {
    if (tok.empty()) {
        return false;
    }
    errno = 0;
    char *end = nullptr;
    long v = strtol(tok.c_str(), &end, 10);
    if (end == tok.c_str() || *end != '\0') {
        return false; // no digits consumed, or trailing garbage
    }
    if (errno == ERANGE || v < INT_MIN || v > INT_MAX) {
        return false;
    }
    out = static_cast<int>(v);
    return true;
}

semver_t from_string(const string &version) {
    if (version.empty()) {
        return {0, 0, 0, nullptr, nullptr};
    }
    // Strip optional leading 'v' so tags like "2.0.0" and "v2.0.0" both parse correctly
    string ver = (version[0] == 'v' || version[0] == 'V') ? version.substr(1) : version;
    // Strip semver build metadata (everything at and after the first '+'). Per semver,
    // build metadata applies to the whole version, is the trailing component, and does
    // NOT affect precedence, so we intentionally ignore it before parsing the core
    // version and prerelease. This must happen before the '.'-split and '-'-split so
    // neither the patch token nor the prerelease can contain a "+meta" suffix.
    auto plus_at = ver.find('+');
    if (plus_at != string::npos) {
        ver = ver.substr(0, plus_at);
    }
    auto numbers = split(ver, '.');
    if (numbers.size() < 3) {
        return {0, 0, 0, nullptr, nullptr};
    }

    // Parse & validate all three core components FIRST, before allocating the
    // prerelease copy, so a malformed patch can never leak a malloc'd buffer.
    int major = 0;
    int minor = 0;
    int patch = 0;
    if (!parse_component(numbers.at(0), major) || !parse_component(numbers.at(1), minor)) {
        return {0, 0, 0, nullptr, nullptr};
    }

    string prerelease;
    bool has_prerelease = false;
    auto split_at = numbers.at(2).find('-');
    if (split_at != string::npos) {
        if (!parse_component(numbers.at(2).substr(0, split_at), patch)) {
            return {0, 0, 0, nullptr, nullptr};
        }
        prerelease = numbers.at(2).substr(split_at + 1);
        has_prerelease = true;
    } else {
        if (!parse_component(numbers.at(2), patch)) {
            return {0, 0, 0, nullptr, nullptr};
        }
    }

    // All core components are known-good; only now allocate the prerelease copy.
    char *prerelease_ptr = nullptr;
    if (has_prerelease) {
        prerelease_ptr = (char *)malloc(prerelease.length() + 1);
        if (prerelease_ptr != nullptr) {
            prerelease.copy(prerelease_ptr, prerelease.length());
            prerelease_ptr[prerelease.length()] = '\0';
        }
        // On malloc failure prerelease_ptr stays nullptr, which is the well-defined
        // "no prerelease" representation (render_to_string guards prerelease != nullptr).
    }

    semver_t _ver = {major, minor, patch, nullptr, prerelease_ptr};

    return _ver;
}

String render_to_string(const semver_t &version) {
    String rendered = String(version.major) + "." + String(version.minor) + "." + String(version.patch);
    if (version.prerelease != nullptr) {
        rendered += "-" + String(version.prerelease);
    }
    return rendered;
}

bool operator>(const semver_t &x, const semver_t &y) { return semver_compare(x, y) > 0; }
