export const MODE_STANDBY = 0;
export const MODE_BREW = 1;
export const MODE_STEAM = 2;
export const MODE_WATER = 3;
export const MODE_GRIND = 4;
export const MODE_MANUAL = 5;
export const TEMP_MIN = 0;
export const TEMP_MAX = 105;
export const MANUAL_TARGET_PRESSURE = 'pressure';
export const MANUAL_TARGET_FLOW = 'flow';
export const MANUAL_TEMP_MIN = 80;
export const MANUAL_TEMP_MAX = 105;
export const MANUAL_PRESSURE_MIN = 0;
export const MANUAL_PRESSURE_MAX = 12;
export const MANUAL_FLOW_MIN = 0;
export const MANUAL_FLOW_MAX = 6;

export const MODE_OPTIONS = [
  { id: MODE_STANDBY, name: 'STANDBY' },
  { id: MODE_BREW, name: 'BREW' },
  { id: MODE_STEAM, name: 'STEAM' },
  { id: MODE_WATER, name: 'WATER' },
  { id: MODE_MANUAL, name: 'MANUAL' },
  { id: MODE_GRIND, name: 'GRIND' },
];

const MIN_STEAM_TARGET = 120;
const DEFAULT_STEAM_TARGET = 150;

function finiteOrZero(value) {
  return Number.isFinite(value) ? value : 0;
}

function fractionBetween(value, min, max) {
  if (!Number.isFinite(max) || max <= min) return 0;
  return (finiteOrZero(value) - min) / (max - min);
}

function clampNumber(value, min, max) {
  const numeric = Number(value);
  if (!Number.isFinite(numeric)) return min;
  return Math.min(max, Math.max(min, numeric));
}

export function clampManualTemperature(value) {
  return clampNumber(value, MANUAL_TEMP_MIN, MANUAL_TEMP_MAX);
}

export function clampManualPressure(value) {
  return clampNumber(value, MANUAL_PRESSURE_MIN, MANUAL_PRESSURE_MAX);
}

export function clampManualFlow(value) {
  return clampNumber(value, MANUAL_FLOW_MIN, MANUAL_FLOW_MAX);
}

export function getManualControlLabels(targetType) {
  return targetType === MANUAL_TARGET_FLOW
    ? { pressure: 'PRESSURE LIMIT', flow: 'FLOW TARGET' }
    : { pressure: 'PRESSURE TARGET', flow: 'FLOW LIMIT' };
}

export function getSteamTarget(targetTemp) {
  return Number.isFinite(targetTemp) && targetTemp > MIN_STEAM_TARGET
    ? targetTemp
    : DEFAULT_STEAM_TARGET;
}

export function getTemperatureRingMetrics({ mode, tempVal, targetTemp }) {
  const ringMax = mode === MODE_STEAM ? getSteamTarget(targetTemp) : TEMP_MAX;
  const ringTarget = mode === MODE_STEAM ? ringMax : finiteOrZero(targetTemp);

  return {
    progressFraction: fractionBetween(tempVal, TEMP_MIN, ringMax),
    targetFraction: fractionBetween(ringTarget, TEMP_MIN, ringMax),
    color: mode === MODE_STEAM ? 'var(--dm-warn)' : 'var(--dm-accent)',
  };
}

export function getBoilerHeatingState({ mode, active, finished, targetTemp, tempVal }) {
  const isSteamMode = mode === MODE_STEAM;
  return (
    (mode === MODE_BREW || mode === MODE_STEAM || mode === MODE_WATER || mode === MODE_MANUAL) &&
    (!active || isSteamMode) &&
    (!finished || isSteamMode) &&
    targetTemp > 0 &&
    tempVal < targetTemp
  );
}

export function shouldSendManualUpdate({ active, isManualMode, partial }) {
  if (!isManualMode) return false;
  if (active) return true;
  return Object.prototype.hasOwnProperty.call(partial ?? {}, 'temperature');
}

export function shouldKeepManualDraftDirty({ active, partial }) {
  return !active && Object.keys(partial ?? {}).length > 0;
}

export function computeYieldEditable({ allowYieldOverride, brewTarget, bluetoothConnected }) {
  return !!allowYieldOverride && !!brewTarget && !!bluetoothConnected;
}

