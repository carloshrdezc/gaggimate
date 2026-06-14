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

// ---------------------------------------------------------------------------
// CAR-367: Phase::isFinished — dashboard YIELD must be the authoritative stop
// even when the user raises it past the volumetric phase's `duration` cap.
//
// isFinished(enableVolumetric, volume, time_in_phase, flow, pressure, pumped).
// enableVolumetric mirrors `target == ProcessTarget::VOLUMETRIC` (scale
// connected). The previous code only suppressed the time cap for
// type == "standard"; pro volumetric phases exited on whichever of weight or
// duration fired first. The fix suppresses the time cap for ANY volumetric
// phase running volumetrically, regardless of profile type.
// ---------------------------------------------------------------------------

// Build the failing-from-CAR-367 case: Atole Negro's last "Decline" phase, a
// `pro` phase with a volumetric stop (42 g) AND duration:20.
static Phase makeProDeclinePhase(float volumetricGrams, float durationSeconds) {
    Phase p;
    p.name = "Decline";
    p.phase = PhaseType::PHASE_TYPE_BREW;
    p.valve = 1;
    p.duration = durationSeconds;
    p.pumpIsSimple = false;
    Target vol;
    vol.type = TargetType::TARGET_TYPE_VOLUMETRIC;
    vol.operator_ = TargetOperator::GTE;
    vol.value = volumetricGrams;
    p.targets.push_back(vol);
    return p;
}

// Core CAR-367 fix: the TERMINAL volumetric phase running volumetrically must
// NOT exit on its duration cap. With YIELD raised to 42 g on a 20 s phase, at
// t=25 s and only 30 g pulled the phase must still be running. The terminal
// phase is signalled by suppressDurationForVolumetric=true (set by the caller
// only for Profile::indexOfFinalVolumetricPhase()).
void test_pro_volumetric_phase_does_not_exit_on_duration(void) {
    Phase p = makeProDeclinePhase(42.0f, 20.0f);
    // Past the 20 s duration cap, weight (30 g) below the 42 g target: keep going.
    TEST_ASSERT_FALSE(p.isFinished(/*enableVolumetric=*/true, /*volume=*/30.0f, /*time_in_phase=*/25.0f,
                                   /*flow=*/0.0f, /*pressure=*/0.0f, /*pumped=*/0.0f,
                                   /*suppressDurationForVolumetric=*/true));
    // Once the raised YIELD (42 g) is reached, the phase ends — on weight.
    TEST_ASSERT_TRUE(p.isFinished(true, 42.0f, 25.0f, 0.0f, 0.0f, 0.0f, true));
}

// Lowering YIELD still works: a smaller target reached before the duration cap
// stops the phase immediately on weight.
void test_pro_volumetric_phase_stops_on_lowered_yield(void) {
    Phase p = makeProDeclinePhase(20.0f, 20.0f);
    TEST_ASSERT_TRUE(p.isFinished(true, 20.0f, 5.0f, 0.0f, 0.0f, 0.0f, true)); // reached early
}

// Standard profile behaviour is unchanged: a standard volumetric stop phase
// (the terminal volumetric phase) ignores its duration cap when running
// volumetrically, and still does.
void test_standard_volumetric_phase_unchanged(void) {
    Phase p = makeProDeclinePhase(42.0f, 20.0f); // structure is type-agnostic at Phase level
    TEST_ASSERT_FALSE(p.isFinished(true, 30.0f, 25.0f, 0.0f, 0.0f, 0.0f, true)); // past duration, below target
    TEST_ASSERT_TRUE(p.isFinished(true, 42.0f, 25.0f, 0.0f, 0.0f, 0.0f, true));  // reached target
}

// REGRESSION (PR #172 review, Codex P1): an INTERMEDIATE volumetric phase MUST
// keep its duration cap. Mirrors Adaptive v2's "Pressurize" phase (6 s duration
// + pressure 8.8 + 38 g volumetric). With suppressDurationForVolumetric=false
// (because it is NOT the terminal volumetric phase), past its duration the phase
// must advance on TIME even though the cumulative yield isn't reached — it must
// NOT hold the shot at the intermediate pressure until 38 g.
void test_intermediate_volumetric_phase_still_exits_on_duration(void) {
    Phase p = makeProDeclinePhase(38.0f, 6.0f);
    Target pressure;
    pressure.type = TargetType::TARGET_TYPE_PRESSURE;
    pressure.operator_ = TargetOperator::GTE;
    pressure.value = 8.8f;
    p.targets.push_back(pressure);
    // Within duration, below all targets: keep going.
    TEST_ASSERT_FALSE(p.isFinished(true, 10.0f, 4.0f, 0.0f, /*pressure=*/5.0f, 0.0f,
                                   /*suppressDurationForVolumetric=*/false));
    // Past the 6 s duration, weight (10 g) below 38 g, pressure (5 bar) below 8.8:
    // an intermediate phase must STILL exit on time.
    TEST_ASSERT_TRUE(p.isFinished(true, 10.0f, 7.0f, 0.0f, /*pressure=*/5.0f, 0.0f, false));
}

