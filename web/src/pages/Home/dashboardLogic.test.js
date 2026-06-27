import { test, expect } from 'vitest';

import {
  MODE_GRIND,
  MODE_MANUAL,
  MODE_STANDBY,
  MODE_STEAM,
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
