// PRO-630 regression: a successful req:brew-temperature:set response must never
// repopulate the pending (optimistic) temperature after the selected profile has
// already moved on. Reviewer-flagged race on PR #627: another client switches the
// profile while this client's write is in flight, the evt:status-driven effect
// correctly clears the pending value for the new profile, and the late response
// then puts profile A's echoed temperature on screen while profile B is selected.

import { afterEach, beforeEach, expect, test, vi } from 'vitest';
import { h } from 'preact';
import { act, cleanup, fireEvent, render, screen, waitFor } from '@testing-library/preact';

vi.mock('@fortawesome/react-fontawesome', () => ({
  FontAwesomeIcon: () => h('span', { 'data-testid': 'fa-icon' }),
}));

// useGrindSettings' settings fetch — irrelevant to the TEMP field.
vi.mock('preact-fetching', () => ({
  useQuery: () => ({ isLoading: false, isError: false, data: {} }),
}));

vi.mock('../../services/localAuthFetch.js', () => ({
  authenticatedFetch: vi.fn(() => Promise.resolve({ json: async () => ({}) })),
  LOCAL_AUTH_TOKEN_KEY: 'gaggimate_local_admin_token',
}));

import { ApiServiceContext, machine } from '../../services/ApiService.js';
import DashboardMerged from './DashboardMerged.jsx';

const PROFILE_A = 'profile-a';
const PROFILE_B = 'profile-b';
// Device-authoritative `bto` values published in evt:status for each profile.
const PROFILE_A_TARGET = 93;
const PROFILE_B_TARGET = 90;

function deferred() {
  let resolve;
  const promise = new Promise(r => {
    resolve = r;
  });
  return { promise, resolve };
}

/** Publish an evt:status frame (what the ApiService signal update looks like). */
function publishStatus(overrides) {
  act(() => {
    machine.value = {
      ...machine.value,
      connected: true,
      status: { ...machine.value.status, ...overrides },
    };
  });
}

function renderDashboard(pendingSet) {
  const api = {
    send: vi.fn(),
    request: vi.fn(async payload => {
      if (payload.tp === 'req:brew-temperature:set') return pendingSet.promise;
      // Profile list / profile load fired by useProfileData.
      return {};
    }),
    on: vi.fn(() => () => {}),
  };
  render(h(ApiServiceContext.Provider, { value: api }, h(DashboardMerged, {})));
  return api;
}

/** Current on-screen TEMP value (the edit trigger renders the number). */
function tempValue() {
  return screen.getByRole('button', { name: 'Edit TEMP' }).textContent;
}

/** Commit a +0.5 °C edit through the TEMP stepper. */
function bumpTemp() {
  fireEvent.click(screen.getByRole('button', { name: 'Increase TEMP' }));
}

beforeEach(() => {
  // jsdom has no matchMedia; the dashboard's mobile-layout effect needs one.
  vi.stubGlobal('matchMedia', () => ({
    matches: false,
    addEventListener: () => {},
    removeEventListener: () => {},
  }));

  // Brew mode (1), connected, no active process, profile A selected with a
  // published target => the TEMP field is editable.
  machine.value = {
    ...machine.value,
    connected: true,
    status: {
      ...machine.value.status,
      mode: 1,
      process: null,
      selectedProfileId: PROFILE_A,
      selectedProfile: 'Profile A',
      brewTemperatureOverrideTarget: PROFILE_A_TARGET,
      brewTemperatureOverrideEnabled: false,
    },
  };
});

afterEach(() => {
  cleanup();
  vi.restoreAllMocks();
});

test('drops a successful brew-temperature response when the profile changed while it was in flight', async () => {
  const pendingSet = deferred();
  const api = renderDashboard(pendingSet);

  expect(tempValue()).toBe(PROFILE_A_TARGET.toFixed(1));

  // 1. Fire the write for profile A. It does NOT resolve yet.
  bumpTemp();
  await waitFor(() =>
    expect(api.request).toHaveBeenCalledWith({
      tp: 'req:brew-temperature:set',
      temperature: PROFILE_A_TARGET + 0.5,
    })
  );
  // Optimistic value is on screen while the request is outstanding.
  expect(tempValue()).toBe((PROFILE_A_TARGET + 0.5).toFixed(1));

  // 2. Another client switches the selected profile: evt:status carries profile
  //    B and its own `bto`. The clear-effect drops the pending value.
  publishStatus({
    selectedProfileId: PROFILE_B,
    selectedProfile: 'Profile B',
    brewTemperatureOverrideTarget: PROFILE_B_TARGET,
  });
  expect(tempValue()).toBe(PROFILE_B_TARGET.toFixed(1));

  // 3. Only now does profile A's request answer, echoing profile A's value.
  await act(async () => {
    pendingSet.resolve({ ok: true, temperature: PROFILE_A_TARGET + 0.5 });
    await pendingSet.promise;
  });

  // The stale echo must not repopulate pending state: profile B's
  // device-authoritative target still owns the field.
  expect(tempValue()).toBe(PROFILE_B_TARGET.toFixed(1));
});

test('still shows the device echo when the profile is unchanged', async () => {
  const pendingSet = deferred();
  const api = renderDashboard(pendingSet);

  bumpTemp();
  await waitFor(() => expect(api.request).toHaveBeenCalled());

  // Device clamps/rounds to its own value and echoes it back.
  await act(async () => {
    pendingSet.resolve({ ok: true, temperature: 94 });
    await pendingSet.promise;
  });

  expect(tempValue()).toBe('94.0');
});
