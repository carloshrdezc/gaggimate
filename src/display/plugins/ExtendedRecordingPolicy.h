#ifndef EXTENDEDRECORDINGPOLICY_H
#define EXTENDEDRECORDINGPOLICY_H

// PRO-232: Decision predicate for opening the post-stop extended-recording /
// weight-settle window in ShotHistoryPlugin::endRecording().
//
// The window must open exactly when a live BLE scale was the active volumetric
// source for the just-ended shot, so the post-stop drips are captured before
// auto-steam engages (the PRO-223 gate in DefaultUI holds auto-steam until
// isExtendedRecording() goes false).
//
// Inputs:
//  - recording:              a shot was actively being recorded.
//  - allowExtendedRecording: the end path permits settling. False for the MANUAL
//                            controller:process:end path, which must never settle.
//  - bluetoothScaleHealthy:  Controller::isBluetoothScaleHealthy() — true only when
//                            a BLE measurement arrived within BLUETOOTH_GRACE_PERIOD_MS.
//                            This is the genuine "a BLE scale is live" signal and the
//                            correct, non-brittle replacement for the old
//                            `currentBluetoothWeight > 0` precondition (a stale/0 sample
//                            during the grace-period source switch wrongly skipped it).
//
// Note we deliberately do NOT require a positive instantaneous weight: opening with
// weight==0 is safe because the settle loop self-terminates via weight stabilization
// and is hard-capped by EXTENDED_RECORDING_DURATION, and it closes immediately if the
// scale stops being healthy. On the no-scale / flow-estimation / time-based path,
// bluetoothScaleHealthy is false, so the window never opens and steam engages at once.
constexpr bool shouldOpenExtendedRecording(bool recording, bool allowExtendedRecording, bool bluetoothScaleHealthy) {
    return recording && allowExtendedRecording && bluetoothScaleHealthy;
}

#endif // EXTENDEDRECORDINGPOLICY_H
