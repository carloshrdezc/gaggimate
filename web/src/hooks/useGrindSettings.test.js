import { test, expect } from 'vitest';

import { getGrindSettingsState } from './useGrindSettings.js';

test('grind is unavailable when settings have not loaded', () => {
  const state = getGrindSettingsState(null);

  expect(state.altRelayFunction).toBe(0);
  expect(state.isGrindAvailable).toBe(false);
  expect(state.showGrindTab).toBe(false);
});

test('grind is available when alt relay is explicitly configured for grind', () => {
  const state = getGrindSettingsState({ smartGrindActive: false, altRelayFunction: 1 });

  expect(state.isGrindAvailable).toBe(true);
  expect(state.showGrindTab).toBe(true);
});

test('grind is available when smart grind is active', () => {
  const state = getGrindSettingsState({ smartGrindActive: true, altRelayFunction: 0 });

  expect(state.isGrindAvailable).toBe(true);
  expect(state.showGrindTab).toBe(true);
});
