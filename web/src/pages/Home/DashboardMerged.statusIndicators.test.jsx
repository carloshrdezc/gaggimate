// PRO-640 regression: the dashboard must NOT render the concatenated readiness
// sentence as a dedicated visible line. Connection health is a compact pill pair
// (ONLINE / SCALE) whose LED tone only escalates when the missing link actually
// blocks something; the sentence survives only as a visually-hidden aria-live
// region that speaks on meaningful transitions. Standby keeps the selected
// profile's name visible.

import { h } from 'preact';
import { afterEach, beforeEach, expect, test, vi } from 'vitest';
import { cleanup, render, screen } from '@testing-library/preact';

vi.mock('@fortawesome/react-fontawesome', () => ({
  FontAwesomeIcon: () => h('span', { 'data-testid': 'fa-icon' }),
}));

// useGrindSettings' settings fetch — irrelevant to the status indicators.
vi.mock('preact-fetching', () => ({
  useQuery: () => ({ isLoading: false, isError: false, data: {} }),
}));

vi.mock('../../services/localAuthFetch.js', () => ({
  authenticatedFetch: vi.fn(() => Promise.resolve({ json: async () => ({}) })),
  LOCAL_AUTH_TOKEN_KEY: 'gaggimate_local_admin_token',
}));

import { ApiServiceContext, machine } from '../../services/ApiService.js';
import DashboardMerged, { StandbyBlock } from './DashboardMerged.jsx';
import * as dashboardLogic from './dashboardLogic.js';
import {
  MODE_BREW,
  MODE_STANDBY,
  MODE_STEAM,
  getConnectivityIndicators,
  getReadinessAnnouncement,
  getReadinessSignals,
} from './dashboardLogic.js';

const FULL_SENTENCE_FRAGMENT = 'Machine in standby. Controller connected.';

beforeEach(() => {
  // jsdom has no matchMedia; the dashboard's mobile-layout effect needs one.
  vi.stubGlobal('matchMedia', () => ({
    matches: false,
    addEventListener: () => {},
    removeEventListener: () => {},
  }));
});

afterEach(() => {
  cleanup();
  vi.restoreAllMocks();
});

function setMachine({ connected = true, mode = MODE_STANDBY, bluetoothConnected = false, selectedProfile = 'Morning espresso' } = {}) {
  machine.value = {
    ...machine.value,
    connected,
    status: {
      ...machine.value.status,
      mode,
      process: null,
      bluetoothConnected,
      selectedProfile,
    },
  };
}

function renderDashboard() {
  const api = {
    send: vi.fn(),
    request: vi.fn(async () => ({})),
    on: vi.fn(() => () => {}),
  };
  return render(h(ApiServiceContext.Provider, { value: api }, h(DashboardMerged, {})));
}

// ── The visible line is gone ─────────────────────────────────────────────────

test('does not render the concatenated readiness sentence anywhere visible', () => {
  setMachine();
  renderDashboard();

  // The sentence the sub-header used to display verbatim.
  expect(screen.queryByText(new RegExp(FULL_SENTENCE_FRAGMENT))).toBeNull();
  expect(document.body.textContent).not.toContain(FULL_SENTENCE_FRAGMENT);
});

test('keeps connection health discoverable as compact pills with explanatory tooltips', () => {
  setMachine({ connected: true, bluetoothConnected: false, mode: MODE_STANDBY });
  renderDashboard();

  expect(screen.getByText('ONLINE')).toBeTruthy();
  expect(screen.getByTitle('Controller connected')).toBeTruthy();
  expect(screen.getByText('SCALE')).toBeTruthy();
  expect(screen.getByTitle('Scale not connected — not needed right now')).toBeTruthy();
});

test('keeps an empty polite live region mounted so later transitions are announced', () => {
  setMachine();
  renderDashboard();

  const live = screen.getByTestId('readiness-live-region');
  expect(live.getAttribute('role')).toBe('status');
  expect(live.getAttribute('aria-live')).toBe('polite');
  // Nothing is narrated on first paint — the freshly opened dashboard is not news.
  expect(live.textContent).toBe('');
  // And it takes no layout space (clipped, 1x1).
  expect(live.style.position).toBe('absolute');
  expect(live.style.clipPath).toBe('inset(50%)');
});

test('announces only the axis that changed when the controller drops', async () => {
  setMachine({ connected: true });
  renderDashboard();

  setMachine({ connected: false });
  const live = await vi.waitFor(() => {
    const node = screen.getByTestId('readiness-live-region');
    expect(node.textContent).not.toBe('');
    return node;
  });

  expect(live.textContent).toBe('Controller offline');
  // Not the whole sentence — the unchanged axes stay silent.
  expect(live.textContent).not.toContain('Machine in standby');
  expect(live.textContent).not.toContain('Scale');
});

// ── Standby keeps the selected-profile context ───────────────────────────────

test('shows the selected profile name in the standby card', () => {
  render(h(StandbyBlock, { profileName: 'Morning espresso', curve: null }));
  expect(screen.getByText('Morning espresso')).toBeTruthy();
});

test('shows an explicit empty state instead of a blank profile label', () => {
  render(h(StandbyBlock, { profileName: '', curve: null }));
  expect(screen.getByText('NONE SELECTED')).toBeTruthy();
});

// ── Pure logic ───────────────────────────────────────────────────────────────

