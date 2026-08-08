// PRO-643 regression: with user-customizable accents, every foreground painted
// ON an opaque accent fill must come from the derived contrast token, never a
// hardcoded white. This suite covers the three remaining offenders found in
// review:
//
//   1. the Dashboard primary action button (shared by the main Dashboard and
//      ManualConsole via `getPrimaryActionButtonStyle`), whose fill is the
//      dashboard accent,
//   2. the `--color-primary`-filled `nd-*` controls in `style.css`, and
//   3. the BoundaryChart marker remove button.
//
// jsdom neither loads `style.css` nor resolves `var()` in getComputedStyle, so
// the token chain is resolved here against BOTH sources it really comes from:
// the custom properties declared in `style.css` and the inline properties the
// real (unmocked) themeManager writes onto <html>. That asserts the end-to-end
// resolved color — a readable foreground — rather than the mere presence of a
// variable name.

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { h } from 'preact';
import { cleanup, render, screen } from '@testing-library/preact';

vi.mock('@fortawesome/react-fontawesome', () => ({
  FontAwesomeIcon: () => h('span', { 'data-testid': 'fa-icon' }),
}));

// DashboardMerged's useGrindSettings fetch — irrelevant to button colors.
vi.mock('preact-fetching', () => ({
  useQuery: () => ({ isLoading: false, isError: false, data: {} }),
}));

vi.mock('./services/localAuthFetch.js', () => ({
  authenticatedFetch: vi.fn(() => Promise.resolve({ json: async () => ({}) })),
  LOCAL_AUTH_TOKEN_KEY: 'gaggimate_local_admin_token',
}));

// BoundaryChart only needs its chart to occupy space; chart.js in jsdom does not.
vi.mock('./pages/ShotHistory/HistoryChart.jsx', () => ({
  HistoryChart: () => h('div', { 'data-testid': 'history-chart' }),
}));

import { ApiServiceContext, machine } from './services/ApiService.js';
import { applyThemePreferences } from './utils/themeManager.js';
import DashboardMerged, { ManualConsole } from './pages/Home/DashboardMerged.jsx';
import {
  MODE_MANUAL,
  getManualControlLabels,
  getPrimaryActionState,
} from './pages/Home/dashboardLogic.js';
import { BoundaryChart } from './pages/ShotToProfile/BoundaryChart.jsx';

// Comments are stripped so declaration scanning never has to reason about the
// prose inside them.
const STYLE_CSS = fs
  .readFileSync(path.join(path.dirname(fileURLToPath(import.meta.url)), 'style.css'), 'utf8')
  .replace(/\/\*[\s\S]*?\*\//g, '');

// Every `--custom-prop: value` declaration in style.css. Later declarations win,
// which mirrors the cascade for the equal-specificity blocks this file uses for
// the accent tokens.
const CSS_CUSTOM_PROPERTIES = (() => {
  const declarations = new Map();
  const pattern = /(?:^|[;{])[ \t]*(--[\w-]+)\s*:\s*([^;}]+)/gm;
  let match = pattern.exec(STYLE_CSS);
  while (match) {
    declarations.set(match[1], match[2].trim());
    match = pattern.exec(STYLE_CSS);
  }
  return declarations;
})();

// Resolve one `var(--token[, fallback])` hop: an inline property on <html>
// (themeManager) outranks anything declared in style.css.
function lookupToken(name) {
  const inline = document.documentElement.style.getPropertyValue(name).trim();
  return inline || CSS_CUSTOM_PROPERTIES.get(name) || '';
}

function resolveColor(value) {
  let current = String(value).trim();
  for (let hop = 0; hop < 6; hop += 1) {
    const match = /^var\(\s*(--[\w-]+)\s*(?:,\s*([^)]*))?\)$/.exec(current);
    if (!match) return current;
    const resolved = lookupToken(match[1]);
    current = resolved || (match[2] ?? '').trim();
    if (!current) return '';
  }
  return current;
}

// Like resolveColor, but expands EVERY `var()` in place instead of only a
// whole-value one. Needed for the derived hover fill, whose final value is a
// `color-mix()` whose arguments are themselves accent tokens: expanding them
// proves the hover color is computed from the accent in force rather than
// being a hardcoded literal.
function expandTokens(value) {
  let current = String(value).trim();
  for (let pass = 0; pass < 8; pass += 1) {
    const next = current.replace(
      /var\(\s*(--[\w-]+)\s*(?:,\s*([^()]*))?\)/g,
      (whole, name, fallback) => lookupToken(name) || (fallback ?? '').trim(),
    );
    if (next === current) return current;
    current = next;
  }
  return current;
}

// One declaration of a top-level rule in style.css, so the assertions read the
// real stylesheet instead of a copy of it.
function ruleDeclaration(selector, property) {
  const escaped = selector.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
  const block = new RegExp(`(?:^|})\\s*${escaped}\\s*\\{([^}]*)\\}`, 'm').exec(STYLE_CSS);
  expect(block, `rule ${selector} not found in style.css`).not.toBeNull();
  const declaration = new RegExp(`(?:^|;)\\s*${property}\\s*:\\s*([^;]+)`).exec(block[1].trim());
  expect(declaration, `rule ${selector} has no ${property} declaration`).not.toBeNull();
  return declaration[1].trim();
}

