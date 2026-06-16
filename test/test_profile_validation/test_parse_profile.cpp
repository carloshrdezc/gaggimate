#include <ArduinoJson.h>
#include <unity.h>

#include <display/core/utils.h>
#include <display/models/profile.h>

void setUp(void) {}
void tearDown(void) {}

static bool parseProfileJson(const char *json, Profile &profile) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    TEST_ASSERT_FALSE(err);
    return parseProfile(doc.as<JsonObject>(), profile);
}

void test_rejects_missing_label(void) {
    Profile profile;
    TEST_ASSERT_FALSE(parseProfileJson(
        R"({"type":"standard","phases":[{"name":"Brew","phase":"brew","valve":1,"duration":25,"pump":100}]})", profile));
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

void test_sanitizes_unsafe_id(void) {
    Profile profile;
    // An unsafe ID (path separators, commas, etc.) must never survive into
    // profile.id, since it later feeds filesystem helpers. parseProfile does
    // NOT reject the whole profile over it -- it rescues the profile by blanking
    // the id (saveProfile regenerates one on next save). See profile.h.
    TEST_ASSERT_TRUE(parseProfileJson(
        R"({"id":"bad,id","label":"Bad","type":"standard","phases":[{"name":"Brew","phase":"brew","valve":1,"duration":25,"pump":100}]})",
        profile));
    TEST_ASSERT_TRUE(profile.id.isEmpty());
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

void test_accepts_valid_pro_profile_with_advanced_pump(void) {
    Profile profile;
    // Pro profiles carry an advanced pump control object ({target, pressure,
    // flow}) instead of a simple integer pump percentage, plus a transition
    // and explicit targets. Verify the parser routes all of that correctly.
    TEST_ASSERT_TRUE(
        parseProfileJson(R"({"label":"Pro","type":"pro","temperature":94,"phases":[{"name":"Ramp","phase":"preinfusion",)"
                         R"("valve":1,"duration":10,"temperature":92,"pump":{"target":"pressure","pressure":3,"flow":0},)"
                         R"("transition":{"type":"linear","duration":2,"adaptive":1},)"
                         R"("targets":[{"type":"pressure","operator":"gte","value":4}]}]})",
                         profile));
    TEST_ASSERT_EQUAL_STRING("pro", profile.type.c_str());
    TEST_ASSERT_EQUAL_UINT(1, profile.phases.size());
    const Phase &phase = profile.phases[0];
    TEST_ASSERT_FALSE(phase.pumpIsSimple);
    TEST_ASSERT_TRUE(phase.pumpAdvanced.target == PumpTarget::PUMP_TARGET_PRESSURE);
    TEST_ASSERT_EQUAL_FLOAT(3.0f, phase.pumpAdvanced.pressure);
    TEST_ASSERT_TRUE(phase.transition.type == TransitionType::LINEAR);
    TEST_ASSERT_TRUE(phase.transition.adaptive);
    TEST_ASSERT_TRUE(phase.phase == PhaseType::PHASE_TYPE_PREINFUSION);
    TEST_ASSERT_EQUAL_UINT(1, phase.targets.size());
    TEST_ASSERT_TRUE(phase.targets[0].type == TargetType::TARGET_TYPE_PRESSURE);
}

void test_phase_temperature_override_and_default(void) {
    Profile profile;
    // A phase may omit its own temperature, in which case the parser stores
    // 0.0 (the "no per-phase override" sentinel) and the brew controller falls
    // back to the profile-level temperature. A phase MAY also pin an explicit
    // temperature; both must round-trip through parsing.
    TEST_ASSERT_TRUE(
        parseProfileJson(R"({"label":"Temps","type":"standard","temperature":93,"phases":[)"
                         R"({"name":"NoOverride","phase":"brew","valve":1,"duration":20,"pump":100},)"
                         R"({"name":"Override","phase":"brew","valve":1,"duration":5,"temperature":88,"pump":100}]})",
                         profile));
    TEST_ASSERT_EQUAL_UINT(2, profile.phases.size());
    TEST_ASSERT_EQUAL_FLOAT(0.0f, profile.phases[0].temperature);
    TEST_ASSERT_EQUAL_FLOAT(88.0f, profile.phases[1].temperature);
    TEST_ASSERT_EQUAL_FLOAT(93.0f, profile.temperature);
}

void test_volumetric_target_and_total_volume(void) {
    Profile profile;
    TEST_ASSERT_TRUE(
        parseProfileJson(R"({"label":"Vol","type":"standard","temperature":93,"phases":[{"name":"Brew","phase":"brew",)"
                         R"("valve":1,"duration":30,"pump":100,"targets":[{"type":"volumetric","operator":"gte","value":36}]}]})",
                         profile));
    TEST_ASSERT_TRUE(profile.isVolumetric());
    TEST_ASSERT_EQUAL_FLOAT(36.0f, profile.getTotalVolume());
    TEST_ASSERT_EQUAL_FLOAT(30.0f, profile.getTotalDuration());
}

void test_resolve_id_prefers_safe_infile_id(void) {
    // A safe in-file id always wins, regardless of the filename stem.
    TEST_ASSERT_EQUAL_STRING("abc123", resolveAddressableProfileId("abc123", "other").c_str());
    TEST_ASSERT_EQUAL_STRING("abc123", resolveAddressableProfileId("abc123", "").c_str());
}

void test_resolve_id_falls_back_to_safe_stem_when_infile_empty(void) {
    // No in-file id but a safe filename stem -> adopt the stem.
    TEST_ASSERT_EQUAL_STRING("stem01", resolveAddressableProfileId("", "stem01").c_str());
}

void test_resolve_id_unaddressable_for_uuid(void) {
    // CAR-335: a 36-char UUID id (parseProfile blanks it, so in-file arrives
    // empty here) whose filename stem is the same UUID has NO safe address.
    // resolveAddressableProfileId must report this as unaddressable (empty),
    // which is the signal ProfileManager::setup uses to remint the entry.
    const char *uuid = "387ff201-e3de-4102-a071-0a663b08066a";
    TEST_ASSERT_TRUE(resolveAddressableProfileId("", uuid).isEmpty());
    // Even if the unsafe id somehow survived in-file, it is still unaddressable.
    TEST_ASSERT_TRUE(resolveAddressableProfileId(uuid, uuid).isEmpty());
}

void test_resolve_id_unaddressable_when_both_unsafe(void) {
    // Unsafe in-file id and unsafe stem (e.g. path-traversal chars) -> empty.
    TEST_ASSERT_TRUE(resolveAddressableProfileId("bad,id", "../evil").isEmpty());
    // An over-length (>32) but otherwise-charset-clean stem is also unsafe.
    String longStem;
    for (int i = 0; i < 40; ++i) {
        longStem += 'a';
    }
    TEST_ASSERT_TRUE(resolveAddressableProfileId("", longStem).isEmpty());
}

static int runProfileValidationTests() {
    UNITY_BEGIN();
    RUN_TEST(test_rejects_missing_label);
    RUN_TEST(test_rejects_unknown_type);
    RUN_TEST(test_rejects_missing_phases);
    RUN_TEST(test_rejects_empty_phases);
    RUN_TEST(test_sanitizes_unsafe_id);
    RUN_TEST(test_accepts_valid_standard_profile);
    RUN_TEST(test_accepts_valid_pro_profile_with_advanced_pump);
    RUN_TEST(test_phase_temperature_override_and_default);
    RUN_TEST(test_volumetric_target_and_total_volume);
    RUN_TEST(test_resolve_id_prefers_safe_infile_id);
    RUN_TEST(test_resolve_id_falls_back_to_safe_stem_when_infile_empty);
    RUN_TEST(test_resolve_id_unaddressable_for_uuid);
    RUN_TEST(test_resolve_id_unaddressable_when_both_unsafe);
    return UNITY_END();
}

#ifdef ARDUINO
void setup() { runProfileValidationTests(); }

void loop() {}
#else
int main(int argc, char **argv) { return runProfileValidationTests(); }
#endif
