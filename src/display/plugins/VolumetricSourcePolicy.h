#ifndef VOLUMETRICSOURCEPOLICY_H
#define VOLUMETRICSOURCEPOLICY_H

#include "../core/VolumetricMeasurementSource.h"

// PRO-4: mid-shot volumetric source fallback decision.
//
// The problem: Controller::activate() latches currentVolumetricSource ONCE at
// shot start (BLUETOOTH when a healthy BLE scale is present, else
// FLOW_ESTIMATION) and holds it for the whole shot. onVolumetricMeasurement()
// drops every measurement whose source != the latched source. So if a shot
// starts on BLUETOOTH and the scale drops out mid-shot, BLE samples stop
// arriving while FLOW_ESTIMATION samples (which the controller keeps producing
// independently of the scale) keep getting rejected — the volume reading FREEZES
// for the rest of the shot with no fallback and no signal.
//
// The fix: when a FLOW_ESTIMATION sample arrives while the latched source is
// BLUETOOTH and the BLE scale has gone unhealthy, fall the source FORWARD to
// FLOW_ESTIMATION so volume keeps advancing. This is deliberately a ONE-WAY
// fall-forward:
//   - Only BLUETOOTH -> FLOW_ESTIMATION is ever taken mid-shot. We never switch
//     FLOW_ESTIMATION -> BLUETOOTH mid-shot: a late-arriving BLE reading on a
//     flow-started shot is lower priority and switching back risks thrashing.
//   - It is naturally DEBOUNCED by the health check itself:
//     Controller::isBluetoothScaleHealthy() only reports unhealthy once the
//     1.5 s BLUETOOTH_GRACE_PERIOD_MS window has genuinely elapsed with no BLE
//     measurement (and no volumetricOverride), so jittery BLE within the grace
//     window does NOT trigger a switch.
//
// This predicate is pure and header-only so it is host-testable in [env:native]
// without linking Controller/BLE/LVGL/FreeRTOS.
//
// Inputs:
//  - latched:              currentVolumetricSource — the source the shot is
//                          currently consuming.
//  - incoming:             the source of the measurement that just arrived.
//  - bluetoothScaleHealthy: Controller::isBluetoothScaleHealthy() — false once
//                          the BLE grace window has elapsed with no measurement.
//
// Returns true iff the caller should adopt FLOW_ESTIMATION as the new latched
// source (the only fallback this policy performs).
constexpr bool shouldFallBackToFlowEstimation(VolumetricMeasurementSource latched, VolumetricMeasurementSource incoming,
                                              bool bluetoothScaleHealthy) {
    return latched == VolumetricMeasurementSource::BLUETOOTH && incoming == VolumetricMeasurementSource::FLOW_ESTIMATION &&
           !bluetoothScaleHealthy;
}

// Compile-time truth table — pins the fall-forward contract so a future edit to
// the predicate fails the firmware compile rather than silently changing the
// mid-shot switching behavior.
//
// The one case that falls back: latched BLUETOOTH, a FLOW_ESTIMATION sample
// arrived, and the scale is unhealthy.
static_assert(shouldFallBackToFlowEstimation(VolumetricMeasurementSource::BLUETOOTH, VolumetricMeasurementSource::FLOW_ESTIMATION,
                                             false),
              "PRO-4: BLUETOOTH + unhealthy scale + flow-estimation sample -> fall forward");
// Debounce: while the scale is still healthy (within grace), do NOT switch.
static_assert(!shouldFallBackToFlowEstimation(VolumetricMeasurementSource::BLUETOOTH,
                                              VolumetricMeasurementSource::FLOW_ESTIMATION, true),
              "PRO-4: healthy scale (within grace) must not switch");
// Never switch on a BLUETOOTH sample (that is the healthy path / a stray BLE read).
static_assert(!shouldFallBackToFlowEstimation(VolumetricMeasurementSource::BLUETOOTH, VolumetricMeasurementSource::BLUETOOTH,
                                              false),
              "PRO-4: a BLUETOOTH sample never triggers the fallback");
// Already on FLOW_ESTIMATION: no switch (idempotent, and never fall back to BLUETOOTH).
static_assert(!shouldFallBackToFlowEstimation(VolumetricMeasurementSource::FLOW_ESTIMATION,
                                              VolumetricMeasurementSource::FLOW_ESTIMATION, false),
              "PRO-4: already flow-estimation -> no switch");
static_assert(!shouldFallBackToFlowEstimation(VolumetricMeasurementSource::FLOW_ESTIMATION,
                                              VolumetricMeasurementSource::BLUETOOTH, true),
              "PRO-4: flow-estimation shot never switches to bluetooth mid-shot");
// No shot in progress: never switch.
static_assert(!shouldFallBackToFlowEstimation(VolumetricMeasurementSource::INACTIVE, VolumetricMeasurementSource::FLOW_ESTIMATION,
                                              false),
              "PRO-4: inactive source never switches");

#endif // VOLUMETRICSOURCEPOLICY_H
