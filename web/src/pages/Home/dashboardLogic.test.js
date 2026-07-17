import { test, expect } from 'vitest';

import {
  MODE_GRIND,
  MODE_MANUAL,
  MODE_STANDBY,
  MODE_STEAM,
  buildStandbyProfileCurve,
  clampManualFlow,
  clampManualPressure,
  clampManualTemperature,
  computeYieldEditable,
  getAvailableModeOptions,
  getBoilerHeatingState,
  getManualControlLabels,
  getPrimaryActionState,
  getProcessKindForMode,
  getTemperatureRingMetrics,
  shouldFireAutoSteamOnStop,
  shouldKeepManualDraftDirty,
  shouldSendManualUpdate,
} from './dashboardLogic.js';

test('steam temperature ring scales progress against the steam target', () => {
  const metrics = getTemperatureRingMetrics({
    mode: MODE_STEAM,
    tempVal: 105,
    targetTemp: 150,
  });

  expect(metrics.progressFraction).toBe(0.7);
  expect(metrics.targetFraction).toBe(1);
});

test('start steam is tracked as steam, not brew', () => {
  const state = getPrimaryActionState({
    active: false,
    finished: false,
    mode: MODE_STEAM,
  });

  expect(state.label).toBe('START STEAM');
  expect(state.action).toBe('start-process');
  expect(state.processKind).toBe('steam');
});

test('water mode is not tracked as brew for auto-steam', () => {
  expect(getProcessKindForMode(3)).toBe('water');
});

test('grind mode is not selectable when grind is unavailable', () => {
  expect(getProcessKindForMode(MODE_GRIND, false)).toBe(null);
});

test('available mode options omit unavailable optional modes', () => {
  const options = getAvailableModeOptions(false, false);

  expect(options.map(option => option.name)).toEqual(['STANDBY', 'BREW', 'STEAM', 'WATER']);
});

test('available mode options include manual before grind when grind is available', () => {
  const options = getAvailableModeOptions(true, true);

  expect(options.map(option => option.name)).toEqual(['STANDBY', 'BREW', 'STEAM', 'WATER', 'MANUAL', 'GRIND']);
});

test('available mode options include manual when only grind is unavailable', () => {
  const options = getAvailableModeOptions(false, true);

  expect(options.map(option => option.name)).toEqual(['STANDBY', 'BREW', 'STEAM', 'WATER', 'MANUAL']);
});

test('manual mode process kind is manual', () => {
  expect(getProcessKindForMode(MODE_MANUAL)).toBe('manual');
});

test('manual mode is unavailable without pressure capability', () => {
  expect(getProcessKindForMode(MODE_MANUAL, true, false)).toBe(null);

  const state = getPrimaryActionState({
    active: false,
    finished: false,
    mode: MODE_MANUAL,
    isManualAvailable: false,
  });

  expect(state.label).toBe('MANUAL UNAVAILABLE');
  expect(state.action).toBe('noop');
  expect(state.processKind).toBe(null);
});

test('manual primary action starts manual when armed', () => {
  const state = getPrimaryActionState({
    active: false,
    finished: false,
    mode: MODE_MANUAL,
  });

  expect(state.label).toBe('START MANUAL');
  expect(state.action).toBe('start-process');
  expect(state.processKind).toBe('manual');
});

test('manual primary action stops manual when running', () => {
  const state = getPrimaryActionState({
    active: true,
    finished: false,
    mode: MODE_MANUAL,
  });

  expect(state.label).toBe('STOP MANUAL');
  expect(state.action).toBe('deactivate');
  expect(state.processKind).toBe(null);
});

test('manual primary action clears manual when finished', () => {
  const state = getPrimaryActionState({
    active: false,
    finished: true,
    mode: MODE_MANUAL,
  });

  expect(state.label).toBe('CLEAR');
  expect(state.action).toBe('clear');
});

test('manual target labels change with target type', () => {
  expect(getManualControlLabels('pressure')).toEqual({
    pressure: 'PRESSURE TARGET',
    flow: 'FLOW LIMIT',
  });
  expect(getManualControlLabels('flow')).toEqual({
    pressure: 'PRESSURE LIMIT',
    flow: 'FLOW TARGET',
  });
});

