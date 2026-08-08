// PRO-639 regression: the dashboard header's GitHub icon must link to Carlos's
// fork (the repo this firmware is actually built from), not the upstream
// jniebuhr/gaggimate repo. Guards the href plus the new-tab hardening
// (rel="noopener noreferrer" + target="_blank") that must survive URL edits.

import { afterEach, beforeEach, expect, test, vi } from 'vitest';
import { h } from 'preact';
import { cleanup, render, screen } from '@testing-library/preact';

vi.mock('@fortawesome/react-fontawesome', () => ({
  FontAwesomeIcon: () => h('span', { 'data-testid': 'fa-icon' }),
}));

// useGrindSettings' settings fetch — irrelevant to the header link.
vi.mock('preact-fetching', () => ({
  useQuery: () => ({ isLoading: false, isError: false, data: {} }),
}));

vi.mock('../../services/localAuthFetch.js', () => ({
  authenticatedFetch: vi.fn(() => Promise.resolve({ json: async () => ({}) })),
  LOCAL_AUTH_TOKEN_KEY: 'gaggimate_local_admin_token',
}));

import { ApiServiceContext, machine } from '../../services/ApiService.js';
import DashboardMerged from './DashboardMerged.jsx';

const FORK_URL = 'https://github.com/carloshrdezc/gaggimate';

beforeEach(() => {
  // jsdom has no matchMedia; the dashboard's mobile-layout effect needs one.
  vi.stubGlobal('matchMedia', () => ({
    matches: false,
    addEventListener: () => {},
    removeEventListener: () => {},
  }));

  machine.value = {
    ...machine.value,
    connected: true,
    status: { ...machine.value.status, mode: 1, process: null },
  };
});

afterEach(() => {
  cleanup();
  vi.restoreAllMocks();
});

function renderDashboard() {
  const api = {
    send: vi.fn(),
    request: vi.fn(async () => ({})),
    on: vi.fn(() => () => {}),
  };
  render(h(ApiServiceContext.Provider, { value: api }, h(DashboardMerged, {})));
}

test('points the dashboard GitHub link at the fork this firmware is built from', () => {
  renderDashboard();

  const link = screen.getByRole('link', { name: 'github' });
  expect(link.getAttribute('href')).toBe(FORK_URL);
});

test('keeps the GitHub link opening in a hardened new tab', () => {
  renderDashboard();

  const link = screen.getByRole('link', { name: 'github' });
  expect(link.getAttribute('target')).toBe('_blank');
  expect(link.getAttribute('rel')).toBe('noopener noreferrer');
});