export function getYieldLockReason({ allowYieldOverride, brewTarget, bluetoothConnected }) {
  const missingPrerequisites = [];
  if (!allowYieldOverride) missingPrerequisites.push('ENABLE YIELD OVERRIDE');
  if (!brewTarget) missingPrerequisites.push('VOLUMETRIC PROFILE REQUIRED');
  if (!bluetoothConnected) missingPrerequisites.push('SCALE NOT CONNECTED');
  return missingPrerequisites.length > 0 ? `YIELD LOCKED · ${missingPrerequisites.join(' · ')}` : null;
}

export function getReadinessSummary({ mode, connected, bluetoothConnected, selectedProfile, wakeAvailable }) {
  const signals = [
    mode === MODE_STANDBY ? 'Machine in standby' : 'Machine not in standby',
    connected ? 'Controller connected' : 'Controller offline',
    bluetoothConnected ? 'Scale connected' : 'Scale not connected',
    selectedProfile ? `Profile ${selectedProfile} selected` : 'No profile selected',
  ];
  if (wakeAvailable) signals.push('Wake available');
  return signals.join('. ');
}

export function getAvailableModeOptions(isGrindAvailable = true, isManualAvailable = true) {
  return MODE_OPTIONS.filter(option => {
    if (option.id === MODE_GRIND) return isGrindAvailable;
    if (option.id === MODE_MANUAL) return isManualAvailable;
    return true;
  });
}

export function getProcessKindForMode(mode, isGrindAvailable = true, isManualAvailable = true) {
  if (mode === MODE_BREW) return 'brew';
  if (mode === MODE_STEAM) return 'steam';
  if (mode === MODE_WATER) return 'water';
  if (mode === MODE_MANUAL && isManualAvailable) return 'manual';
  if (mode === MODE_GRIND && isGrindAvailable) return 'grind';
  return null;
}

export function getPrimaryActionState({ active, finished, mode, connected = true, isGrindAvailable = true, isManualAvailable = true }) {
  const isSteamMode = mode === MODE_STEAM;
  const isManualMode = mode === MODE_MANUAL;

  if (mode === MODE_GRIND && !isGrindAvailable) {
    return {
      label: 'GRIND UNAVAILABLE',
      accent: 'var(--dm-fg-dim)',
      action: 'noop',
      processKind: null,
    };
  }

  if (isManualMode && !isManualAvailable) {
    return {
      label: 'MANUAL UNAVAILABLE',
      accent: 'var(--dm-fg-dim)',
      action: 'noop',
      processKind: null,
    };
  }

  if (active && isManualMode) {
    return {
      label: 'STOP MANUAL',
      accent: 'var(--dm-accent)',
      action: 'deactivate',
      processKind: null,
    };
  }

  if (active && isSteamMode) {
    return {
      label: 'STOP STEAM',
      accent: 'var(--dm-warn)',
      action: 'change-mode',
      mode: MODE_STANDBY,
      processKind: null,
    };
  }

  if (active) {
    return {
      label: 'STOP SHOT',
      accent: 'var(--dm-accent)',
      action: 'deactivate',
    };
  }

  if (finished) {
    return {
      label: 'CLEAR',
      accent: isSteamMode ? 'var(--dm-warn)' : 'var(--dm-accent)',
      action: 'clear',
    };
  }

  if (mode === MODE_STANDBY) {
    if (!connected) {
      return {
        label: 'WAKE UNAVAILABLE',
        accent: 'var(--dm-fg-dim)',
        action: 'noop',
        processKind: null,
      };
    }

    // Wakes the machine into BREW (mirrors the physical brew button's first
    // press). Firmware maps req:process:activate in MODE_STANDBY to
    // deactivateStandby(). A second tap then starts the shot.
    return {
      label: 'WAKE',
      accent: 'var(--dm-accent)',
      action: 'start-process',
      processKind: null,
    };
  }

  return {
    label: isManualMode ? 'START MANUAL' : isSteamMode ? 'START STEAM' : 'START SHOT',
    accent: isSteamMode ? 'var(--dm-warn)' : 'var(--dm-accent)',
    action: 'start-process',
    processKind: getProcessKindForMode(mode, isGrindAvailable, isManualAvailable),
  };
}

