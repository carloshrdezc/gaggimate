import { test, expect } from 'vitest';

import {
  normalizeSettings,
  parseAutoWakeupSchedules,
  DEFAULT_AUTOWAKEUP_SCHEDULE,
} from './settingsNormalize.js';

const ORDER_FIRST = 'order-first';

test('parses a multi-entry autowakeup schedule string into the editor array', () => {
  const schedules = parseAutoWakeupSchedules('06:30|1111100;08:15|0000011');
  expect(schedules).toEqual([
    { time: '06:30', days: [true, true, true, true, true, false, false] },
    { time: '08:15', days: [false, false, false, false, false, true, true] },
  ]);
});

test('parses a single-entry custom schedule preserving per-day flags', () => {
  const schedules = parseAutoWakeupSchedules('09:45|1010101');
  expect(schedules).toEqual([
    { time: '09:45', days: [true, false, true, false, true, false, true] },
  ]);
});

test('falls back to the default schedule for empty/absent/malformed input', () => {
  expect(parseAutoWakeupSchedules('')).toEqual([DEFAULT_AUTOWAKEUP_SCHEDULE()]);
  expect(parseAutoWakeupSchedules(undefined)).toEqual([DEFAULT_AUTOWAKEUP_SCHEDULE()]);
  // Malformed (wrong day-string length) entries are dropped, then default applies.
  expect(parseAutoWakeupSchedules('07:00|111')).toEqual([DEFAULT_AUTOWAKEUP_SCHEDULE()]);
});

test('device-fetch shape: splits combined pid CSV into pid + kf', () => {
  const { formData, autowakeupSchedules } = normalizeSettings(
    {
      pid: '3.000,0.100,40.000,0.250',
      autowakeupSchedules: '06:30|1111100',
      standbyBrightness: 50,
    },
    { defaultDashboardLayout: ORDER_FIRST },
  );

  expect(formData.pid).toBe('3.000,0.100,40.000');
  expect(formData.kf).toBe('0.250');
  expect(formData.standbyDisplayEnabled).toBe(true);
  expect(formData.dashboardLayout).toBe(ORDER_FIRST);
  expect(autowakeupSchedules).toEqual([
    { time: '06:30', days: [true, true, true, true, true, false, false] },
  ]);
});

test('imported (already-split) shape: preserves explicit kf so PID round-trips', () => {
  const { formData } = normalizeSettings(
    { pid: '3.000,0.100,40.000', kf: '0.250' },
    { defaultDashboardLayout: ORDER_FIRST },
  );

  expect(formData.pid).toBe('3.000,0.100,40.000');
  expect(formData.kf).toBe('0.250');
});

test('already-split pid with no kf defaults kf to 0.000', () => {
  const { formData } = normalizeSettings(
    { pid: '3.000,0.100,40.000' },
    { defaultDashboardLayout: ORDER_FIRST },
  );

  expect(formData.kf).toBe('0.000');
});

test('export -> import round-trips a custom multi-entry schedule and PID', () => {
  // Simulate device fetch, then export (formData), then re-import.
  const deviceGet = {
    pid: '3.000,0.100,40.000,0.250',
    autowakeupSchedules: '06:30|1111100;08:15|0000011',
    standbyBrightness: 50,
  };
  const fetched = normalizeSettings(deviceGet, { defaultDashboardLayout: ORDER_FIRST });

  // formData (what onExport dumps) keeps the raw device schedule string and the
  // split pid/kf. Re-importing it must reproduce the same editor state.
  const exported = fetched.formData;
  const reimported = normalizeSettings(exported, { defaultDashboardLayout: ORDER_FIRST });

  expect(reimported.autowakeupSchedules).toEqual(fetched.autowakeupSchedules);
  expect(reimported.formData.pid).toBe('3.000,0.100,40.000');
  expect(reimported.formData.kf).toBe('0.250');
});

test('standbyDisplayEnabled honours an explicit value over brightness', () => {
  const { formData } = normalizeSettings(
    { standbyDisplayEnabled: false, standbyBrightness: 80 },
    { defaultDashboardLayout: ORDER_FIRST },
  );
  expect(formData.standbyDisplayEnabled).toBe(false);
});