function ruleColor(selector) {
  return ruleDeclaration(selector, 'color');
}

function renderDashboard() {
  const api = { send: vi.fn(), request: vi.fn(async () => ({})), on: vi.fn(() => () => {}) };
  render(h(ApiServiceContext.Provider, { value: api }, h(DashboardMerged, {})));
}

function renderManualConsole() {
  const primary = getPrimaryActionState({ active: true, finished: false, mode: MODE_MANUAL });
  render(
    h(ManualConsole, {
      active: true,
      finished: false,
      draft: { targetType: 'pressure', pressure: 9, flow: 2, temperature: 93 },
      selectedBean: 'Test bean',
      dose: 18,
      currentWeight: 0,
      bluetoothConnected: false,
      scaleName: '',
      pressure: 9,
      flow: 2,
      temperature: 93,
      controlLabels: getManualControlLabels('pressure'),
      isBeanDropdownOpen: false,
      beanOptions: [],
      loadingBeans: false,
      beanError: null,
      primaryAction: () => {},
      primaryActionAccent: primary.accent,
      primaryActionLabel: primary.label,
      onBeanClick: () => {},
      onBeanSelect: () => {},
      onBeanRetry: () => {},
      onBeanDropdownClose: () => {},
      onDoseCommit: () => {},
      onEditingChange: () => {},
      onManualUpdate: () => {},
    }),
  );
}