test('the concatenated readiness sentence is no longer built for rendering', () => {
  // PRO-640 documentation-as-test: getReadinessSummary was deliberately removed
  // (its only remaining consumer was the visible line this issue deleted). If it
  // comes back, the sentence is probably being rendered again — reject it here.
  expect(dashboardLogic.getReadinessSummary).toBeUndefined();
});

test('announcement is empty on the first render and when nothing changed', () => {
  const signals = getReadinessSignals({
    mode: MODE_STANDBY,
    connected: true,
    bluetoothConnected: false,
    selectedProfile: 'A',
  });

  expect(getReadinessAnnouncement(null, signals)).toBe('');
  expect(getReadinessAnnouncement(signals, signals)).toBe('');
});

test('announcement names every changed axis and nothing else', () => {
  const before = getReadinessSignals({
    mode: MODE_STANDBY,
    connected: true,
    bluetoothConnected: false,
    selectedProfile: 'A',
  });
  const after = getReadinessSignals({
    mode: MODE_BREW,
    connected: true,
    bluetoothConnected: true,
    selectedProfile: 'A',
  });

  expect(getReadinessAnnouncement(before, after)).toBe(
    'Machine not in standby. Scale connected'
  );
});

test('a withdrawn wake affordance is not announced', () => {
  const base = { mode: MODE_STANDBY, connected: true, bluetoothConnected: false, selectedProfile: 'A' };
  const withWake = getReadinessSignals({ ...base, wakeAvailable: true });
  const withoutWake = getReadinessSignals({ ...base, wakeAvailable: false });

  expect(getReadinessAnnouncement(withoutWake, withWake)).toBe('Wake available');
  expect(getReadinessAnnouncement(withWake, withoutWake)).toBe('');
});

test('a disconnected controller always demands attention', () => {
  const indicators = getConnectivityIndicators({
    connected: false,
    bluetoothConnected: true,
    mode: MODE_STANDBY,
  });
  expect(indicators.controller.tone).toBe('attention');
});

test('a missing scale is neutral while nothing depends on it', () => {
  // Idle, no volumetric target: a missing scale is the normal resting state.
  expect(
    getConnectivityIndicators({ connected: true, bluetoothConnected: false, mode: MODE_STANDBY, brewTarget: false })
      .scale.tone
  ).toBe('idle');
  // Volumetric profile selected, but the machine is steaming — the scale is not
  // in the loop right now.
  expect(
    getConnectivityIndicators({ connected: true, bluetoothConnected: false, mode: MODE_STEAM, brewTarget: true })
      .scale.tone
  ).toBe('idle');
  // Brew mode without a volumetric target stops on time/pressure, not weight.
  expect(
    getConnectivityIndicators({ connected: true, bluetoothConnected: false, mode: MODE_BREW, brewTarget: false })
      .scale.tone
  ).toBe('idle');
});

test('a missing scale demands attention when the brew depends on weight', () => {
  expect(
    getConnectivityIndicators({ connected: true, bluetoothConnected: false, mode: MODE_BREW, brewTarget: true })
      .scale.tone
  ).toBe('attention');
  // Also mid-shot, whatever the mode reads by then.
  expect(
    getConnectivityIndicators({
      connected: true,
      bluetoothConnected: false,
      mode: MODE_STANDBY,
      brewTarget: true,
      active: true,
    }).scale.tone
  ).toBe('attention');
});

test('a connected scale is always ok', () => {
  expect(
    getConnectivityIndicators({ connected: true, bluetoothConnected: true, mode: MODE_BREW, brewTarget: true })
      .scale.tone
  ).toBe('ok');
});

// PRO-640 regression: on a GAGGIMATE_ENABLE_BLE_SCALE=0 firmware the status
// payload hardcodes bc:false, yet volumetric brewing still reaches its target
// via flow estimation. Amber there is a permanent, un-actionable fault light, so
// the pill must stay neutral for the whole volumetric brew.
test('a BLE-scale-disabled build never demands attention for its absent scale', () => {
  const midVolumetricBrew = {
    connected: true,
    bluetoothConnected: false,
    mode: MODE_BREW,
    brewTarget: true,
    active: true,
    bluetoothScaleEnabled: false,
  };
  const indicators = getConnectivityIndicators(midVolumetricBrew);
  expect(indicators.scale.tone).toBe('idle');
  expect(indicators.scale.label).toBe('Scale not connected — this build uses flow estimation');
  // Not just mid-shot: idle in Brew mode before starting, too.
  expect(getConnectivityIndicators({ ...midVolumetricBrew, active: false }).scale.tone).toBe('idle');
  // The controller pill is untouched by the BLE feature flag.
  expect(indicators.controller.tone).toBe('ok');
});

// The same volumetric brew on a BLE-capable build (the case the amber pill
// exists for) must keep escalating — the fix above must not blanket-silence it.
test('the BLE-scale flag defaults to enabled so legacy firmware still escalates', () => {
  expect(
    getConnectivityIndicators({
      connected: true,
      bluetoothConnected: false,
      mode: MODE_BREW,
      brewTarget: true,
      bluetoothScaleEnabled: undefined,
    }).scale.tone
  ).toBe('attention');
  expect(
    getConnectivityIndicators({
      connected: true,
      bluetoothConnected: false,
      mode: MODE_BREW,
      brewTarget: true,
      bluetoothScaleEnabled: true,
    }).scale.tone
  ).toBe('attention');
});
