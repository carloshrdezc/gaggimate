// PRO-669: host coverage for the legacy BLE client's required-characteristic
// guard (src/display/core/LegacyClientCharPolicy.h).
#include "../../src/display/core/LegacyClientCharPolicy.h"
#include <unity.h>

static LegacyClientChars all() {
    LegacyClientChars c;
    c.outputControl = c.sensor = c.ping = c.info = true;
    return c;
}

void setUp() {}
void tearDown() {}

void test_all_present_is_compatible() {
    TEST_ASSERT_TRUE(legacyClientIsCompatible(all()));
    TEST_ASSERT_EQUAL_STRING("", legacyClientMissingRequiredChars(all()).c_str());
}

void test_each_missing_is_named() {
    LegacyClientChars c = all();
    c.outputControl = false;
    TEST_ASSERT_FALSE(legacyClientIsCompatible(c));
    TEST_ASSERT_EQUAL_STRING("outputControl", legacyClientMissingRequiredChars(c).c_str());
    c = all();
    c.sensor = false;
    TEST_ASSERT_EQUAL_STRING("sensor", legacyClientMissingRequiredChars(c).c_str());
    c = all();
    c.ping = false;
    TEST_ASSERT_EQUAL_STRING("ping", legacyClientMissingRequiredChars(c).c_str());
    c = all();
    c.info = false;
    TEST_ASSERT_EQUAL_STRING("info", legacyClientMissingRequiredChars(c).c_str());
}

void test_none_present_lists_all() {
    TEST_ASSERT_EQUAL_STRING("outputControl, sensor, ping, info", legacyClientMissingRequiredChars(LegacyClientChars{}).c_str());
}

void test_retry_backoff() {
    TEST_ASSERT_TRUE(legacyClientRetryAllowed(false, 5, 0));
    TEST_ASSERT_FALSE(legacyClientRetryAllowed(true, 1000, 1000));
    TEST_ASSERT_FALSE(legacyClientRetryAllowed(true, 1000 + LEGACY_CLIENT_INCOMPATIBLE_BACKOFF_MS - 1, 1000));
    TEST_ASSERT_TRUE(legacyClientRetryAllowed(true, 1000 + LEGACY_CLIENT_INCOMPATIBLE_BACKOFF_MS, 1000));
    // millis() wrap
    TEST_ASSERT_FALSE(legacyClientRetryAllowed(true, 100, 0xFFFFFF00u));
    TEST_ASSERT_TRUE(legacyClientRetryAllowed(true, LEGACY_CLIENT_INCOMPATIBLE_BACKOFF_MS, 0xFFFFFF00u));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_all_present_is_compatible);
    RUN_TEST(test_each_missing_is_named);
    RUN_TEST(test_none_present_lists_all);
    RUN_TEST(test_retry_backoff);
    return UNITY_END();
}
