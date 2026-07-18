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
    TEST_ASSERT_FALSE(strict_validation::validateWebSocketRequest("req:dose:set", {{"grams", "nan", true}}, error));
    TEST_ASSERT_FALSE(strict_validation::validateWebSocketRequest("req:change-mode", {{"mode", "6", true}}, error));
    TEST_ASSERT_FALSE(strict_validation::validateWebSocketRequest("req:manual:update", {{"targetType", "wat"}}, error));
    TEST_ASSERT_TRUE(strict_validation::validateWebSocketRequest("req:manual:update", {{"targetType", "flow"}, {"pressure", "9.5", true}}, error));
}

void test_websocket_validation_rejects_numeric_strings() {
    strict_validation::Error error;
    TEST_ASSERT_FALSE(strict_validation::validateWebSocketRequest("req:change-grind-target", {{"target", "1"}}, error));
    TEST_ASSERT_FALSE(strict_validation::validateWebSocketRequest("req:change-mode", {{"mode", "1"}}, error));
    TEST_ASSERT_FALSE(strict_validation::validateWebSocketRequest("req:change-brew-target", {{"target", "18"}}, error));
    TEST_ASSERT_FALSE(strict_validation::validateWebSocketRequest("req:dose:set", {{"grams", "18"}}, error));
    TEST_ASSERT_FALSE(strict_validation::validateWebSocketRequest("req:manual:update", {{"pressure", "9"}}, error));
    TEST_ASSERT_FALSE(strict_validation::validateWebSocketRequest("req:manual:update", {{"flow", "9"}}, error));
    TEST_ASSERT_FALSE(strict_validation::validateWebSocketRequest("req:manual:update", {{"temperature", "90"}}, error));
    TEST_ASSERT_FALSE(strict_validation::validateWebSocketRequest("req:autotune-start", {{"time", "60"}}, error));
    TEST_ASSERT_FALSE(strict_validation::validateWebSocketRequest("req:autotune-start", {{"samples", "10"}}, error));
    TEST_ASSERT_FALSE(strict_validation::validateWebSocketRequest("req:manual:update", {{"targetType", "flow", true}}, error));
}

void test_websocket_validation_requires_command_fields() {
    strict_validation::Error error;
    TEST_ASSERT_FALSE(strict_validation::validateWebSocketRequest("req:dose:set", {}, error));
    TEST_ASSERT_EQUAL_STRING("grams", error.field.c_str());
    TEST_ASSERT_FALSE(strict_validation::validateWebSocketRequest("req:change-mode", {}, error));
    TEST_ASSERT_EQUAL_STRING("mode", error.field.c_str());
    TEST_ASSERT_TRUE(strict_validation::validateWebSocketRequest("req:manual:update", {}, error));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_integer_parser_rejects_partial_empty_and_non_finite);
    RUN_TEST(test_settings_validation_rejects_bad_enum_range_and_schedule_atomically);
    RUN_TEST(test_websocket_validation_rejects_invalid_command_values);
    RUN_TEST(test_websocket_validation_rejects_numeric_strings);
    RUN_TEST(test_websocket_validation_requires_command_fields);
    return UNITY_END();
}
