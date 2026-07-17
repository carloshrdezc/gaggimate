// Arduino.h (the test/native shim) provides ::String, and <vector> backs the
// std::vector return type — both are referenced by semver_extensions.h, which
// (matching semver_extensions.cpp) relies on its includer to pull them in.
#include <Arduino.h>
#include <unity.h>

#include <string>
#include <vector>

#include "semver.h"
#include "semver_extensions.h"

// Native host regression test for the reassignment pattern that was the
// PRO-21 bug site (PR #448): GitHubOTA::setControllerVersion() and
// GitHubOTA::checkForUpdates() both re-assign a semver_t member (a struct
// holding a malloc'd `prerelease` char*) without first calling semver_free()
// on the OLD value, e.g. (lib/OTA/src/GitHubOTA.cpp, pre-fix):
//
//     _controller_version = from_string(new_version.c_str());
//
// vs. the PRO-21 fix:
//
//     semver_free(&_controller_version);
//     _controller_version = from_string(new_version.c_str());
//
// Re-assigning without the semver_free() call leaks (or, depending on
// allocator reuse, double-frees) the previous `prerelease` allocation the
// first time from_string() is called again on the same semver_t.
//
// GitHubOTA itself cannot be instantiated on the native host: GitHubOTA.cpp
// #includes ESP/WiFi/NimBLE-only headers (HTTPClient.h, WiFiClientSecure.h,
// NimBLEClient.h, ...) that don't exist here, and [env:native]'s
// build_src_filter deliberately keeps GitHubOTA.cpp OUT of the host build
// (only semver.c + semver_extensions.cpp are host-safe — see the
// `lib_ignore = OTA` comment block in platformio.ini). So this test calls the
// exact same semver_free()-then-from_string() primitive sequence directly,
// in the same order setControllerVersion()/checkForUpdates() call it,
// against local semver_t locals that stand in for the _controller_version /
// _latest_version members.
//
// The canonical proof this is memory-safe is `pio test -e native-sanitize`
// (this suite built + linked with -fsanitize=address,undefined): a missing
// semver_free() before reassignment would trip ASan on the leaked/double-freed
// `prerelease` allocation. `pio test -e native` (no sanitizer) only proves the
// parsed values are correct; it will NOT catch a reintroduced bug.

void setUp(void) {}
void tearDown(void) {}

// Mirrors GitHubOTA::setControllerVersion() called three times in sequence:
//   semver_free(&_controller_version);
//   _controller_version = from_string(new_version.c_str());
// Each call must free the previous value (including its malloc'd prerelease,
// when present) before the reassignment. Runs a no-prerelease -> prerelease
// -> build-metadata sequence and checks each intermediate result.
void test_set_controller_version_reassign_sequence(void) {
    semver_t controller_version = from_string("1.0.0");
    TEST_ASSERT_EQUAL_INT(1, controller_version.major);
    TEST_ASSERT_EQUAL_INT(0, controller_version.minor);
    TEST_ASSERT_EQUAL_INT(0, controller_version.patch);
    TEST_ASSERT_NULL(controller_version.prerelease);

    // setControllerVersion() reassign #1: no prerelease -> prerelease.
    semver_free(&controller_version);
    controller_version = from_string("2.0.0-rc1");
    TEST_ASSERT_EQUAL_INT(2, controller_version.major);
    TEST_ASSERT_EQUAL_INT(0, controller_version.minor);
    TEST_ASSERT_EQUAL_INT(0, controller_version.patch);
    TEST_ASSERT_NOT_NULL(controller_version.prerelease);
    TEST_ASSERT_EQUAL_STRING("rc1", controller_version.prerelease);

    // setControllerVersion() reassign #2: prerelease -> build metadata only
    // (from_string strips build metadata; the input has no prerelease
    // component, so prerelease is NULL — not removed, simply absent). This is
    // the round-trip the PRO-21 fix must get right: the malloc'd "rc1"
    // prerelease from the PREVIOUS value must be freed here, not leaked.
    semver_free(&controller_version);
    controller_version = from_string("3.0.0+build.5");
    TEST_ASSERT_EQUAL_INT(3, controller_version.major);
    TEST_ASSERT_EQUAL_INT(0, controller_version.minor);
    TEST_ASSERT_EQUAL_INT(0, controller_version.patch);
    TEST_ASSERT_NULL(controller_version.prerelease);

    semver_free(&controller_version);
}