// Profile::indexOfFinalVolumetricPhase() drives the terminal-phase decision.
// Adaptive-v2-shaped profile: an intermediate volumetric "Pressurize" phase
// (index 0 here) and a terminal volumetric "Extraction" phase (index 1). Only
// the terminal one should be flagged.
void test_index_of_final_volumetric_phase(void) {
    Profile profile;
    profile.type = "pro";
    profile.phases.push_back(makeProDeclinePhase(38.0f, 6.0f));  // intermediate volumetric
    profile.phases.push_back(makeProDeclinePhase(38.0f, 60.0f)); // terminal volumetric
    TEST_ASSERT_EQUAL_INT(1, profile.indexOfFinalVolumetricPhase());

    // No volumetric phase at all -> -1 (e.g. a pure time/pressure profile).
    Profile timeOnly;
    timeOnly.type = "pro";
    Phase t;
    t.phase = PhaseType::PHASE_TYPE_BREW;
    t.duration = 30.0f;
    Target pr;
    pr.type = TargetType::TARGET_TYPE_PRESSURE;
    pr.operator_ = TargetOperator::GTE;
    pr.value = 9.0f;
    t.targets.push_back(pr);
    timeOnly.phases.push_back(t);
    TEST_ASSERT_EQUAL_INT(-1, timeOnly.indexOfFinalVolumetricPhase());
}

// No scale connected (enableVolumetric=false): the volumetric target is NOT a
// stop condition, so the phase falls back to its duration cap unchanged. This
// preserves the "no change when volumetric unavailable" acceptance criterion.
// suppressDurationForVolumetric is false here (the caller only sets it under
// ProcessTarget::VOLUMETRIC).
void test_no_scale_falls_back_to_duration(void) {
    Phase p = makeProDeclinePhase(42.0f, 20.0f);
    TEST_ASSERT_FALSE(p.isFinished(/*enableVolumetric=*/false, /*volume=*/0.0f, /*time_in_phase=*/19.0f,
                                   0.0f, 0.0f, 0.0f, /*suppressDurationForVolumetric=*/false)); // within duration
    TEST_ASSERT_TRUE(p.isFinished(false, 0.0f, 21.0f, 0.0f, 0.0f, 0.0f, false)); // exceeds duration -> stop on time
}

// Multi-target terminal `pro` phase: even with duration suppression active, the
// other targets (pressure/flow/pumped) still fire to advance the phase.
void test_pro_multi_target_phase_other_targets_still_fire(void) {
    Phase p = makeProDeclinePhase(42.0f, 20.0f);
    Target pressure;
    pressure.type = TargetType::TARGET_TYPE_PRESSURE;
    pressure.operator_ = TargetOperator::LTE; // e.g. decline finished when pressure drops <= 4 bar
    pressure.value = 4.0f;
    p.targets.push_back(pressure);
    // Terminal phase (suppress=true): past duration, weight below target,
    // pressure still high -> keep going.
    TEST_ASSERT_FALSE(p.isFinished(true, 30.0f, 25.0f, /*flow=*/0.0f, /*pressure=*/9.0f, 0.0f, /*suppress=*/true));
    // Pressure target reached -> phase advances even though weight target isn't.
    TEST_ASSERT_TRUE(p.isFinished(true, 30.0f, 25.0f, 0.0f, /*pressure=*/3.0f, 0.0f, true));
}

// A duration-only phase (no volumetric target at all) still honours its time
// cap when running volumetrically — even if the caller erroneously passed
// suppress=true, there is no volumetric target so volumetricTested stays false.
void test_non_volumetric_phase_honours_duration_when_volumetric_enabled(void) {
    Phase p;
    p.name = "RampUp";
    p.phase = PhaseType::PHASE_TYPE_PREINFUSION;
    p.valve = 1;
    p.duration = 5.0f;
    p.pumpIsSimple = true;
    p.pumpSimple = 100;
    // No targets at all.
    TEST_ASSERT_FALSE(p.isFinished(true, 0.0f, 4.0f, 0.0f, 0.0f, 0.0f, true)); // within duration
    TEST_ASSERT_TRUE(p.isFinished(true, 0.0f, 6.0f, 0.0f, 0.0f, 0.0f, true));  // exceeds duration -> stop
}

