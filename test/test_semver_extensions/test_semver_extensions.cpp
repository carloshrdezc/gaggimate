// Arduino.h (the test/native shim) provides ::String, and <vector> backs the
// std::vector return type — both are referenced by semver_extensions.h, which
// (matching semver_extensions.cpp) relies on its includer to pull them in.
#include <Arduino.h>
#include <unity.h>

#include <string>
#include <vector>

#include "semver.h"
#include "semver_extensions.h"

// Native host regression test for the OTA semver helper `from_string`
// (lib/OTA/src/semver_extensions.cpp), wired into [env:native] via
// build_src_filter so it exercises the real production code (PRO-264).
//
// `from_string` parses a (possibly 'v'-prefixed) version tag into a semver_t.
// The PRO-20 fix (PR #240) made it strip semver build metadata ("+meta",
// the trailing component that does not affect precedence) BEFORE splitting on
// '.' / '-', so a "+build.5" suffix can no longer leak into the patch token or
// the prerelease. There was zero coverage of that behavior; this pins it.
//
// semver_t (lib/OTA/src/semver.h): { int major; int minor; int patch;
// char *metadata; char *prerelease; }. `from_string` always leaves metadata
// NULL and sets prerelease to either NULL (no prerelease) or a malloc'd
// C-string. We free with the library's semver_free() to stay ASan-clean.

void setUp(void) {}
void tearDown(void) {}

// 1.2.3+abc -> 1,2,3, no prerelease (build metadata stripped).
void test_build_metadata_simple(void) {
    semver_t v = from_string("1.2.3+abc");
    TEST_ASSERT_EQUAL_INT(1, v.major);
    TEST_ASSERT_EQUAL_INT(2, v.minor);
    TEST_ASSERT_EQUAL_INT(3, v.patch);
    TEST_ASSERT_NULL(v.prerelease);
    semver_free(&v);
}

// 1.2.3+build.5 -> 1,2,3, no prerelease. The dotted metadata must not be
// mistaken for an extra version segment.
void test_build_metadata_dotted(void) {
    semver_t v = from_string("1.2.3+build.5");
    TEST_ASSERT_EQUAL_INT(1, v.major);
    TEST_ASSERT_EQUAL_INT(2, v.minor);
    TEST_ASSERT_EQUAL_INT(3, v.patch);
    TEST_ASSERT_NULL(v.prerelease);
    semver_free(&v);
}

// PRO-20 regression: 1.2.3-rc1+build.5 -> 1,2,3, prerelease == "rc1".
// Metadata must be stripped while the prerelease is preserved.
void test_prerelease_with_build_metadata(void) {
    semver_t v = from_string("1.2.3-rc1+build.5");
    TEST_ASSERT_EQUAL_INT(1, v.major);
    TEST_ASSERT_EQUAL_INT(2, v.minor);
    TEST_ASSERT_EQUAL_INT(3, v.patch);
    TEST_ASSERT_NOT_NULL(v.prerelease);
    TEST_ASSERT_EQUAL_STRING("rc1", v.prerelease);
    semver_free(&v);
}

// Leading-'v' strip: "v2.0.0" and "2.0.0" both parse to 2,0,0.
void test_leading_v_stripped(void) {
    semver_t with_v = from_string("v2.0.0");
    TEST_ASSERT_EQUAL_INT(2, with_v.major);
    TEST_ASSERT_EQUAL_INT(0, with_v.minor);
    TEST_ASSERT_EQUAL_INT(0, with_v.patch);
    TEST_ASSERT_NULL(with_v.prerelease);
    semver_free(&with_v);

    semver_t without_v = from_string("2.0.0");
    TEST_ASSERT_EQUAL_INT(2, without_v.major);
    TEST_ASSERT_EQUAL_INT(0, without_v.minor);
    TEST_ASSERT_EQUAL_INT(0, without_v.patch);
    TEST_ASSERT_NULL(without_v.prerelease);
    semver_free(&without_v);
}