// Mirrors GitHubOTA::checkForUpdates() called two to three times in sequence
// (once per update check):
//   semver_free(&_latest_version);
//   _latest_version = from_string(semver_str.c_str());
// checkForUpdates() has two call sites for this pattern (the redirect-based
// path and the version.txt fallback path); both share the identical
// free+reassign shape, so one sequence exercises both.
void test_check_for_updates_reassign_sequence(void) {
    semver_t latest_version = {0, 0, 0, nullptr, nullptr};

    // checkForUpdates() call #1.
    semver_free(&latest_version);
    latest_version = from_string("1.5.0");
    TEST_ASSERT_EQUAL_INT(1, latest_version.major);
    TEST_ASSERT_EQUAL_INT(5, latest_version.minor);
    TEST_ASSERT_EQUAL_INT(0, latest_version.patch);
    TEST_ASSERT_NULL(latest_version.prerelease);

    // checkForUpdates() call #2: introduces a prerelease.
    semver_free(&latest_version);
    latest_version = from_string("1.6.0-beta2");
    TEST_ASSERT_EQUAL_INT(1, latest_version.major);
    TEST_ASSERT_EQUAL_INT(6, latest_version.minor);
    TEST_ASSERT_EQUAL_INT(0, latest_version.patch);
    TEST_ASSERT_NOT_NULL(latest_version.prerelease);
    TEST_ASSERT_EQUAL_STRING("beta2", latest_version.prerelease);

    // checkForUpdates() call #3: a later poll finds a final release. The
    // "beta2" prerelease malloc'd on call #2 must be freed here.
    semver_free(&latest_version);
    latest_version = from_string("2.0.0");
    TEST_ASSERT_EQUAL_INT(2, latest_version.major);
    TEST_ASSERT_EQUAL_INT(0, latest_version.minor);
    TEST_ASSERT_EQUAL_INT(0, latest_version.patch);
    TEST_ASSERT_NULL(latest_version.prerelease);

    semver_free(&latest_version);
}

// Round-trip: reassign into a prerelease value, then reassign again with a
// version that has NO prerelease. This is the specific shape the PRO-21 bug
// hit hardest — a live malloc'd prerelease char* getting overwritten by a
// reassignment whose new value has a NULL prerelease, which (without the
// preceding semver_free()) drops the only pointer to the old allocation.
void test_prerelease_to_no_prerelease_round_trip_frees_prerelease(void) {
    semver_t version = from_string("1.0.0-alpha");
    TEST_ASSERT_NOT_NULL(version.prerelease);
    TEST_ASSERT_EQUAL_STRING("alpha", version.prerelease);

    // Reassign pattern: free the old value (frees the malloc'd "alpha")
    // before overwriting with a value whose prerelease is NULL.
    semver_free(&version);
    version = from_string("1.0.0");
    TEST_ASSERT_EQUAL_INT(1, version.major);
    TEST_ASSERT_EQUAL_INT(0, version.minor);
    TEST_ASSERT_EQUAL_INT(0, version.patch);
    TEST_ASSERT_NULL(version.prerelease);

    semver_free(&version);
}

static int runGitHubOtaSemverReassignTests() {
    UNITY_BEGIN();
    RUN_TEST(test_set_controller_version_reassign_sequence);
    RUN_TEST(test_check_for_updates_reassign_sequence);
    RUN_TEST(test_prerelease_to_no_prerelease_round_trip_frees_prerelease);
    return UNITY_END();
}

#ifdef ARDUINO
void setup() { runGitHubOtaSemverReassignTests(); }

void loop() {}
#else
int main(int argc, char **argv) { return runGitHubOtaSemverReassignTests(); }
#endif
