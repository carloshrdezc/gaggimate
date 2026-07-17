#pragma once

#include <cmath>

// PRO-386: single source of truth for the BLE-scale measurement-validity gate.
// A measurement is cached (BLEScalePlugin::lastWeight, read by getLastWeight())
// and forwarded to the controller ONLY when this returns true — see
// BLEScalePlugin::onMeasurement, which follows a gate-then-cache ordering: the
// cache write `lastWeight = value` is downstream of this predicate, so a false
// result provably leaves getLastWeight() unchanged.
//
// Rejects: NaN, +/-inf, values < -1000, values > 10000.
// Accepts: everything finite and in [-1000, 10000], boundaries inclusive.
// std::isfinite is not constexpr before C++23, so this stays a plain runtime
// `inline bool` (matches how BLEScaleScanPolicy.h mixes constexpr predicates
// with runtime-tested ones).
inline bool isValidBleScaleMeasurement(float value) { return std::isfinite(value) && value >= -1000.0f && value <= 10000.0f; }
