// PRO-631: brew start target temperature, derived from the shot's own log data.
//
// The `.slog` binary format already samples `tt` (target temperature, stored as
// °C × 10, see shot_log_format.h / parseBinaryShot.js) every 250 ms, so the
// effective target requested at brew start is the first valid `tt` sample of the
// shot. Deriving it here keeps the value a historical fact of the shot log
// instead of a mutable profile/notes value, so no new binary field and no new
// device-note write is needed.

// `tt` is a uint16 divided by TEMP_SCALE, so garbage/uninitialised data can show
// up as a wildly out-of-range value. Anything above this is not a plausible
// brew target and is treated as missing rather than displayed.
const MAX_PLAUSIBLE_TARGET_C = 200;

function isPlausibleTarget(value) {
  return (
    typeof value === 'number' &&
    Number.isFinite(value) &&
    value > 0 &&
    value <= MAX_PLAUSIBLE_TARGET_C
  );
}

/**
 * First valid target temperature of a shot, i.e. the target that was requested
 * at brew start. Phase-changing profiles are not misrepresented: this is only
 * the *start* target, later phases may request something else.
 *
 * @param {{ samples?: Array<{ tt?: number }> }} shot parsed shot (device, browser or imported)
 * @returns {number|null} target in °C rounded to 0.1, or null when the shot log
 *   carries no usable `tt` (manual shots, logs predating the TT field, zeroed or
 *   malformed samples, shots stored without their sample trace).
 */
export function deriveStartTargetTemperature(shot) {
  const samples = shot?.samples;
  if (!Array.isArray(samples)) return null;

  for (const sample of samples) {
    const tt = sample?.tt;
    if (isPlausibleTarget(tt)) {
      return Math.round(tt * 10) / 10;
    }
  }

  return null;
}

/**
 * Display string for a derived start target temperature.
 *
 * @param {number|null} value result of deriveStartTargetTemperature
 * @returns {string} e.g. `93.0 °C`, or `N/A` when no target could be established.
 */
export function formatStartTargetTemperature(value) {
  if (!isPlausibleTarget(value)) return 'N/A';
  return `${value.toFixed(1)} °C`;
}
