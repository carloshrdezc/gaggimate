#include <unity.h>
#include <cstdio>
#include <string>
#include "display/core/constants.h"
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

void test_settings_validation_accepts_common_pressure_sensor_ratings() {
    strict_validation::Error error;
    // Regression for PRO-577: the firmware's own DEFAULT_PRESSURE_SCALING (16.0)
    // must pass validateSettings(). Previously the bound was capped at 10 bar,
    // locking out any user whose sensor is rated above 10 bar.
    char defaultRating[32];
    std::snprintf(defaultRating, sizeof(defaultRating), "%g", static_cast<double>(DEFAULT_PRESSURE_SCALING));
    TEST_ASSERT_TRUE(strict_validation::validateSettings({{"pressureScaling", std::string(defaultRating)}}, error));
    // Common espresso transducer bar ratings must all validate.
    TEST_ASSERT_TRUE(strict_validation::validateSettings({{"pressureScaling", "12"}}, error));
    TEST_ASSERT_TRUE(strict_validation::validateSettings({{"pressureScaling", "16"}}, error));
    TEST_ASSERT_TRUE(strict_validation::validateSettings({{"pressureScaling", "20"}}, error));
    TEST_ASSERT_TRUE(strict_validation::validateSettings({{"pressureScaling", "30"}}, error));
    // Above the range is still rejected.
    TEST_ASSERT_FALSE(strict_validation::validateSettings({{"pressureScaling", "31"}}, error));
    TEST_ASSERT_EQUAL_STRING("pressureScaling", error.field.c_str());
    TEST_ASSERT_FALSE(strict_validation::validateSettings({{"pressureScaling", "-1"}}, error));
}

void test_settings_validation_caps_display_brightness_at_hardware_ceiling() {
    strict_validation::Error error;
    // Regression for PRO-609: mainBrightness/standbyBrightness are display backlight
    // *levels*, and every supported panel family truncates them at 16 (LilyGo-T-RGB
    // `if (value > 16) value = 16;`, Waveshare `constrain(value, 0, WS_BACKLIGHT_MAX)`,
    // AmoledDisplay scales a 0..16 level by 16). The validator used to accept 0..255,
    // so a client bypassing the web form could persist a value NVS kept but no driver
    // ever honored.
    for (const char *field : {"mainBrightness", "standbyBrightness"}) {
        // The whole hardware range, plus the boundaries, must be accepted.
        for (int accepted = 0; accepted <= 16; ++accepted) {
            TEST_ASSERT_TRUE(strict_validation::validateSettings({{field, std::to_string(accepted)}}, error));
        }
        // Anything above the ceiling is rejected, and reports the offending field.
        for (int rejected : {17, 18, 100, 254, 255}) {
            TEST_ASSERT_FALSE(strict_validation::validateSettings({{field, std::to_string(rejected)}}, error));
            TEST_ASSERT_EQUAL_STRING(field, error.field.c_str());
        }
        // Below the range stays rejected too.
        TEST_ASSERT_FALSE(strict_validation::validateSettings({{field, "-1"}}, error));
        TEST_ASSERT_EQUAL_STRING(field, error.field.c_str());
    }
    // The firmware's own backlight defaults (Settings.h: mainBrightness = 16,
    // standbyBrightness = 8) must remain savable.
    TEST_ASSERT_TRUE(strict_validation::validateSettings({{"mainBrightness", "16"}, {"standbyBrightness", "8"}}, error));
}

void test_settings_validation_keeps_full_8bit_range_for_sunrise_channels() {
    strict_validation::Error error;
    // Counterpart to PRO-609: the sunrise channels are NOT backlight levels and must
    // keep the full 0..255 range. LedControlPlugin::sendControl forwards them verbatim
    // as uint8_t over BLE (emitting literal 255s of its own, and sending ext as
    // `255 - ext`), and the firmware defaults are sunriseB = 255 / sunriseExtBrightness
    // = 255 — capping these at 16 would make the firmware's own defaults unsavable.
    for (const char *field : {"sunriseR", "sunriseG", "sunriseB", "sunriseW", "sunriseExtBrightness"}) {
        for (int accepted : {0, 16, 17, 50, 128, 254, 255}) {
            TEST_ASSERT_TRUE(strict_validation::validateSettings({{field, std::to_string(accepted)}}, error));
        }
        // Still bounded at the 8-bit ceiling.
        TEST_ASSERT_FALSE(strict_validation::validateSettings({{field, "256"}}, error));
        TEST_ASSERT_EQUAL_STRING(field, error.field.c_str());
        TEST_ASSERT_FALSE(strict_validation::validateSettings({{field, "-1"}}, error));
        TEST_ASSERT_EQUAL_STRING(field, error.field.c_str());
    }
}

void test_websocket_validation_rejects_invalid_command_values() {
    strict_validation::Error error;
    TEST_ASSERT_FALSE(strict_validation::validateWebSocketRequest("req:dose:set", {{"grams", "nan", true}}, error));
    TEST_ASSERT_FALSE(strict_validation::validateWebSocketRequest("req:manual-grind:set", {{"value", "nan", true}}, error));
    TEST_ASSERT_FALSE(strict_validation::validateWebSocketRequest("req:manual-grind:set", {{"value", "150", true}}, error));
    TEST_ASSERT_FALSE(strict_validation::validateWebSocketRequest("req:brew-temperature:set", {{"temperature", "161", true}}, error));
    TEST_ASSERT_TRUE(strict_validation::validateWebSocketRequest("req:brew-temperature:set", {{"temperature", "93", true}}, error));
    TEST_ASSERT_FALSE(strict_validation::validateWebSocketRequest("req:manual-grind:set", {{"value", "-1", true}}, error));
    TEST_ASSERT_TRUE(strict_validation::validateWebSocketRequest("req:manual-grind:set", {{"value", "0", true}}, error));
    TEST_ASSERT_TRUE(strict_validation::validateWebSocketRequest("req:manual-grind:set", {{"value", "42.5", true}}, error));
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
    TEST_ASSERT_FALSE(strict_validation::validateWebSocketRequest("req:manual-grind:set", {{"value", "42"}}, error));
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
    TEST_ASSERT_FALSE(strict_validation::validateWebSocketRequest("req:manual-grind:set", {}, error));
    TEST_ASSERT_EQUAL_STRING("value", error.field.c_str());
    TEST_ASSERT_FALSE(strict_validation::validateWebSocketRequest("req:brew-temperature:set", {}, error));
    TEST_ASSERT_EQUAL_STRING("temperature", error.field.c_str());
    TEST_ASSERT_FALSE(strict_validation::validateWebSocketRequest("req:change-mode", {}, error));
    TEST_ASSERT_EQUAL_STRING("mode", error.field.c_str());
    TEST_ASSERT_TRUE(strict_validation::validateWebSocketRequest("req:manual:update", {}, error));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_integer_parser_rejects_partial_empty_and_non_finite);
    RUN_TEST(test_settings_validation_rejects_bad_enum_range_and_schedule_atomically);
    RUN_TEST(test_settings_validation_accepts_common_pressure_sensor_ratings);
    RUN_TEST(test_settings_validation_caps_display_brightness_at_hardware_ceiling);
    RUN_TEST(test_settings_validation_keeps_full_8bit_range_for_sunrise_channels);
    RUN_TEST(test_websocket_validation_rejects_invalid_command_values);
    RUN_TEST(test_websocket_validation_rejects_numeric_strings);
    RUN_TEST(test_websocket_validation_requires_command_fields);
    return UNITY_END();
}