test('manual target values are clamped to first-version bounds', () => {
  expect(clampManualTemperature(70)).toBe(80);
  expect(clampManualTemperature(110)).toBe(105);
  expect(clampManualPressure(-1)).toBe(0);
  expect(clampManualPressure(15)).toBe(12);
  expect(clampManualFlow(-1)).toBe(0);
  expect(clampManualFlow(7)).toBe(6);
});

test('manual temperature updates send before start while pump controls stay staged', () => {
  expect(shouldSendManualUpdate({
    active: false,
    isManualMode: true,
    partial: { temperature: 94 },
  })).toBe(true);
  expect(shouldSendManualUpdate({
    active: false,
    isManualMode: true,
    partial: { pressure: 8 },
  })).toBe(false);
  expect(shouldSendManualUpdate({
    active: true,
    isManualMode: true,
    partial: { pressure: 8 },
  })).toBe(true);
});

test('manual pre-start edits keep the local draft stable until start', () => {
  expect(shouldKeepManualDraftDirty({
    active: false,
    partial: { temperature: 94 },
  })).toBe(true);
  expect(shouldKeepManualDraftDirty({
    active: false,
    partial: { pressure: 8 },
  })).toBe(true);
  expect(shouldKeepManualDraftDirty({
    active: true,
    partial: { temperature: 94 },
  })).toBe(false);
});

test('manual mode heats like brew before the process starts', () => {
  expect(getBoilerHeatingState({
    mode: MODE_MANUAL,
    active: false,
    finished: false,
    targetTemp: 94,
    tempVal: 88,
  })).toBe(true);
  expect(getBoilerHeatingState({
    mode: MODE_MANUAL,
    active: true,
    finished: false,
    targetTemp: 94,
    tempVal: 88,
  })).toBe(false);
});

test('primary action for unavailable grind mode does not start a process', () => {
  const state = getPrimaryActionState({
    active: false,
    finished: false,
    mode: MODE_GRIND,
    isGrindAvailable: false,
  });

  expect(state.label).toBe('GRIND UNAVAILABLE');
  expect(state.action).toBe('noop');
  expect(state.processKind).toBe(null);
});

test('stop steam changes mode to standby and clears process tracking', () => {
  const state = getPrimaryActionState({
    active: true,
    finished: false,
    mode: MODE_STEAM,
  });

  expect(state.label).toBe('STOP STEAM');
  expect(state.action).toBe('change-mode');
  expect(state.mode).toBe(0);
  expect(state.processKind).toBe(null);
});

test('auto-steam fires only for a brew/manual shot when enabled (PRO-421)', () => {
  // Brew shot just ended, auto-steam on -> fire steam.
  expect(shouldFireAutoSteamOnStop({ lastActiveWasBrew: true, autoSteamEnabled: true })).toBe(true);
  // Auto-steam off -> never fire.
  expect(shouldFireAutoSteamOnStop({ lastActiveWasBrew: true, autoSteamEnabled: false })).toBe(false);
  // Not a brew/manual session (e.g. the auto-steamed STEAM session being
  // stopped, after the caller clears lastActiveWasBrew) -> never re-fire. This
  // is the bounce guard: pressing Stop-Steam must not re-send change-mode:STEAM
  // right after the STANDBY.
  expect(shouldFireAutoSteamOnStop({ lastActiveWasBrew: false, autoSteamEnabled: true })).toBe(false);
  expect(shouldFireAutoSteamOnStop({ lastActiveWasBrew: false, autoSteamEnabled: false })).toBe(false);
});

test('standby primary action wakes the machine into brew', () => {
  const state = getPrimaryActionState({ active: false, finished: false, mode: MODE_STANDBY });
  expect(state.label).toBe('WAKE');
  expect(state.action).toBe('start-process');
  expect(state.accent).toBe('var(--dm-accent)');
  expect(state.processKind).toBe(null);
});

test('yield is editable when override is on, the profile is volumetric, and the scale is connected', () => {
  expect(
    computeYieldEditable({ allowYieldOverride: true, brewTarget: true, bluetoothConnected: true })
  ).toBe(true);
});