// Prerelease without build metadata is preserved: 1.2.3-rc1 -> 1,2,3,"rc1".
void test_prerelease_preserved(void) {
    semver_t v = from_string("1.2.3-rc1");
    TEST_ASSERT_EQUAL_INT(1, v.major);
    TEST_ASSERT_EQUAL_INT(2, v.minor);
    TEST_ASSERT_EQUAL_INT(3, v.patch);
    TEST_ASSERT_NOT_NULL(v.prerelease);
    TEST_ASSERT_EQUAL_STRING("rc1", v.prerelease);
    semver_free(&v);
}

// PRO-376: a fully non-numeric tag ("abc.def.ghi") is a deliberate parse
// failure. The old unchecked atoi() silently coerced each token to 0; the
// checked strtol parse now returns the {0,0,0,nullptr,nullptr} sentinel used
// for empty / too-few-component input. (0,0,0 is the sentinel here, distinct
// from a real "0.0.0" tag only in that prerelease is NULL, which it is.)
void test_non_numeric_components_return_zero(void) {
    semver_t v = from_string("abc.def.ghi");
    TEST_ASSERT_EQUAL_INT(0, v.major);
    TEST_ASSERT_EQUAL_INT(0, v.minor);
    TEST_ASSERT_EQUAL_INT(0, v.patch);
    TEST_ASSERT_NULL(v.prerelease);
    semver_free(&v);
}

// PRO-376: trailing garbage on a component ("1.2.3x") is rejected — the whole
// token must be consumed by strtol, so this returns the sentinel too.
void test_trailing_garbage_patch_returns_zero(void) {
    semver_t v = from_string("1.2.3x");
    TEST_ASSERT_EQUAL_INT(0, v.major);
    TEST_ASSERT_EQUAL_INT(0, v.minor);
    TEST_ASSERT_EQUAL_INT(0, v.patch);
    TEST_ASSERT_NULL(v.prerelease);
    semver_free(&v);
}

// PRO-376: a malformed patch on the prerelease path ("1.2.x-rc1") must still
// return the sentinel AND must not leak the prerelease malloc (validated under
// [env:native-sanitize] ASan). The prerelease of the sentinel is NULL.
void test_malformed_patch_with_prerelease_returns_zero(void) {
    semver_t v = from_string("1.2.x-rc1");
    TEST_ASSERT_EQUAL_INT(0, v.major);
    TEST_ASSERT_EQUAL_INT(0, v.minor);
    TEST_ASSERT_EQUAL_INT(0, v.patch);
    TEST_ASSERT_NULL(v.prerelease);
    semver_free(&v);
}

// PRO-387: leading whitespace on a core component ("  12.0.0", "1. 2.0") is
// rejected. strtol would skip leading whitespace and accept "  12" as 12; the
// digit-only lead-char precheck now returns the sentinel instead.
void test_leading_whitespace_component_returns_zero(void) {
    semver_t a = from_string("  12.0.0");
    TEST_ASSERT_EQUAL_INT(0, a.major);
    TEST_ASSERT_EQUAL_INT(0, a.minor);
    TEST_ASSERT_EQUAL_INT(0, a.patch);
    TEST_ASSERT_NULL(a.prerelease);
    semver_free(&a);

    semver_t b = from_string("1. 2.0");
    TEST_ASSERT_EQUAL_INT(0, b.major);
    TEST_ASSERT_EQUAL_INT(0, b.minor);
    TEST_ASSERT_EQUAL_INT(0, b.patch);
    TEST_ASSERT_NULL(b.prerelease);
    semver_free(&b);
}

