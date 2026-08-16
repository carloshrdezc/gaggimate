// PRO-643 regression: the header "G" badge is painted on the *dashboard accent*,
// which the user can now set to any color. It used to hardcode `color: '#fff'`,
// so a light accent (e.g. #ffffff) produced white text on a white badge —
// unreadable. Both header implementations (the shared PageShell chrome and the
// Dashboard's own inlined header) must route the badge foreground through the
// `--gm-dashboard-accent-content` token themeManager.js derives for contrast.
//
// jsdom does not resolve `var()` in getComputedStyle, so we resolve the token
// chain ourselves against the inline root custom properties the real (unmocked)
// themeManager writes. That asserts the end-to-end result — a readable
// foreground — not merely the presence of a variable name.

import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { h } from 'preact';
import { cleanup, render, screen } from '@testing-library/preact';

vi.mock('@fortawesome/react-fontawesome', () => ({
  FontAwesomeIcon: () => h('span', { 'data-testid': 'fa-icon' }),
}));

// DashboardMerged's useGrindSettings fetch — irrelevant to the header badge.
vi.mock('preact-fetching', () => ({
  useQuery: () => ({ isLoading: false, isError: false, data: {} }),
}));

vi.mock('../services/localAuthFetch.js', () => ({
  authenticatedFetch: vi.fn(() => Promise.resolve({ json: async () => ({}) })),
  LOCAL_AUTH_TOKEN_KEY: 'gaggimate_local_admin_token',
}));

import { ApiServiceContext, machine } from '../services/ApiService.js';
import { applyThemePreferences } from '../utils/themeManager.js';
import { PageShell } from './PageShell.jsx';
import DashboardMerged from '../pages/Home/DashboardMerged.jsx';

// Resolve a `var(--token)` inline value against the root custom properties that
// themeManager.js sets, following aliases (the dashboard token falls back to the
// app token when no override is stored).
function resolveColor(value) {
  const root = document.documentElement;
  let current = String(value).trim();
  for (let hop = 0; hop < 5; hop += 1) {
    const match = /^var\(\s*(--[\w-]+)\s*\)$/.exec(current);
    if (!match) return current;
    current = root.style.getPropertyValue(match[1]).trim();
    if (!current) return '';
  }
  return current;
}

function badgeForeground() {
  // The badge is the ancestor <span> of the "G" glyph inside the brand button.
  const glyph = screen.getByText('G');
  return resolveColor(glyph.parentElement.style.color);
}

function renderPageShell() {
  render(h(PageShell, { navOpen: false, onNavToggle: () => {} }, h('div', {}, 'page')));
}

function renderDashboard() {
  const api = { send: vi.fn(), request: vi.fn(async () => ({})), on: vi.fn(() => () => {}) };
  render(h(ApiServiceContext.Provider, { value: api }, h(DashboardMerged, {})));
}

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
  document.documentElement.removeAttribute('style');
  vi.restoreAllMocks();
});

describe.each([
  ['PageShell', renderPageShell],
  ['DashboardMerged', renderDashboard],
])('%s header accent badge foreground (PRO-643)', (_name, renderHeader) => {
  it('uses a dark foreground for a light dashboard accent', () => {
    applyThemePreferences({ theme: 'midnight', appAccent: null, dashboardAccent: '#ffffff' });
    renderHeader();
    expect(badgeForeground()).toBe('#000000');
  });

  it('uses a light foreground for a dark dashboard accent', () => {
    applyThemePreferences({ theme: 'midnight', appAccent: null, dashboardAccent: '#101010' });
    renderHeader();
    expect(badgeForeground()).toBe('#ffffff');
  });

  it('follows a light app accent that the dashboard inherits', () => {
    applyThemePreferences({ theme: 'midnight', appAccent: '#ffffff', dashboardAccent: null });
    renderHeader();
    expect(badgeForeground()).toBe('#000000');
  });

  it('never hardcodes white on the badge', () => {
    applyThemePreferences({ theme: 'midnight', appAccent: null, dashboardAccent: '#ffffff' });
    renderHeader();
    expect(screen.getByText('G').parentElement.style.color).toBe(
      'var(--gm-dashboard-accent-content)',
    );
  });
});
