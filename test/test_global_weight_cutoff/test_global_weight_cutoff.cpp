#include "../../src/display/core/process/GlobalWeightCutoffPolicy.h"
#include <unity.h>

// PRO-440: pure predicate for the GLOBAL cumulative-weight cutoff.
//
// isGlobalWeightCutoffReached(target, volumetricAvailable, ceiling,
//                             currentVolume, predictedAddedVolume)
// is a single authoritative "never exceed X grams total output" stop that ends
// a brewing shot regardless of which phase is active. It exists because every
// per-phase volumetric target only ADVANCES an intermediate phase (it does not
// end the shot), so when early phases flow faster than expected the cumulative
// weight can exceed the intended final yield before the shot reaches the
// terminal volumetric phase — causing overshoot. BrewProcess::progress()
// evaluates this once per tick BEFORE the per-phase advance loop.
//
// The ceiling is derived from the profile's final volumetric target
// (BrewProcess::getBrewVolume(), option (a)); the caller passes the same
// predictive-overshoot volume as the per-phase stop; and the cutoff is enabled
// only when running volumetrically with a live scale
// (target == VOLUMETRIC && volumetricAvailable). Extracted as a free function
// so it is host-testable in [env:native] without linking
// Process/predictive/esp_log — mirrors VolumetricSourcePolicy.h.

using PT = ProcessTarget;

// Compile-time truth table (mirrors the runtime cases below).
static_assert(isGlobalWeightCutoffReached(PT::VOLUMETRIC, true, 36.0, 36.0, 0.0),
              "ceiling reached exactly -> cutoff");
static_assert(!isGlobalWeightCutoffReached(PT::VOLUMETRIC, true, 36.0, 30.0, 0.0),
              "below ceiling -> no cutoff");
static_assert(!isGlobalWeightCutoffReached(PT::VOLUMETRIC, false, 36.0, 40.0, 0.0),
              "scale unavailable -> cutoff disabled");
static_assert(!isGlobalWeightCutoffReached(PT::TIME, true, 36.0, 40.0, 0.0),
              "time-based shot -> cutoff disabled");
static_assert(!isGlobalWeightCutoffReached(PT::VOLUMETRIC, true, 0.0, 40.0, 0.0),
              "no volumetric target (ceiling 0) -> cutoff disabled");

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// Acceptance case (a): ceiling reached in an EARLY/intermediate phase => shot
// finishes immediately. Modelled here as the ceiling (final-phase yield, e.g.
// 36 g) being met while cumulative weight is already at/above it — the caller
// (BrewProcess::progress) fires this every tick regardless of phaseIndex, so a
// fast early phase that pushes weight to 36 g stops the shot before it can run
// to the terminal phase / overshoot.
// ---------------------------------------------------------------------------
void test_cutoff_fires_when_ceiling_reached_early(void) {
    // Cumulative weight has already hit the whole-shot ceiling.
    TEST_ASSERT_TRUE(isGlobalWeightCutoffReached(PT::VOLUMETRIC, true, 36.0, 36.0, 0.0));
    // Overshot outright.
    TEST_ASSERT_TRUE(isGlobalWeightCutoffReached(PT::VOLUMETRIC, true, 36.0, 40.0, 0.0));
}

// Predictive overshoot: in-flight drips (predictedAddedVolume) count toward the
// ceiling, mirroring the terminal per-phase stop. 33 g measured + 4 g predicted
// = 37 g >= 36 g ceiling -> stop now, so the drips don't push past the yield.
void test_cutoff_accounts_for_predicted_overshoot(void) {
    TEST_ASSERT_TRUE(isGlobalWeightCutoffReached(PT::VOLUMETRIC, true, 36.0, 33.0, 4.0));
    // 30 g measured + 4 g predicted = 34 g < 36 g -> not yet.
    TEST_ASSERT_FALSE(isGlobalWeightCutoffReached(PT::VOLUMETRIC, true, 36.0, 30.0, 4.0));
}

