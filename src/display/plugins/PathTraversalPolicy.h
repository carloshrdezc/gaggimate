#ifndef PATHTRAVERSALPOLICY_H
#define PATHTRAVERSALPOLICY_H

#include <string>

// Reject any path containing a ".." parent-directory SEGMENT, to prevent
// path traversal out of the WebUI document root (PRO-208). URL paths use '/'
// separators on every platform, but a URL-supplied segment like "..\\x" would be
// served verbatim and could traverse on a Windows host, where '\\' is also an OS
// path separator. So BOTH '/' and '\\' are treated as segment separators here
// (Windows-host hardening, Ref PRO-340 / PR #348 / PRO-208). A whole segment
// equal to ".." (bounded by either separator or the string ends) is the attack;
// an embedded ".." inside an otherwise normal filename (e.g. "foo..bar.js") is a
// legitimate asset and is allowed.
//
// PRO-362: extracted verbatim from the former sim-only static definition in
// sim/web/ESPAsyncWebServer.cpp so the exact same predicate is the SINGLE source
// of truth shared by the sim path-traversal guard (its sole caller) and the host
// unit tests (test/test_path_traversal_guard) under [env:native] /
// [env:native-sanitize]. This is a pure extraction — the body is byte-identical
// and the guard's behavior is unchanged. Header-only with no Arduino / network /
// FS dependencies, mirroring the sibling policy headers (WsReassemblyPolicy.h,
// SdReadRetryPolicy.h).
inline bool hasTraversalSegment(const std::string &p) {
    size_t start = 0;
    while (start <= p.size()) {
        size_t sep = p.find_first_of("/\\", start);
        size_t end = (sep == std::string::npos) ? p.size() : sep;
        if (end - start == 2 && p.compare(start, 2, "..") == 0)
            return true;
        if (sep == std::string::npos)
            break;
        start = sep + 1;
    }
    return false;
}

#endif // PATHTRAVERSALPOLICY_H
