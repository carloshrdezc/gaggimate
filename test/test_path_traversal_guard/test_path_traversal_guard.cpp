#include "display/plugins/PathTraversalPolicy.h"
#include <unity.h>

// PRO-362: host unit tests for hasTraversalSegment(), the WebUI path-traversal
// guard extracted verbatim from sim/web/ESPAsyncWebServer.cpp into
// src/display/plugins/PathTraversalPolicy.h (Ref PRO-340 / PR #348 / PRO-208).
//
// The guard rejects any path whose ".." forms a WHOLE parent-directory SEGMENT,
// treating BOTH '/' and '\\' as segment separators (Windows-host hardening), so
// a URL-supplied "..\\x" can't traverse on a host where '\\' is an OS separator.
// An embedded ".." inside an otherwise-normal filename (e.g. "foo..bar.js") is a
// legitimate asset and must be allowed. These tests pin that dual-separator
// attack/legit matrix so the guard's behavior can't silently drift.
//
// The guard was previously only verified via a throwaway g++ harness (PRO-340);
// this is the permanent regression suite under [env:native] / [env:native-sanitize].

void setUp(void) {}
void tearDown(void) {}

// --- REJECTED: a whole ".." segment is a traversal attempt (expect true) ------

// A bare "..", and ".." as the leading, trailing, or interior segment, bounded
// by '/' — the canonical POSIX traversal shapes.
void test_rejected_slash_separated(void) {
    TEST_ASSERT_TRUE(hasTraversalSegment(".."));       // whole path is ".."
    TEST_ASSERT_TRUE(hasTraversalSegment("../x"));     // leading segment
    TEST_ASSERT_TRUE(hasTraversalSegment("x/.."));     // trailing segment
    TEST_ASSERT_TRUE(hasTraversalSegment("a/b/../c")); // interior segment
}

// The Windows-host hardening: '\\' (a single backslash in the actual string) is
// ALSO a segment separator, so a ".." segment bounded by backslashes must be
// rejected too. In C++ source each backslash is escaped as "\\".
void test_rejected_backslash_separated(void) {
    TEST_ASSERT_TRUE(hasTraversalSegment("..\\x"));       // "..\x"   leading
    TEST_ASSERT_TRUE(hasTraversalSegment("x\\.."));       // "x\.."   trailing
    TEST_ASSERT_TRUE(hasTraversalSegment("a\\b\\..\\c")); // "a\b\..\c" interior
}

// --- ALLOWED: no whole ".." segment (expect false) ---------------------------

// ".." embedded inside a longer filename is a legitimate asset name, not a
// parent-directory reference — must NOT be rejected.
void test_allowed_embedded_dotdot(void) {
    TEST_ASSERT_FALSE(hasTraversalSegment("foo..bar.js")); // ".." inside a name
    TEST_ASSERT_FALSE(hasTraversalSegment("foo.min.js"));  // ordinary asset
    TEST_ASSERT_FALSE(hasTraversalSegment("index.html"));  // ordinary asset
    TEST_ASSERT_FALSE(hasTraversalSegment("..js"));        // 4-char segment, not ".."
    TEST_ASSERT_FALSE(hasTraversalSegment("a..b"));        // ".." bounded by non-seps
    TEST_ASSERT_FALSE(hasTraversalSegment("..."));         // 3-char segment, not ".."
    TEST_ASSERT_FALSE(hasTraversalSegment("...."));        // 4-char segment, not ".."
}

// Legitimate multi-segment paths with both separators, none of whose segments
// equal exactly "..".
void test_allowed_normal_paths(void) {
    TEST_ASSERT_FALSE(hasTraversalSegment("a/b/c.js")); // '/'-separated, clean
    TEST_ASSERT_FALSE(hasTraversalSegment("a\\b.js"));  // "a\b.js" '\'-sep, clean
}

static int runPathTraversalGuardTests() {
    UNITY_BEGIN();
    RUN_TEST(test_rejected_slash_separated);
    RUN_TEST(test_rejected_backslash_separated);
    RUN_TEST(test_allowed_embedded_dotdot);
    RUN_TEST(test_allowed_normal_paths);
    return UNITY_END();
}

#ifdef ARDUINO
void setup() { runPathTraversalGuardTests(); }

void loop() {}
#else
int main(int argc, char **argv) { return runPathTraversalGuardTests(); }
#endif