// PRO-421: decide whether the auto-steam effect should fire change-mode:STEAM
// when a process transitions active -> inactive.
//
// Auto-steam must fire EXACTLY ONCE per brew/manual shot: after the shot ends we
// switch to steam. The subtle bug it guards against is a DOUBLE fire — when the
// auto-steamed steam session is itself stopped (web "Stop Steam" -> STANDBY),
// the effect must NOT re-fire STEAM, or the machine bounces straight back to
// Steam out of the Standby the user just asked for. The caller enforces the
// once-only semantics by clearing lastActiveWasBrew after a fire; this helper is
// the pure decision so it can be unit-tested.
//
//   lastActiveWasBrew - was the just-ended active session a brew/manual shot
//                       (as opposed to steam/water/grind)?
//   autoSteamEnabled  - the (device-authoritative) auto-steam setting.
export function shouldFireAutoSteamOnStop({ lastActiveWasBrew, autoSteamEnabled }) {
  return Boolean(lastActiveWasBrew) && Boolean(autoSteamEnabled);
}

// PRO-545: decide whether the standby-on-brew effect should fire
// change-mode:STANDBY when a process transitions active -> inactive.
//
// Like auto-steam, this must fire EXACTLY ONCE per brew/manual shot: after the
// shot ends we drop the machine to Standby. The caller enforces the once-only
// semantics by clearing lastActiveWasBrew after a fire; this helper is the pure
// decision so it can be unit-tested.
//
// Mutual exclusion: auto-steam wins. When auto-steam is enabled the standby-on
// button is disabled in the UI, but we also guard here so that even if both
// flags are somehow set, only the auto-steam STEAM transition fires and we do
// NOT also try to drop to Standby (the two post-shot actions conflict).
//
//   lastActiveWasBrew    - was the just-ended active session a brew/manual shot?
//   standbyOnBrewEnabled - the (device-authoritative) standby-on-brew setting.
//   autoSteamEnabled     - the (device-authoritative) auto-steam setting; when
//                          true it takes priority and this predicate is false.
export function shouldFireStandbyOnStop({ lastActiveWasBrew, standbyOnBrewEnabled, autoSteamEnabled }) {
  return Boolean(lastActiveWasBrew) && Boolean(standbyOnBrewEnabled) && !autoSteamEnabled;
}

// PRO-545: pure rendering state for the "Standby On" chip button. Mutual
// exclusion is enforced at the button: while auto-steam is enabled the chip is
// disabled (non-interactive, grayed) and shown un-armed regardless of the
// stored standby-on-brew value — but that value is PRESERVED (not cleared), so
// the chip resumes its armed appearance and behavior once auto-steam is turned
// back off. `armed` drives the active (danger) styling and the "ON" label.
export function computeStandbyOnBrewButtonState({ standbyOnBrewEnabled, autoSteamEnabled }) {
  const disabled = Boolean(autoSteamEnabled);
  return {
    disabled,
    armed: Boolean(standbyOnBrewEnabled) && !disabled,
  };
}

// PRO-426: standby profile mini-curve.
//
// Builds SVG polyline point strings for a pro profile's pressure + flow target
// curves so the standby shot card can render a small dark-themed preview WITHOUT
// pulling in the light-themed Chart.js ExtendedProfileChart. It mirrors the
// phase-interpolation semantics of ExtendedProfileChart.prepareData (a phase
// with pump.target === 'pressure'|'flow' ramps linearly from the previous value
// to its own; otherwise the axis holds a flat value), but samples per-phase
// endpoints only (enough for a mini preview) and emits ready-to-use viewBox
// coordinates. Pure + unit-testable; returns null when there is nothing to draw.
const STANDBY_CURVE_PRESSURE_MAX = 12; // bar (matches dashboard PRESSURE_MAX)
const STANDBY_CURVE_FLOW_MAX = 6; // g/s (matches dashboard FLOW_MAX)
const STANDBY_CURVE_TEMP_MAX = 110; // °C (matches dashboard TEMP_GRAPH_MAX)
const STANDBY_CURVE_TEMP_FALLBACK = 93; // profile-default when nothing usable (matches ExtendedProfileChart)

function phaseAxisValue(phase, axis, prev) {
  const pump = phase?.pump ?? {};
  let raw = pump[axis];
  // -1 means "use current value at phase start" (pro schema sentinel).
  if (raw === -1 || raw == null) raw = prev;
  const num = Number(raw);
  return Number.isFinite(num) ? Math.max(0, num) : 0;
}

