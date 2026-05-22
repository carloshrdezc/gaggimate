#include <ArduinoJson.h>
#include <unity.h>

#include <display/models/profile.h>

static bool parseProfileJson(const char *json, Profile &profile) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    TEST_ASSERT_FALSE(err);
    return parseProfile(doc.as<JsonObject>(), profile);
}

void test_rejects_missing_label(void) {
    Profile profile;
    TEST_ASSERT_FALSE(parseProfileJson(R"({"type":"standard","phases":[{"name":"Brew","phase":"brew","valve":1,"duration":25,"pump":100}]})",
                                       profile));
}

void test_rejects_unknown_type(void) {
    Profile profile;
    TEST_ASSERT_FALSE(parseProfileJson(
        R"({"label":"Bad","type":"turbo","phases":[{"name":"Brew","phase":"brew","valve":1,"duration":25,"pump":100}]})",
        profile));
}

void test_rejects_missing_phases(void) {
    Profile profile;
    TEST_ASSERT_FALSE(parseProfileJson(R"({"label":"Bad","type":"standard"})", profile));
}

void test_rejects_empty_phases(void) {
    Profile profile;
    TEST_ASSERT_FALSE(parseProfileJson(R"({"label":"Bad","type":"standard","phases":[]})", profile));
}

void test_rejects_unsafe_id(void) {
    Profile profile;
    TEST_ASSERT_FALSE(parseProfileJson(
        R"({"id":"bad,id","label":"Bad","type":"standard","phases":[{"name":"Brew","phase":"brew","valve":1,"duration":25,"pump":100}]})",
        profile));
}

void test_accepts_valid_standard_profile(void) {
    Profile profile;
    TEST_ASSERT_TRUE(parseProfileJson(
        R"({"label":"Good","type":"standard","temperature":93,"phases":[{"name":"Brew","phase":"brew","valve":1,"duration":25,"pump":100}]})",
        profile));
    TEST_ASSERT_EQUAL_STRING("Good", profile.label.c_str());
    TEST_ASSERT_EQUAL_STRING("standard", profile.type.c_str());
    TEST_ASSERT_EQUAL_UINT(1, profile.phases.size());
}

void setup() {
    UNITY_BEGIN();
    RUN_TEST(test_rejects_missing_label);
    RUN_TEST(test_rejects_unknown_type);
    RUN_TEST(test_rejects_missing_phases);
    RUN_TEST(test_rejects_empty_phases);
    RUN_TEST(test_rejects_unsafe_id);
    RUN_TEST(test_accepts_valid_standard_profile);
    UNITY_END();
}

void loop() {}