test('yield is locked when the override is off', () => {
  expect(
    computeYieldEditable({ allowYieldOverride: false, brewTarget: true, bluetoothConnected: true })
  ).toBe(false);
});

test('yield is locked when the profile is not volumetric', () => {
  expect(
    computeYieldEditable({ allowYieldOverride: true, brewTarget: false, bluetoothConnected: true })
  ).toBe(false);
});

test('yield is locked when the scale is disconnected', () => {
  expect(
    computeYieldEditable({ allowYieldOverride: true, brewTarget: true, bluetoothConnected: false })
  ).toBe(false);
});

test('yield is locked when all conditions are false', () => {
  expect(
    computeYieldEditable({ allowYieldOverride: false, brewTarget: false, bluetoothConnected: false })
  ).toBe(false);
});

// PRO-426: standby profile mini-curve builder.
test('standby curve returns null for missing / empty / non-array phases', () => {
  expect(buildStandbyProfileCurve(null, { width: 100, height: 40 })).toBeNull();
  expect(buildStandbyProfileCurve({}, { width: 100, height: 40 })).toBeNull();
  expect(buildStandbyProfileCurve({ phases: [] }, { width: 100, height: 40 })).toBeNull();
  expect(buildStandbyProfileCurve({ phases: 'nope' }, { width: 100, height: 40 })).toBeNull();
});

test('standby curve returns null for invalid dimensions', () => {
  const p = { phases: [{ duration: 10, pump: { pressure: 9 } }] };
  expect(buildStandbyProfileCurve(p, { width: 0, height: 40 })).toBeNull();
  expect(buildStandbyProfileCurve(p, { width: 100, height: -1 })).toBeNull();
  expect(buildStandbyProfileCurve(p, {})).toBeNull();
});

test('standby curve returns null when profile has zero total duration', () => {
  const p = { phases: [{ duration: 0, pump: { pressure: 9 } }, { pump: { pressure: 6 } }] };
  expect(buildStandbyProfileCurve(p, { width: 100, height: 40 })).toBeNull();
});

test('standby curve maps a two-phase pressure profile to viewBox points', () => {
  // Phase 1: 10s ramp to 9 bar; phase 2: 20s hold at 9 bar. Max pressure = 12.
  const profile = {
    phases: [
      { duration: 10, pump: { target: 'pressure', pressure: 9, flow: 0 } },
      { duration: 20, pump: { target: 'pressure', pressure: 9, flow: 0 } },
    ],
  };
  const curve = buildStandbyProfileCurve(profile, { width: 102, height: 42, padding: 2 });
  expect(curve).not.toBeNull();
  expect(curve.totalDuration).toBe(30);
  expect(curve.phaseCount).toBe(2);
  // 3 points: t=0, end of phase 1 (t=10), end of phase 2 (t=30).
  const pts = curve.pressure.split(' ');
  expect(pts).toHaveLength(3);
  // inner width = 98 (102 - 2*2), padding 2. x at t=0 -> 2.0; t=30 -> 100.0.
  expect(pts[0].split(',')[0]).toBe('2.0');
  expect(pts[2].split(',')[0]).toBe('100.0');
  // y for 9/12 bar: padding + (1 - 0.75)*innerH(38) = 2 + 9.5 = 11.5.
  expect(pts[1].split(',')[1]).toBe('11.5');
});

test('standby curve treats -1 sentinel as carry-over of previous value', () => {
  const profile = {
    phases: [
      { duration: 5, pump: { target: 'pressure', pressure: 6, flow: 2 } },
      { duration: 5, pump: { target: 'pressure', pressure: -1, flow: -1 } },
    ],
  };
  const curve = buildStandbyProfileCurve(profile, { width: 100, height: 40 });
  const pPts = curve.pressure.split(' ');
  // End-of-phase-1 y and end-of-phase-2 y must match (6 bar carried through).
  expect(pPts[1].split(',')[1]).toBe(pPts[2].split(',')[1]);
});