// PRO-387: a leading '-' sign on any core component ("-1.0.0", "1.-1.0",
// "1.0.-1") is rejected. strtol would parse "-1" as a negative value; semver
// core components must be non-negative, so these return the sentinel.
void test_negative_sign_component_returns_zero(void) {
    semver_t a = from_string("-1.0.0");
    TEST_ASSERT_EQUAL_INT(0, a.major);
    TEST_ASSERT_EQUAL_INT(0, a.minor);
    TEST_ASSERT_EQUAL_INT(0, a.patch);
    TEST_ASSERT_NULL(a.prerelease);
    semver_free(&a);

    semver_t b = from_string("1.-1.0");
    TEST_ASSERT_EQUAL_INT(0, b.major);
    TEST_ASSERT_EQUAL_INT(0, b.minor);
    TEST_ASSERT_EQUAL_INT(0, b.patch);
    TEST_ASSERT_NULL(b.prerelease);
    semver_free(&b);

    semver_t c = from_string("1.0.-1");
    TEST_ASSERT_EQUAL_INT(0, c.major);
    TEST_ASSERT_EQUAL_INT(0, c.minor);
    TEST_ASSERT_EQUAL_INT(0, c.patch);
    TEST_ASSERT_NULL(c.prerelease);
    semver_free(&c);
}

// PRO-387: a leading '+' sign on a core component ("+1.0.0", "1.+1.0") is
// rejected. Note this differs from the "+build.5" metadata case (that '+'
// starts the metadata suffix on the WHOLE version and is stripped up front);
// here the '+' is the lead char of a numeric component and must be rejected.
void test_positive_sign_component_returns_zero(void) {
    semver_t a = from_string("+1.0.0");
    TEST_ASSERT_EQUAL_INT(0, a.major);
    TEST_ASSERT_EQUAL_INT(0, a.minor);
    TEST_ASSERT_EQUAL_INT(0, a.patch);
    TEST_ASSERT_NULL(a.prerelease);
    semver_free(&a);

    semver_t b = from_string("1.+1.0");
    TEST_ASSERT_EQUAL_INT(0, b.major);
    TEST_ASSERT_EQUAL_INT(0, b.minor);
    TEST_ASSERT_EQUAL_INT(0, b.patch);
    TEST_ASSERT_NULL(b.prerelease);
    semver_free(&b);
}

// PRO-387 positive control: a normal, unpadded tag ("2.0.0", "v2.0.0") still
// parses unchanged after the lead-char precheck was added.
void test_unpadded_tag_still_parses(void) {
    semver_t a = from_string("2.0.0");
    TEST_ASSERT_EQUAL_INT(2, a.major);
    TEST_ASSERT_EQUAL_INT(0, a.minor);
    TEST_ASSERT_EQUAL_INT(0, a.patch);
    TEST_ASSERT_NULL(a.prerelease);
    semver_free(&a);

    semver_t b = from_string("v2.0.0");
    TEST_ASSERT_EQUAL_INT(2, b.major);
    TEST_ASSERT_EQUAL_INT(0, b.minor);
    TEST_ASSERT_EQUAL_INT(0, b.patch);
    TEST_ASSERT_NULL(b.prerelease);
    semver_free(&b);
}

static int runSemverExtensionsTests() {
    UNITY_BEGIN();
    RUN_TEST(test_build_metadata_simple);
    RUN_TEST(test_build_metadata_dotted);
    RUN_TEST(test_prerelease_with_build_metadata);
    RUN_TEST(test_leading_v_stripped);
    RUN_TEST(test_prerelease_preserved);
    RUN_TEST(test_non_numeric_components_return_zero);
    RUN_TEST(test_trailing_garbage_patch_returns_zero);
    RUN_TEST(test_malformed_patch_with_prerelease_returns_zero);
    RUN_TEST(test_leading_whitespace_component_returns_zero);
    RUN_TEST(test_negative_sign_component_returns_zero);
    RUN_TEST(test_positive_sign_component_returns_zero);
    RUN_TEST(test_unpadded_tag_still_parses);
    return UNITY_END();
}

#ifdef ARDUINO
void setup() { runSemverExtensionsTests(); }

void loop() {}
#else
int main(int argc, char **argv) { return runSemverExtensionsTests(); }
#endif