beforeEach(() => {
  // jsdom has neither; the dashboard's mobile-layout effect and BoundaryChart's
  // resize observer both need one.
  vi.stubGlobal('matchMedia', () => ({
    matches: false,
    addEventListener: () => {},
    removeEventListener: () => {},
  }));
  vi.stubGlobal(
    'ResizeObserver',
    class {
      constructor(callback) {
        this.callback = callback;
      }
      // Fire immediately so the component re-renders with its container ref set,
      // which is what gates the boundary markers.
      observe() {
        this.callback([]);
      }
      unobserve() {}
      disconnect() {}
    },
  );

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

describe('dashboard primary action button foreground (PRO-643)', () => {
  function activeProcess() {
    machine.value = {
      ...machine.value,
      status: { ...machine.value.status, mode: 1, process: { a: true, e: 4000 } },
    };
  }

  it('keeps the Dashboard STOP SHOT label readable on a light dashboard accent', () => {
    applyThemePreferences({ theme: 'midnight', appAccent: null, dashboardAccent: '#ffffff' });
    activeProcess();
    renderDashboard();

    const button = screen.getByRole('button', { name: 'STOP SHOT' });
    // Background is the raw accent, so the label must be its contrast token.
    expect(resolveColor(button.style.background)).toBe('#ffffff');
    expect(button.style.color).toBe('var(--gm-dashboard-accent-content)');
    expect(resolveColor(button.style.color)).toBe('#000000');
  });

  it('keeps the Dashboard STOP SHOT label readable on a dark dashboard accent', () => {
    applyThemePreferences({ theme: 'midnight', appAccent: null, dashboardAccent: '#101010' });
    activeProcess();
    renderDashboard();

    const button = screen.getByRole('button', { name: 'STOP SHOT' });
    expect(resolveColor(button.style.color)).toBe('#ffffff');
  });

  it('follows a light app accent the dashboard inherits', () => {
    applyThemePreferences({ theme: 'midnight', appAccent: '#ffffff', dashboardAccent: null });
    activeProcess();
    renderDashboard();

    expect(resolveColor(screen.getByRole('button', { name: 'STOP SHOT' }).style.color)).toBe(
      '#000000',
    );
  });

  it('keeps the ManualConsole STOP MANUAL label readable on a light dashboard accent', () => {
    applyThemePreferences({ theme: 'midnight', appAccent: null, dashboardAccent: '#ffffff' });
    renderManualConsole();

    const button = screen.getByRole('button', { name: 'STOP MANUAL' });
    expect(resolveColor(button.style.background)).toBe('#ffffff');
    expect(resolveColor(button.style.color)).toBe('#000000');
  });

  // WAKE is an OUTLINED button: its fill is a 14% accent tint over the dark
  // shell, so accent-colored text is the readable choice there. Asserting the
  // fill stays a tint is what keeps this exempt from the contrast-token rule.
  it('renders WAKE as accent-on-tint rather than a hardcoded white label', () => {
    applyThemePreferences({ theme: 'midnight', appAccent: null, dashboardAccent: '#ffffff' });
    machine.value = {
      ...machine.value,
      status: { ...machine.value.status, mode: 0, process: null },
    };
    renderDashboard();

    const button = screen.getByRole('button', { name: 'WAKE' });
    expect(button.style.background).toContain('color-mix');
    expect(button.style.color).toBe('var(--dm-accent)');
    expect(resolveColor(button.style.color)).toBe('#ffffff');
  });
});

describe('app-accent filled controls foreground (PRO-643)', () => {
  it.each(['.nd-segmented-btn--active', '.nd-action-btn--primary', '.nd-day-btn--active'])(
    '%s resolves to a readable foreground for a light app accent',
    selector => {
      applyThemePreferences({ theme: 'midnight', appAccent: '#ffffff', dashboardAccent: null });
      expect(resolveColor(ruleColor(selector))).toBe('#000000');
    },
  );

  it.each(['.nd-segmented-btn--active', '.nd-action-btn--primary', '.nd-day-btn--active'])(
    '%s resolves to a readable foreground for a dark app accent',
    selector => {
      applyThemePreferences({ theme: 'midnight', appAccent: '#101010', dashboardAccent: null });
      expect(resolveColor(ruleColor(selector))).toBe('#ffffff');
    },
  );

  it('leaves the named-theme defaults resolvable with no custom accent', () => {
    applyThemePreferences({ theme: 'matcha', appAccent: null, dashboardAccent: null });
    // matcha's #5aaa52 default derives a dark foreground.
    expect(resolveColor(ruleColor('.nd-action-btn--primary'))).toBe('#000000');
  });
});

// A control whose rest fill is the accent but whose :hover fill was a hardcoded
// brand red flipped back to red on hover, breaking both the accent and the
// contrast token chosen for it. The hover fill must be the derived hover token.
describe('app-accent filled controls hover fill (PRO-643)', () => {
  // [selector, properties that paint the accent on hover]
  const HOVER_RULES = [
    ['.nd-segmented-btn--active:hover', ['background']],
    ['.nd-action-btn--primary:hover:not(:disabled)', ['background', 'border-color']],
    ['.nd-day-btn--active:hover', ['background', 'border-color']],
  ];

  const cases = HOVER_RULES.flatMap(([selector, properties]) =>
    properties.map(property => ({ selector, property })),
  );

  it.each(cases)(
    '$selector $property follows the app accent in force',
    ({ selector, property }) => {
      applyThemePreferences({ theme: 'midnight', appAccent: '#ffffff', dashboardAccent: null });
      const light = expandTokens(ruleDeclaration(selector, property));
      applyThemePreferences({ theme: 'midnight', appAccent: '#101010', dashboardAccent: null });
      const dark = expandTokens(ruleDeclaration(selector, property));

      // The old hardcoded brand red is gone from both, and the resolved value
      // tracks the accent rather than being any fixed literal.
      expect(light).not.toBe('#c4161e');
      expect(dark).not.toBe('#c4161e');
      expect(light).not.toBe(dark);

      // themeManager derives the hover fill as an 86% accent / 14% contrast-token
      // mix, so both accent and its contrast color must appear fully resolved —
      // no `var()` left over, which is what proves the whole chain resolves.
      expect(light).toBe('color-mix(in srgb, #ffffff 86%, #000000)');
      expect(dark).toBe('color-mix(in srgb, #101010 86%, #ffffff)');
      expect(light).not.toContain('var(');
      expect(dark).not.toContain('var(');
    },
  );

  it('falls back to the theme default accent when no custom accent is set', () => {
    applyThemePreferences({ theme: 'midnight', appAccent: null, dashboardAccent: null });
    // :root's #d71921 pre-JS default, mixed toward its #ffffff contrast token —
    // still the accent chain, not the old #c4161e literal.
    expect(
      expandTokens(ruleDeclaration('.nd-action-btn--primary:hover:not(:disabled)', 'background')),
    ).toBe('color-mix(in srgb, #d71921 86%, #ffffff)');
  });

  // The danger button is intentionally exempt: its fill is the fixed brand red
  // in every theme, so it must NOT be routed through the accent hover token.
  it('leaves the danger button hover fill as the fixed brand red', () => {
    applyThemePreferences({ theme: 'midnight', appAccent: '#ffffff', dashboardAccent: null });
    expect(
      expandTokens(ruleDeclaration('.nd-action-btn--danger:hover:not(:disabled)', 'background')),
    ).toBe('#c4161e');
  });
});

describe('BoundaryChart marker remove button foreground (PRO-643)', () => {
  const shot = { samples: Array.from({ length: 20 }, (_, i) => ({ t: i * 100 })) };

  function renderBoundaryChart() {
    render(h(BoundaryChart, { shot, boundaries: [8], onBoundariesChange: () => {} }));
  }

  it('uses the app-accent contrast token for a light app accent', () => {
    applyThemePreferences({ theme: 'midnight', appAccent: '#ffffff', dashboardAccent: null });
    renderBoundaryChart();

    const button = screen.getByRole('button', { name: 'Remove boundary 1' });
    expect(resolveColor(button.style.background)).toBe('#ffffff');
    expect(button.style.color).not.toBe('#fff');
    expect(resolveColor(button.style.color)).toBe('#000000');
  });

  it('uses the app-accent contrast token for a dark app accent', () => {
    applyThemePreferences({ theme: 'midnight', appAccent: '#101010', dashboardAccent: null });
    renderBoundaryChart();

    expect(
      resolveColor(screen.getByRole('button', { name: 'Remove boundary 1' }).style.color),
    ).toBe('#ffffff');
  });
});
