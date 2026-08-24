#pragma once

// BLE-scale yield is cumulative during a shot. A post-shot tare/reset can emit
// a low value while the drip-settle window remains open, so never let it lower
// the final captured yield.
inline constexpr float finalShotYieldAfterMeasurement(float previousYield, float measurement) {
    return measurement > previousYield ? measurement : previousYield;
}
