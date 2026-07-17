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
                         R"("transition":{"type":"linear","duration":2,"adaptive":true},)"
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

void test_remint_decision_safe_stem_unsafe_infile_id_is_addressable(void) {
    // CAR-335 regression guard (review finding): a profile whose in-file id is
    // an unsafe 36-char UUID but whose FILENAME STEM is safe (e.g.
    // "imported.json") is ALREADY addressable today -- listProfiles enumerates
    // it by the stem and loadProfile adopts the stem after parseProfile blanks
    // the unsafe id. The remint scan must therefore NOT remint it.
    //
    // This models the exact decision remintUnsafeProfileIds() makes: parse the
    // file (which blanks the unsafe id), then resolve against the stem. The bug
    // was passing the RAW pre-parse id to the resolver, which returned empty and
    // triggered a needless remint. Passing the POST-parse id must keep the
    // entry addressable (non-empty -> skip remint).
    Profile profile;
    TEST_ASSERT_TRUE(parseProfileJson(R"({"id":"387ff201-e3de-4102-a071-0a663b08066a","label":"Imported","type":"standard",)"
                                      R"("phases":[{"name":"Brew","phase":"brew","valve":1,"duration":25,"pump":100}]})",
                                      profile));
    // parseProfile blanked the unsafe in-file id.
    TEST_ASSERT_TRUE(profile.id.isEmpty());
    // With the safe filename stem, the entry resolves -> addressable -> NOT reminted.
    TEST_ASSERT_EQUAL_STRING("imported", resolveAddressableProfileId(profile.id, "imported").c_str());
}

void test_remint_decision_uuid_stem_and_uuid_id_is_unaddressable(void) {
    // CAR-335 core case: file named <uuid>.json whose in-file id is the same
    // UUID. parseProfile blanks the id; the stem is also unsafe (>32 chars), so
    // there is NO safe address -> the resolver returns empty -> remint fires.
    const char *uuid = "387ff201-e3de-4102-a071-0a663b08066a";
    Profile profile;
    TEST_ASSERT_TRUE(parseProfileJson(R"({"id":"387ff201-e3de-4102-a071-0a663b08066a","label":"Legacy","type":"standard",)"
                                      R"("phases":[{"name":"Brew","phase":"brew","valve":1,"duration":25,"pump":100}]})",
                                      profile));
    TEST_ASSERT_TRUE(profile.id.isEmpty());
    TEST_ASSERT_TRUE(resolveAddressableProfileId(profile.id, uuid).isEmpty());
}

void test_resolve_id_length_boundary(void) {
    // isSafeId accepts 1..32 chars. The remint decision hinges on this cliff:
    // a 32-char id/stem is addressable (NOT reminted); 33 chars is unaddressable
    // (reminted). A test using only a far-over-length value (e.g. 40) would not
    // catch an off-by-one in the length comparison, so pin both sides.
    String len32, len33;
    for (int i = 0; i < 32; ++i) {
        len32 += 'a';
    }
    len33 = len32 + 'a';
    // 32-char safe in-file id wins.
    TEST_ASSERT_EQUAL_STRING(len32.c_str(), resolveAddressableProfileId(len32, "").c_str());
    // 32-char safe stem (empty in-file id) is adopted.
    TEST_ASSERT_EQUAL_STRING(len32.c_str(), resolveAddressableProfileId("", len32).c_str());
    // 33 chars is unsafe on both axes -> unaddressable.
    TEST_ASSERT_TRUE(resolveAddressableProfileId(len33, len33).isEmpty());
    TEST_ASSERT_TRUE(resolveAddressableProfileId("", len33).isEmpty());
}

void test_resolve_id_both_empty_is_unaddressable(void) {
    // Empty in-file id AND empty stem -> no safe address (isSafeId rejects len 0).
    TEST_ASSERT_TRUE(resolveAddressableProfileId("", "").isEmpty());
}

void test_resolve_id_raw_unsafe_id_does_not_adopt_safe_stem(void) {
    // Pins the round-1 regression at the resolver contract: a NON-EMPTY but
    // unsafe in-file id does NOT fall through to the safe stem (the stem branch
    // is gated on inFileId.isEmpty()). This is exactly why the remint scan must
    // pass the POST-parse (blanked) id, not the raw on-disk id -- with the raw
    // UUID here the resolver returns empty even though the stem is safe.
    TEST_ASSERT_TRUE(resolveAddressableProfileId("387ff201-e3de-4102-a071-0a663b08066a", "imported").isEmpty());
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
    RUN_TEST(test_resolve_id_length_boundary);
    RUN_TEST(test_resolve_id_both_empty_is_unaddressable);
    RUN_TEST(test_resolve_id_raw_unsafe_id_does_not_adopt_safe_stem);
    RUN_TEST(test_remint_decision_safe_stem_unsafe_infile_id_is_addressable);
    RUN_TEST(test_remint_decision_uuid_stem_and_uuid_id_is_unaddressable);
    return UNITY_END();
}

#ifdef ARDUINO
void setup() { runProfileValidationTests(); }

void loop() {}
#else
int main(int argc, char **argv) { return runProfileValidationTests(); }
#endif
