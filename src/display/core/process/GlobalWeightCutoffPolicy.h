#ifndef GLOBALWEIGHTCUTOFFPOLICY_H
#define GLOBALWEIGHTCUTOFFPOLICY_H

#include <display/core/process/Process.h>

// PRO-440: pure predicate for the GLOBAL cumulative-weight cutoff.
//
// Every stop condition in a brew profile is otherwise per-phase: a volumetric
// Target::isReached is a GTE test against CUMULATIVE weight, but on an
// INTERMEDIATE phase a reached volumetric target only advances to the next
// phase (BrewProcess::progress) — it does not end the shot. When early phases
// flow faster than expected, cumulative shot weight can therefore exceed the
// intended final yield before the shot reaches the terminal volumetric phase
// where the stop target lives, causing overshoot.
//
// This is the whole-shot ceiling: a single authoritative "never exceed X grams
// total output" stop that ends the shot regardless of which phase is active.
// It is evaluated once per progress tick in BrewProcess::progress(), BEFORE the
// per-phase advance loop, so it wins over intermediate-phase advancement.
//
// Design (see PRO-440 PR body):
//  - Ceiling source: derived automatically from the profile's final volumetric
//    target via BrewProcess::getBrewVolume() (option (a): zero-config safety
//    net, no new UI). Only applied when the ceiling is > 0, i.e. the profile
//    actually has a volumetric target.
//  - Never regresses the terminal-phase authoritative stop (CAR-367): the
//    ceiling equals the terminal volumetric target, and the effective volume is
//    computed with the SAME predictive-overshoot math the terminal stop uses,
//    so on the final phase this predicate fires at exactly the same weight the
//    per-phase stop already would — it can only ever stop EARLIER (on an
//    intermediate phase), never later.
//  - Predictive overshoot: the caller passes predictedAddedVolume
//    (currentRate * brewDelay, clamped identically to
//    BrewProcess::isCurrentPhaseFinished) so the cutoff accounts for in-flight
//    drips, consistent with the terminal stop.
//  - Scale-loss fallback: the cutoff is ENABLED only when
//    target == ProcessTarget::VOLUMETRIC && volumetricAvailable. If the scale
//    goes unhealthy mid-shot (volumetricAvailable == false) the cutoff is
//    disabled and normal per-phase duration / BREW_SAFETY_DURATION_MS govern.
//
// Extracted as a free function (rather than inlined into BrewProcess) so it is
// host-testable in [env:native] without linking Process/predictive/esp_log —
// mirrors src/display/plugins/VolumetricSourcePolicy.h.
//
// ceiling            : the whole-shot weight ceiling (grams). Typically
//                      BrewProcess::getBrewVolume() — the profile's final
//                      volumetric target. Values <= 0 mean "no ceiling".
// currentVolume      : most recent cumulative volume pushed to the process (g).
// predictedAddedVolume: extra grams predicted to land during brewDelay
//                      (currentRate * brewDelay, already clamped by the caller).
inline constexpr bool isGlobalWeightCutoffReached(ProcessTarget target, bool volumetricAvailable, double ceiling,
                                                  double currentVolume, double predictedAddedVolume) {
    if (target != ProcessTarget::VOLUMETRIC || !volumetricAvailable) {
        return false;
    }
    if (ceiling <= 0.0) {
        return false;
    }
    const double effectiveVolume = currentVolume + predictedAddedVolume;
    // effectiveVolume is a monotonically-increasing cumulative weight; the ceiling is
    // always a "reach-or-exceed" (GTE) target. An LTE volumetric target is not a valid
    // configuration, so >= is the correct and only meaningful comparison here.
    return effectiveVolume >= ceiling;
}

#endif // GLOBALWEIGHTCUTOFFPOLICY_H