// ---------------------------------------------------------------------------
// Acceptance case (b): ceiling NOT reached => cutoff does not fire, so the
// normal per-phase advance runs. Below the ceiling (weight + prediction) the
// predicate is false and BrewProcess::progress() falls through to its per-phase
// isCurrentPhaseFinished() loop unchanged.
// ---------------------------------------------------------------------------
void test_cutoff_does_not_fire_below_ceiling(void) {
    TEST_ASSERT_FALSE(isGlobalWeightCutoffReached(PT::VOLUMETRIC, true, 36.0, 20.0, 0.0));
    TEST_ASSERT_FALSE(isGlobalWeightCutoffReached(PT::VOLUMETRIC, true, 36.0, 35.9, 0.0));
}

// ---------------------------------------------------------------------------
// Acceptance case (c): scale unavailable (volumetricAvailable == false) =>
// cutoff disabled, so normal per-phase duration + BREW_SAFETY_DURATION_MS
// govern. Even a wildly overshot weight must NOT trip the cutoff once the scale
// is unhealthy — the reading is stale and the per-phase duration fallback owns
// the stop (consistent with the CAR-367 scale-loss fallback).
// ---------------------------------------------------------------------------
void test_cutoff_disabled_when_scale_unavailable(void) {
    TEST_ASSERT_FALSE(isGlobalWeightCutoffReached(PT::VOLUMETRIC, false, 36.0, 40.0, 0.0));
    TEST_ASSERT_FALSE(isGlobalWeightCutoffReached(PT::VOLUMETRIC, false, 36.0, 36.0, 10.0));
}

// A time-based shot (target == TIME, no volumetric stop) never trips the
// weight cutoff — the ceiling is irrelevant to a duration-driven shot.
void test_cutoff_disabled_for_time_target(void) {
    TEST_ASSERT_FALSE(isGlobalWeightCutoffReached(PT::TIME, true, 36.0, 40.0, 0.0));
    TEST_ASSERT_FALSE(isGlobalWeightCutoffReached(PT::TIME, true, 36.0, 100.0, 5.0));
}

// No volumetric target in the profile => getBrewVolume() returns 0 => ceiling
// <= 0 => cutoff disabled. This is the "only apply when getBrewVolume() > 0"
// zero-config guard: a pure pressure/flow/time profile is never cut off by
// weight even though it is (nominally) running with a scale attached.
void test_cutoff_disabled_when_no_ceiling(void) {
    TEST_ASSERT_FALSE(isGlobalWeightCutoffReached(PT::VOLUMETRIC, true, 0.0, 40.0, 0.0));
    // Defensive: a negative ceiling (shouldn't happen) is also treated as "no ceiling".
    TEST_ASSERT_FALSE(isGlobalWeightCutoffReached(PT::VOLUMETRIC, true, -1.0, 40.0, 0.0));
}

// No CAR-367 regression: on the terminal volumetric phase the ceiling equals
// the phase's own volumetric target and the effective volume is computed with
// the same predictive math, so this predicate fires at EXACTLY the weight the
// per-phase stop already fires at (36 g). It therefore never stops later than
// today — it only ever stops earlier (on an intermediate phase). We assert the
// boundary: at 35.9 g it does not fire (phase would still be running), at 36 g
// it fires (same as the per-phase terminal stop).
void test_terminal_phase_boundary_matches_per_phase_stop(void) {
    TEST_ASSERT_FALSE(isGlobalWeightCutoffReached(PT::VOLUMETRIC, true, 36.0, 35.9, 0.0));
    TEST_ASSERT_TRUE(isGlobalWeightCutoffReached(PT::VOLUMETRIC, true, 36.0, 36.0, 0.0));
}

static int runGlobalWeightCutoffTests() {
    UNITY_BEGIN();
    RUN_TEST(test_cutoff_fires_when_ceiling_reached_early);
    RUN_TEST(test_cutoff_accounts_for_predicted_overshoot);
    RUN_TEST(test_cutoff_does_not_fire_below_ceiling);
    RUN_TEST(test_cutoff_disabled_when_scale_unavailable);
    RUN_TEST(test_cutoff_disabled_for_time_target);
    RUN_TEST(test_cutoff_disabled_when_no_ceiling);
    RUN_TEST(test_terminal_phase_boundary_matches_per_phase_stop);
    return UNITY_END();
}

#ifdef ARDUINO
void setup() { runGlobalWeightCutoffTests(); }

void loop() {}
#else
int main(int argc, char **argv) { return runGlobalWeightCutoffTests(); }
#endif