// Defense-in-depth (PR #172 review S2): a malformed VOLUMETRIC target with
// value <= 0 must NOT count as a volumetric stop. It must not be "reached" at
// volume 0, and it must not enable duration-cap suppression — the phase must
// fall back to its duration cap exactly like a non-volumetric phase. Mirrors
// hasVolumetricTarget()'s `value > 0` guard.
void test_zero_value_volumetric_target_ignored(void) {
    Phase p;
    p.name = "Bogus";
    p.phase = PhaseType::PHASE_TYPE_BREW;
    p.valve = 1;
    p.duration = 5.0f;
    p.pumpIsSimple = true;
    p.pumpSimple = 100;
    Target zero;
    zero.type = TargetType::TARGET_TYPE_VOLUMETRIC;
    zero.operator_ = TargetOperator::GTE;
    zero.value = 0.0f;
    p.targets.push_back(zero);
    TEST_ASSERT_FALSE(p.hasVolumetricTarget()); // consistency with the guard
    // At volume 0, even with suppress=true requested, the 0-value target is
    // ignored -> not finished within duration...
    TEST_ASSERT_FALSE(p.isFinished(true, 0.0f, 4.0f, 0.0f, 0.0f, 0.0f, /*suppress=*/true));
    // ...and the duration cap still applies past it.
    TEST_ASSERT_TRUE(p.isFinished(true, 0.0f, 6.0f, 0.0f, 0.0f, 0.0f, true));
}

// REGRESSION (PR #172 review, Codex P1): scale lost MID-SHOT must restore the
// duration cap on the terminal volumetric phase. BrewProcess::target is latched
// to ProcessTarget::VOLUMETRIC at construction and never updated, so before the
// fix suppressDurationForVolumetric stayed true after the scale went unhealthy
// and the pump ran to the 300 s BREW_SAFETY_DURATION_MS ceiling instead of the
// configured phase duration. The fix threads Controller::isVolumetricAvailable()
// into BrewProcess each tick and ANDs it into suppressDurationForVolumetric, so
// when live volumetric availability is lost the Controller now computes
// suppressDurationForVolumetric=FALSE. We model that post-scale-loss path here
// by passing suppress=false on the terminal volumetric phase: it must fall back
// to its duration cap (still bounded by BREW_SAFETY_DURATION_MS as before).
void test_scale_lost_restores_duration_cap(void) {
    Phase p = makeProDeclinePhase(42.0f, 20.0f); // terminal volumetric phase, 20 s cap
    // Scale lost -> Controller passes volumetricAvailable=false -> suppress=false.
    // Past the 20 s duration with weight (30 g) below the 42 g target: STOP on time.
    TEST_ASSERT_TRUE(p.isFinished(true, 30.0f, 25.0f, 0.0f, 0.0f, 0.0f, /*suppress=*/false));
    // Within the duration cap it keeps running (no stale weight has reached target).
    TEST_ASSERT_FALSE(p.isFinished(true, 30.0f, 19.0f, 0.0f, 0.0f, 0.0f, false));
}

static int runVolumetricTargetTests() {
    UNITY_BEGIN();
    RUN_TEST(test_set_volumetric_target_single_brew_phase);
    RUN_TEST(test_set_volumetric_target_multi_brew_phase);
    RUN_TEST(test_set_volumetric_target_on_non_brew_phase);
    RUN_TEST(test_set_volumetric_target_no_op_when_not_volumetric);
    RUN_TEST(test_set_volumetric_target_rejects_non_positive);
    RUN_TEST(test_adjust_volumetric_target_includes_non_brew);
    RUN_TEST(test_pro_volumetric_phase_does_not_exit_on_duration);
    RUN_TEST(test_pro_volumetric_phase_stops_on_lowered_yield);
    RUN_TEST(test_standard_volumetric_phase_unchanged);
    RUN_TEST(test_intermediate_volumetric_phase_still_exits_on_duration);
    RUN_TEST(test_index_of_final_volumetric_phase);
    RUN_TEST(test_no_scale_falls_back_to_duration);
    RUN_TEST(test_pro_multi_target_phase_other_targets_still_fire);
    RUN_TEST(test_non_volumetric_phase_honours_duration_when_volumetric_enabled);
    RUN_TEST(test_zero_value_volumetric_target_ignored);
    RUN_TEST(test_scale_lost_restores_duration_cap);
    return UNITY_END();
}

#ifdef ARDUINO
void setup() { runVolumetricTargetTests(); }

void loop() {}
#else
int main(int argc, char **argv) { return runVolumetricTargetTests(); }
#endif