// PRO-432: standby profile mini-curve now includes a target-temperature line.
test('standby curve maps per-phase target temperature to viewBox points', () => {
  // Profile default 93 °C; phase 1 overrides to 92, phase 2 to 96. Max = 110.
  const profile = {
    temperature: 93,
    phases: [
      { duration: 10, temperature: 92, pump: { target: 'pressure', pressure: 9, flow: 0 } },
      { duration: 20, temperature: 96, pump: { target: 'pressure', pressure: 9, flow: 0 } },
    ],
  };
  const curve = buildStandbyProfileCurve(profile, { width: 102, height: 42, padding: 2 });
  expect(curve).not.toBeNull();
  const tPts = curve.temperature.split(' ');
  // 3 points: anchor (profile default), end phase 1, end phase 2.
  expect(tPts).toHaveLength(3);
  // Anchor uses the profile temperature (93). innerH = 38, padding 2.
  // y = 2 + (1 - 93/110)*38 = 2 + 0.15454*38 = 7.9 (1 d.p.).
  expect(tPts[0].split(',')[1]).toBe('7.9');
  // End of phase 1 (92 °C): y = 2 + (1 - 92/110)*38 = 8.2.
  expect(tPts[1].split(',')[1]).toBe('8.2');
  // End of phase 2 (96 °C): y = 2 + (1 - 96/110)*38 = 6.8.
  expect(tPts[2].split(',')[1]).toBe('6.8');
});

test('standby curve carries the profile temperature through phases with no temperature (sentinel)', () => {
  // No per-phase temperature -> "use current value" falls back to the profile
  // temperature (95) and holds it flat across both phases.
  const profile = {
    temperature: 95,
    phases: [
      { duration: 5, pump: { target: 'pressure', pressure: 6, flow: 2 } },
      { duration: 5, temperature: -1, pump: { target: 'pressure', pressure: 6, flow: 2 } },
    ],
  };
  const curve = buildStandbyProfileCurve(profile, { width: 100, height: 40 });
  const tPts = curve.temperature.split(' ');
  expect(tPts).toHaveLength(3);
  // All three temperature samples share the same y (95 °C carried through).
  const ys = tPts.map(p => p.split(',')[1]);
  expect(ys[0]).toBe(ys[1]);
  expect(ys[1]).toBe(ys[2]);
});

test('standby curve carries a mid-profile temperature override forward (diverges from ExtendedProfileChart reset)', () => {
  // PRO-433: profile default 93 °C; phase 1 overrides to a higher 100 °C; phase 2
  // has NO temperature. The mini-curve CARRIES the phase-1 override FORWARD
  // (target-hold preview) rather than RESETTING to the profile temperature the
  // way ExtendedProfileChart.prepareTemperatureData does. Pin that divergence:
  // phase 2's sample must equal phase 1's override, NOT the profile anchor.
  const profile = {
    temperature: 93,
    phases: [
      { duration: 10, temperature: 100, pump: { target: 'pressure', pressure: 9, flow: 0 } },
      { duration: 10, pump: { target: 'pressure', pressure: 9, flow: 0 } },
    ],
  };
  const curve = buildStandbyProfileCurve(profile, { width: 102, height: 42, padding: 2 });
  expect(curve).not.toBeNull();
  const ys = curve.temperature.split(' ').map(p => p.split(',')[1]);
  // 3 points: anchor (profile 93), end phase 1 (override 100), end phase 2 (carry-forward).
  expect(ys).toHaveLength(3);
  // Phase 2 carries phase 1's override forward, so its y matches phase 1...
  expect(ys[2]).toBe(ys[1]);
  // ...and is NOT reset to the profile anchor (which would be the ExtendedProfileChart behavior).
  expect(ys[2]).not.toBe(ys[0]);
});

test('standby curve omits the temperature line when no temperature data exists', () => {
  // Neither the profile nor any phase carries a usable temperature.
  const profile = {
    phases: [
      { duration: 10, pump: { target: 'pressure', pressure: 9, flow: 0 } },
      { duration: 20, pump: { target: 'pressure', pressure: 9, flow: 0 } },
    ],
  };
  const curve = buildStandbyProfileCurve(profile, { width: 100, height: 40 });
  expect(curve).not.toBeNull();
  // Temperature line is empty, but pressure + flow still render.
  expect(curve.temperature).toBe('');
  expect(curve.pressure).not.toBe('');
  expect(curve.flow).not.toBe('');
});

