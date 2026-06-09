#include <ArduinoJson.h>
#include <unity.h>

#include <display/models/profile.h>

// Pins the dashboard YIELD override behavior (CAR-355 / CAR-252).
//
// Profile::setVolumetricTarget(grams) implements the Home dashboard YIELD
// slider: it rescales the profile's volumetric stop target(s) in memory so the
// shot stops at the requested cumulative weight. The bug fixed in CAR-355 was
// that the setter only scaled phases tagged PHASE_TYPE_BREW while the
// denominator (getTotalVolume) and the stop logic (Phase::isFinished) read ALL
// volumetric phases — so a stop target living on a non-brew phase was silently
// skipped and the shot kept stopping at the saved profile value.

void setUp(void) {}
void tearDown(void) {}

static bool parseProfileJson(const char *json, Profile &profile) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    TEST_ASSERT_FALSE(err);
    return parseProfile(doc.as<JsonObject>(), profile);
}

// Single brew phase: the common case. Override must replace the stop target.
void test_set_volumetric_target_single_brew_phase(void) {
    Profile profile;
    TEST_ASSERT_TRUE(parseProfileJson(
        R"({"label":"Vol","type":"standard","temperature":93,"phases":[{"name":"Brew","phase":"brew",)"
        R"("valve":1,"duration":30,"pump":100,"targets":[{"type":"volumetric","operator":"gte","value":36}]}]})",
        profile));
    TEST_ASSERT_EQUAL_FLOAT(36.0f, profile.getTotalVolume());
    profile.setVolumetricTarget(44.0f);
    TEST_ASSERT_EQUAL_FLOAT(44.0f, profile.getTotalVolume());
    TEST_ASSERT_EQUAL_FLOAT(44.0f, profile.phases[0].getVolumetricTarget().value);
}

// Multi brew phase carrying the same cumulative target on each phase (mirrors
// the bundled "adapt" profile). Both must end up at the new YIELD so whichever
// phase trips first stops at the requested weight.
void test_set_volumetric_target_multi_brew_phase(void) {
    Profile profile;
    TEST_ASSERT_TRUE(parseProfileJson(
        R"({"label":"Multi","type":"pro","temperature":93,"phases":[)"
        R"({"name":"Pressurize","phase":"brew","valve":1,"duration":6,"pump":{"target":"pressure","pressure":11,"flow":0},)"
        R"("targets":[{"type":"volumetric","operator":"gte","value":38}]},)"
        R"({"name":"Extraction","phase":"brew","valve":1,"duration":60,"pump":{"target":"flow","pressure":9,"flow":2},)"
        R"("targets":[{"type":"volumetric","operator":"gte","value":38}]}]})",
        profile));
    TEST_ASSERT_EQUAL_FLOAT(38.0f, profile.getTotalVolume());
    profile.setVolumetricTarget(50.0f);
    TEST_ASSERT_EQUAL_FLOAT(50.0f, profile.getTotalVolume());
    TEST_ASSERT_EQUAL_FLOAT(50.0f, profile.phases[0].getVolumetricTarget().value);
    TEST_ASSERT_EQUAL_FLOAT(50.0f, profile.phases[1].getVolumetricTarget().value);
}

// CAR-355 regression: the volumetric stop target lives on a phase tagged
// "preinfusion" (a lever/manual-style profile). The old setter filtered on
// PHASE_TYPE_BREW and skipped it, so the override was a no-op. The fix scales
// every volumetric phase regardless of tag.
void test_set_volumetric_target_on_non_brew_phase(void) {
    Profile profile;
    TEST_ASSERT_TRUE(parseProfileJson(
        R"({"label":"Lever","type":"pro","temperature":90,"phases":[)"
        R"({"name":"Preinfuse","phase":"preinfusion","valve":1,"duration":40,"pump":{"target":"pressure","pressure":2,"flow":0},)"
        R"("targets":[{"type":"volumetric","operator":"gte","value":36}]}]})",
        profile));
    TEST_ASSERT_TRUE(profile.isVolumetric());
    TEST_ASSERT_EQUAL_FLOAT(36.0f, profile.getTotalVolume());
    profile.setVolumetricTarget(42.0f);
    // Must actually move — not be skipped because the phase isn't "brew".
    TEST_ASSERT_EQUAL_FLOAT(42.0f, profile.getTotalVolume());
    TEST_ASSERT_EQUAL_FLOAT(42.0f, profile.phases[0].getVolumetricTarget().value);
}

