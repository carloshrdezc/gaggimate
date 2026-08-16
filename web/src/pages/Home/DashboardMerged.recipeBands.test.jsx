// PRO-645 regression: every field in the Brew recipe row must reserve the SAME
// three-band footprint — label / value+control / metadata — so a value or arrow
// baseline can never move because a neighbour's label is longer or a field grew
// supplemental metadata.
//
// jsdom does not implement CSS grid or box layout, so these tests assert the
// structural *contract* that produces the alignment (band count per field, the
// reserved band heights, an identically sized control column on every field,
// and top alignment of the row) rather than a rendered visual snapshot.

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
import DashboardMerged, {
  getRecipeGridStyle,
  RECIPE_CONTROL_COLUMN_WIDTH,
  RECIPE_LABEL_BAND_HEIGHT,
  RECIPE_METADATA_BAND_HEIGHT,
  RECIPE_VALUE_BAND_HEIGHT,
} from './DashboardMerged.jsx';
import { MODE_STANDBY } from './dashboardLogic.js';

const FIELDS = ['GRIND', 'DOSE', 'TEMP', 'YIELD', 'SCALES'];
// GRIND/DOSE are always editable; TEMP is locked outside Brew mode and YIELD is
// locked without the override + volumetric profile + scale, and SCALES is a
// read-only readout — those three must still reserve the arrow column.
const EDITABLE_FIELDS = ['GRIND', 'DOSE'];

beforeEach(() => {
  vi.stubGlobal('innerWidth', 1024);
  vi.stubGlobal('matchMedia', () => ({
    matches: false,
    addEventListener: () => {},
    removeEventListener: () => {},
  }));

  machine.value = {
    ...machine.value,
    connected: true,
    // Standby exercises the worst case: TEMP renders its lock reason and YIELD
    // renders a three-clause one, which is what used to push their values down.
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
  return screen.getByText('SCALES').closest('div[style*="grid-template-columns"]');
}

function fieldCell(name) {
  const cell = recipeRow().querySelector(`[data-dm-field="${name}"]`);
  expect(cell, `recipe field ${name} is missing`).toBeTruthy();
  return cell;
}

function bands(name) {
  const cell = fieldCell(name);
  return {
    label: cell.querySelectorAll('[data-dm-band="label"]'),
    value: cell.querySelectorAll('[data-dm-band="value"]'),
    metadata: cell.querySelectorAll('[data-dm-band="metadata"]'),
    control: cell.querySelectorAll('[data-dm-control]'),
  };
}

test('builds every recipe field from exactly one label, value and metadata band', () => {
  renderDashboard();

  FIELDS.forEach(name => {
    const found = bands(name);
    expect(found.label, `${name} label band`).toHaveLength(1);
    expect(found.value, `${name} value band`).toHaveLength(1);
    expect(found.metadata, `${name} metadata band`).toHaveLength(1);
  });
});

test('reserves an identical label, value and metadata band height on every recipe field', () => {
  renderDashboard();

  FIELDS.forEach(name => {
    const found = bands(name);
    expect(found.label[0].style.minHeight, `${name} label band height`).toBe(
      `${RECIPE_LABEL_BAND_HEIGHT}px`,
    );
    expect(found.value[0].style.height, `${name} value band height`).toBe(
      `${RECIPE_VALUE_BAND_HEIGHT}px`,
    );
    expect(found.metadata[0].style.minHeight, `${name} metadata band height`).toBe(
      `${RECIPE_METADATA_BAND_HEIGHT}px`,
    );
  });
});

test('reserves the same numeric-control footprint on editable and locked fields alike', () => {
  renderDashboard();

  FIELDS.forEach(name => {
    const found = bands(name);
    expect(found.control, `${name} control column`).toHaveLength(1);
    const control = found.control[0];
    expect(control.style.width, `${name} control width`).toBe(`${RECIPE_CONTROL_COLUMN_WIDTH}px`);
    expect(control.style.height, `${name} control height`).toBe(`${RECIPE_VALUE_BAND_HEIGHT}px`);
    expect(control.getAttribute('data-dm-control')).toBe(
      EDITABLE_FIELDS.includes(name) ? 'stepper' : 'spacer',
    );
  });
});

test('renders live steppers only inside the reserved control column of editable fields', () => {
  renderDashboard();

  EDITABLE_FIELDS.forEach(name => {
    const control = bands(name).control[0];
    expect(control.contains(screen.getByRole('button', { name: `Increase ${name}` }))).toBe(true);
    expect(control.contains(screen.getByRole('button', { name: `Decrease ${name}` }))).toBe(true);
  });

  ['TEMP', 'YIELD'].forEach(name => {
    const control = bands(name).control[0];
    expect(control.children).toHaveLength(0);
    expect(control.getAttribute('aria-hidden')).toBe('true');
    expect(screen.queryByRole('button', { name: `Increase ${name}` })).toBeNull();
  });
});

test('keeps the short field name in the label band and moves lock reasons to the metadata band', () => {
  renderDashboard();

  FIELDS.forEach(name => {
    expect(bands(name).label[0].textContent.trim(), `${name} label band text`).toBe(name);
  });

  // The reasons themselves are unchanged — they just live below the value now.
  expect(bands('TEMP').metadata[0].textContent).toMatch(/^TEMP LOCKED/);
  expect(bands('YIELD').metadata[0].textContent).toMatch(/YIELD LOCKED/);
});

test('gives the numeric glyph of every field an identical box model', () => {
  renderDashboard();

  // The editable trigger's dashed underline lives INSIDE its border box, so the
  // locked values and the SCALES readout reserve the same 1px — otherwise their
  // glyph sits a pixel lower inside the centred value band. Verified against
  // real headless-Chrome geometry (all five glyph tops equal).
  FIELDS.forEach(name => {
    const glyph = bands(name).value[0].querySelector('button, span');
    expect(glyph.style.fontSize, `${name} glyph size`).toBe('28px');
    expect(glyph.style.lineHeight, `${name} glyph line height`).toBe('1');
    expect(glyph.style.borderBottomWidth, `${name} glyph underline reserve`).toBe('1px');
  });
});

test('top-aligns the recipe row so a wrapping metadata band cannot re-centre a cell', () => {
  expect(getRecipeGridStyle(false).alignItems).toBe('start');
  expect(getRecipeGridStyle(true).alignItems).toBe('start');

  renderDashboard();
  expect(recipeRow().style.alignItems).toBe('start');
});