// Temperature is a PHASE-LEVEL field (phase.temperature), NOT phase.pump.*.
// A missing / non-positive / NaN value carries the PREVIOUS sample forward
// (mirroring the phaseAxisValue pump-axis -1/null sentinel on this same curve),
// seeded by the anchor with the profile-level temperature (fallback
// STANDBY_CURVE_TEMP_FALLBACK = 93 when the profile has none).
//
// NOTE: this DIVERGES from ExtendedProfileChart.prepareTemperatureData, which
// RESETS to the profile temperature on each unset phase. Here the carry-forward
// is intentional — it produces a target-hold preview (an override on one phase
// keeps holding until a later phase overrides it again), which is consistent
// with the pump-axis carry-forward on this mini-curve. See PRO-433.
function phaseTemperatureValue(phase, prev) {
  const raw = Number.parseFloat(phase?.temperature);
  if (Number.isFinite(raw) && raw > 0) return Math.max(0, raw);
  return prev;
}

export function buildStandbyProfileCurve(profileData, { width, height, padding = 2 } = {}) {
  const phases = profileData?.phases;
  if (!Array.isArray(phases) || phases.length === 0) return null;
  if (!Number.isFinite(width) || !Number.isFinite(height) || width <= 0 || height <= 0) {
    return null;
  }

  // Accumulate total duration; bail if the profile has no time span to plot.
  const durations = phases.map(p => {
    const d = Number.parseFloat(p?.duration);
    return Number.isFinite(d) && d > 0 ? d : 0;
  });
  const totalDuration = durations.reduce((a, b) => a + b, 0);
  if (totalDuration <= 0) return null;

  const innerW = Math.max(1, width - padding * 2);
  const innerH = Math.max(1, height - padding * 2);

  const xAt = t => padding + (t / totalDuration) * innerW;
  const yAt = (value, max) => {
    const frac = Math.min(1, Math.max(0, value / max));
    // SVG y grows downward: full value sits near the top.
    return padding + (1 - frac) * innerH;
  };

  // Build endpoint samples. Each phase contributes its start point (carried from
  // the previous phase's end) and its end point.
  const pressurePts = [];
  const flowPts = [];
  const tempPts = [];
  let elapsed = 0;
  let prevPressure = phaseAxisValue(phases[0], 'pressure', 0);
  let prevFlow = phaseAxisValue(phases[0], 'flow', 0);

  // Temperature is profile-level with a per-phase override (phase.temperature).
  // Determine whether the profile carries any usable temperature at all; if not,
  // the temperature line is omitted (empty string) so the curve still renders
  // pressure + flow without a bogus flat line.
  const profileTemp = Number.parseFloat(profileData?.temperature);
  const hasProfileTemp = Number.isFinite(profileTemp) && profileTemp > 0;
  const hasPhaseTemp = phases.some(p => {
    const t = Number.parseFloat(p?.temperature);
    return Number.isFinite(t) && t > 0;
  });
  const hasTempData = hasProfileTemp || hasPhaseTemp;
  // Anchor temperature at the profile default (falling back to 93 like
  // ExtendedProfileChart) so the first phase can carry it forward.
  let prevTemp = hasProfileTemp ? profileTemp : STANDBY_CURVE_TEMP_FALLBACK;

  // Anchor at t=0.
  pressurePts.push([xAt(0), yAt(prevPressure, STANDBY_CURVE_PRESSURE_MAX)]);
  flowPts.push([xAt(0), yAt(prevFlow, STANDBY_CURVE_FLOW_MAX)]);
  if (hasTempData) tempPts.push([xAt(0), yAt(prevTemp, STANDBY_CURVE_TEMP_MAX)]);

  phases.forEach((phase, i) => {
    const dur = durations[i];
    if (dur <= 0) return;
    const endPressure = phaseAxisValue(phase, 'pressure', prevPressure);
    const endFlow = phaseAxisValue(phase, 'flow', prevFlow);
    const endTemp = phaseTemperatureValue(phase, prevTemp);
    elapsed += dur;
    pressurePts.push([xAt(elapsed), yAt(endPressure, STANDBY_CURVE_PRESSURE_MAX)]);
    flowPts.push([xAt(elapsed), yAt(endFlow, STANDBY_CURVE_FLOW_MAX)]);
    if (hasTempData) tempPts.push([xAt(elapsed), yAt(endTemp, STANDBY_CURVE_TEMP_MAX)]);
    prevPressure = endPressure;
    prevFlow = endFlow;
    prevTemp = endTemp;
  });

  const toPointsAttr = pts => pts.map(([x, y]) => `${x.toFixed(1)},${y.toFixed(1)}`).join(' ');

  return {
    pressure: toPointsAttr(pressurePts),
    flow: toPointsAttr(flowPts),
    temperature: toPointsAttr(tempPts),
    totalDuration,
    phaseCount: phases.length,
  };
}