// Time-based profile (no volumetric target): override is a safe no-op so we
// don't accidentally rewrite a duration-only profile.
void test_set_volumetric_target_no_op_when_not_volumetric(void) {
    Profile profile;
    TEST_ASSERT_TRUE(parseProfileJson(
        R"({"label":"Time","type":"standard","temperature":93,"phases":[)"
        R"({"name":"Brew","phase":"brew","valve":1,"duration":25,"pump":100}]})",
        profile));
    TEST_ASSERT_FALSE(profile.isVolumetric());
    TEST_ASSERT_EQUAL_FLOAT(0.0f, profile.getTotalVolume());
    profile.setVolumetricTarget(40.0f); // no volumetric target -> nothing to scale
    TEST_ASSERT_EQUAL_FLOAT(0.0f, profile.getTotalVolume());
    TEST_ASSERT_EQUAL_FLOAT(25.0f, profile.getTotalDuration()); // duration untouched
}

// Non-positive YIELD is rejected (guards against divide-by-zero / clearing the
// target). The saved target must be preserved.
void test_set_volumetric_target_rejects_non_positive(void) {
    Profile profile;
    TEST_ASSERT_TRUE(parseProfileJson(
        R"({"label":"Vol","type":"standard","temperature":93,"phases":[{"name":"Brew","phase":"brew",)"
        R"("valve":1,"duration":30,"pump":100,"targets":[{"type":"volumetric","operator":"gte","value":36}]}]})",
        profile));
    profile.setVolumetricTarget(0.0f);
    TEST_ASSERT_EQUAL_FLOAT(36.0f, profile.getTotalVolume());
    profile.setVolumetricTarget(-5.0f);
    TEST_ASSERT_EQUAL_FLOAT(36.0f, profile.getTotalVolume());
}

// adjustVolumetricTarget (the +/- steppers) must move every volumetric phase,
// including non-brew ones, consistently with setVolumetricTarget.
void test_adjust_volumetric_target_includes_non_brew(void) {
    Profile profile;
    TEST_ASSERT_TRUE(parseProfileJson(
        R"({"label":"Lever","type":"pro","temperature":90,"phases":[)"
        R"({"name":"Preinfuse","phase":"preinfusion","valve":1,"duration":40,"pump":{"target":"pressure","pressure":2,"flow":0},)"
        R"("targets":[{"type":"volumetric","operator":"gte","value":36}]}]})",
        profile));
    profile.adjustVolumetricTarget(4.0f); // +4g
    TEST_ASSERT_EQUAL_FLOAT(40.0f, profile.getTotalVolume());
    TEST_ASSERT_EQUAL_FLOAT(40.0f, profile.phases[0].getVolumetricTarget().value);
}

static int runVolumetricTargetTests() {
    UNITY_BEGIN();
    RUN_TEST(test_set_volumetric_target_single_brew_phase);
    RUN_TEST(test_set_volumetric_target_multi_brew_phase);
    RUN_TEST(test_set_volumetric_target_on_non_brew_phase);
    RUN_TEST(test_set_volumetric_target_no_op_when_not_volumetric);
    RUN_TEST(test_set_volumetric_target_rejects_non_positive);
    RUN_TEST(test_adjust_volumetric_target_includes_non_brew);
    return UNITY_END();
}

#ifdef ARDUINO
void setup() { runVolumetricTargetTests(); }

void loop() {}
#else
int main(int argc, char **argv) { return runVolumetricTargetTests(); }
#endif
