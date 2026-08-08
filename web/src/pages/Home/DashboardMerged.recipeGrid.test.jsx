// PRO-640 regression: the recipe controls row must reflow to a 2-column grid at
// the existing mobile breakpoint (GRIND|DOSE, TEMP|YIELD, SCALES full-width)
// instead of collapsing to one control per row, while desktop keeps the single
// 5-control row with its decorative separators.
//
// The row/column geometry itself was verified in a real browser (headless Chrome
// layout of the same markup): at 375px GRIND/DOSE share row 1, TEMP/YIELD row 2,
// SCALES spans the full 375px on row 3; at 1024px all five sit on one row. jsdom
// does not implement CSS grid layout, so these tests assert the grid *contract*
// (the style values that produce that layout) plus the DOM/tab order and the
// TEMP lock, which jsdom can observe faithfully.

import { h } from 'preact';
import { afterEach, beforeEach, expect, test, vi } from 'vitest';
import { cleanup, render, screen } from '@testing-library/preact';

vi.mock('@fortawesome/react-fontawesome', () => ({
  FontAwesomeIcon: () => h('span', { 'data-testid': 'fa-icon' }),
}));

vi.mock('preact-fetching', () => ({
  useQuery: () => ({ isLoading: false, isError: false, data: {} }),
}));

vi.mock('../../services/localAuthFetch.js', () => ({
  authenticatedFetch: vi.fn(() => Promise.resolve({ json: async () => ({}) })),
  LOCAL_AUTH_TOKEN_KEY: 'gaggimate_local_admin_token',
}));

import { ApiServiceContext, machine } from '../../services/ApiService.js';
import DashboardMerged, { getRecipeGridStyle, getRecipeScalesCellStyle } from './DashboardMerged.jsx';
import { MODE_STANDBY } from './dashboardLogic.js';

const CONTROL_ORDER = ['GRIND', 'DOSE', 'TEMP', 'YIELD', 'SCALES'];

function stubViewport(isMobile) {
  // The dashboard seeds isMobile from window.innerWidth and then follows
  // matchMedia('(max-width: 639px)').
  vi.stubGlobal('innerWidth', isMobile ? 375 : 1024);
  vi.stubGlobal('matchMedia', () => ({
    matches: isMobile,
    addEventListener: () => {},
    removeEventListener: () => {},
  }));
}

beforeEach(() => {
  machine.value = {
    ...machine.value,
    connected: true,
    // Standby: TEMP is outside Brew mode, so it must render locked (not hidden).
    status: { ...machine.value.status, mode: MODE_STANDBY, process: null },
  };
});

afterEach(() => {
  cleanup();
  vi.restoreAllMocks();
});

function renderDashboard() {
  const api = { send: vi.fn(), request: vi.fn(async () => ({})), on: vi.fn(() => () => {}) };
  return render(h(ApiServiceContext.Provider, { value: api }, h(DashboardMerged, {})));
}

function recipeRow() {
  // The recipe row is the grid that owns the SCALES label.
  const scalesLabel = screen.getByText('SCALES');
  return scalesLabel.closest('div[style*="grid-template-columns"]')
    ?? scalesLabel.parentElement.parentElement;
}

test('renders the recipe controls as a two-column grid at a mobile viewport', () => {
  stubViewport(true);
  renderDashboard();

  const row = recipeRow();
  expect(row.style.gridTemplateColumns).toBe('1fr 1fr');
});

test('spans SCALES across both mobile columns so it has no empty neighbour cell', () => {
  stubViewport(true);
  renderDashboard();

  const scalesCell = screen.getByText('SCALES').parentElement;
  expect(scalesCell.style.gridColumn).toBe('1 / -1');
});

test('keeps the decorative separators out of grid flow on mobile', () => {
  stubViewport(true);
  renderDashboard();

  const separators = Array.from(recipeRow().children).filter(
    child => child.getAttribute('aria-hidden') === 'true'
  );
  expect(separators).toHaveLength(4);
  // display:none elements generate no grid item, so they cannot leave blank cells.
  separators.forEach(sep => expect(sep.style.display).toBe('none'));
});

test('keeps TEMP visible and locked on mobile outside Brew mode', () => {
  stubViewport(true);
  renderDashboard();

  // Present, and locked rather than hidden or blank.
  expect(screen.getByText(/^TEMP LOCKED/)).toBeTruthy();
  expect(screen.queryByRole('button', { name: 'Edit TEMP' })).toBeNull();
  expect(
    screen.getByTitle(
      'The machine owns this target: it can only be changed in Brew mode while no shot is running'
    ).getAttribute('aria-disabled')
  ).toBe('true');
});

test('preserves GRIND → DOSE → TEMP → YIELD → SCALES reading order on mobile', () => {
  stubViewport(true);
  renderDashboard();

  const text = recipeRow().textContent;
  const positions = CONTROL_ORDER.map(label => text.indexOf(label));
  positions.forEach(pos => expect(pos).toBeGreaterThanOrEqual(0));
  expect([...positions].sort((a, b) => a - b)).toEqual(positions);
});

test('keeps the one-row layout with visible separators at desktop widths', () => {
  stubViewport(false);
  renderDashboard();

  const row = recipeRow();
  expect(row.style.gridTemplateColumns).toBe('1fr auto 1fr auto 1fr auto 1fr auto 1fr');
  expect(screen.getByText('SCALES').parentElement.style.gridColumn).toBe('');

  const separators = Array.from(row.children).filter(
    child => child.getAttribute('aria-hidden') === 'true'
  );
  expect(separators).toHaveLength(4);
  separators.forEach(sep => expect(sep.style.display).toBe('inline'));
});

test('grid helpers describe exactly the layout verified in a real browser', () => {
  // 2 columns mobile + SCALES spanning them => 3 rows of 2/2/1 controls.
  expect(getRecipeGridStyle(true).gridTemplateColumns.split(' ')).toHaveLength(2);
  expect(getRecipeScalesCellStyle(true)).toEqual({ gridColumn: '1 / -1' });
  // 5 fields + 4 separators on one desktop row, SCALES unplaced.
  expect(getRecipeGridStyle(false).gridTemplateColumns.split(' ')).toHaveLength(9);
  expect(getRecipeScalesCellStyle(false)).toEqual({});
});
