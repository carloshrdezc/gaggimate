#ifndef SEMVER_EXTENSIONS_H
#define SEMVER_EXTENSIONS_H

#include "semver.h"

// Parse a single semver core component (major/minor/patch) via std::strtol.
// Exposed here (external linkage) purely as a unit-test seam (PRO-390); the
// definition and its behavior live in semver_extensions.cpp and are unchanged.
// Returns true and writes `out` only when `tok` is a non-empty, unpadded,
// non-negative, fully-consumed base-10 integer within int range; otherwise
// returns false and leaves `out` untouched.
bool parse_component(const std::string &tok, int &out);
semver_t from_string(const std::string &version);
String render_to_string(const semver_t &version);
std::vector<std::string> split(const std::string &s, char delim);

bool operator>(const semver_t &x, const semver_t &y);

#endif
