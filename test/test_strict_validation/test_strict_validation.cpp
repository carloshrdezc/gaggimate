#include <unity.h>
#include "display/plugins/StrictValidationPolicy.h"

void test_integer_parser_rejects_partial_empty_and_non_finite() {
    long integer = 0;
    double number = 0;
    TEST_ASSERT_TRUE(strict_validation::parseInteger("42", integer));
    TEST_ASSERT_EQUAL(42, integer);
    TEST_ASSERT_FALSE(strict_validation::parseInteger("42junk", integer));
    TEST_ASSERT_FALSE(strict_validation::parseInteger("", integer));
    TEST_ASSERT_TRUE(strict_validation::parseNumber("1.25", number));
    TEST_ASSERT_FALSE(strict_validation::parseNumber("1.25ms", number));
    TEST_ASSERT_FALSE(strict_validation::parseNumber("nan", number));
    TEST_ASSERT_FALSE(strict_validation::parseNumber("inf", number));
}

void test_settings_validation_rejects_bad_enum_range_and_schedule_atomically() {
    strict_validation::Error error;
    TEST_ASSERT_FALSE(strict_validation::validateSettings({{"targetSteamTemp", "150C"}}, error));
    TEST_ASSERT_EQUAL_STRING("targetSteamTemp", error.field.c_str());
    TEST_ASSERT_FALSE(strict_validation::validateSettings({{"smartGrindMode", "3"}}, error));
    TEST_ASSERT_FALSE(strict_validation::validateSettings({{"autowakeupSchedules", "25:00|1111111"}}, error));
    TEST_ASSERT_TRUE(strict_validation::validateSettings({{"targetSteamTemp", "155"}, {"pressureScaling", "1.5"}, {"autowakeupSchedules", "07:00|1111100"}}, error));
}

void test_websocket_validation_rejects_invalid_command_values() {
    strict_validation::Error error;
    TEST_ASSERT_FALSE(strict_validation::validateWebSocketRequest("req:dose:set", {{"grams", "nan"}}, error));
    TEST_ASSERT_FALSE(strict_validation::validateWebSocketRequest("req:change-mode", {{"mode", "6"}}, error));
    TEST_ASSERT_FALSE(strict_validation::validateWebSocketRequest("req:manual:update", {{"targetType", "wat"}}, error));
    TEST_ASSERT_TRUE(strict_validation::validateWebSocketRequest("req:manual:update", {{"targetType", "flow"}, {"pressure", "9.5"}}, error));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_integer_parser_rejects_partial_empty_and_non_finite);
    RUN_TEST(test_settings_validation_rejects_bad_enum_range_and_schedule_atomically);
    RUN_TEST(test_websocket_validation_rejects_invalid_command_values);
    return UNITY_END();
}
