import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { h } from 'preact';
import { afterEach, expect, test, vi } from 'vitest';
import { cleanup, fireEvent, render, screen } from '@testing-library/preact';

import { EditableNumBlock, ModeRail, getRecipeGridStyle, graphLegendRowStyle } from './DashboardMerged.jsx';
import {
  BREW_TEMPERATURE_PLACEHOLDER,
  BREW_TEMPERATURE_UI_MAX,
  BREW_TEMPERATURE_UI_MIN,
} from './dashboardLogic.js';

afterEach(cleanup);

test('exposes the active dashboard mode as a pressed control', () => {
  const onSelect = vi.fn();

  render(h(ModeRail, {
    active: 1,
    modes: [
      { id: 0, name: 'Brew' },
      { id: 1, name: 'Steam' },
    ],
    onSelect,
  }));

  expect(screen.getByRole('button', { name: 'Brew' }).getAttribute('aria-pressed')).toBe('false');
  const steam = screen.getByRole('button', { name: 'Steam' });
  expect(steam.getAttribute('aria-pressed')).toBe('true');

  fireEvent.click(steam);
  expect(onSelect).toHaveBeenCalledWith(1);
});

test('uses a stacked recipe layout at mobile widths while preserving control order', () => {
  expect(getRecipeGridStyle(true).gridTemplateColumns).toBe('1fr');
  // PRO-630 added TEMP between DOSE and YIELD: 5 fields + 4 separators.
  expect(getRecipeGridStyle(false).gridTemplateColumns).toBe(
    '1fr auto 1fr auto 1fr auto 1fr auto 1fr',
  );
  const columns = getRecipeGridStyle(false).gridTemplateColumns.split(' ');
  expect(columns.filter(c => c === '1fr')).toHaveLength(5);
  expect(columns.filter(c => c === 'auto')).toHaveLength(4);
});

test('allows the live extraction legend to wrap instead of clipping at narrow widths', () => {
  expect(graphLegendRowStyle.flexWrap).toBe('wrap');
});

test('gives dashboard controls a visible focus indicator', () => {
  const stylesheet = readFileSync(resolve(process.cwd(), 'src/style.css'), 'utf8');
  expect(stylesheet).toMatch(/\.dm-shell :is\(button, input, select, a\):focus-visible/);
});

test('gives numeric edit buttons an accessible name that identifies their field', () => {
  render(h(EditableNumBlock, {
    label: 'GRIND',
    value: 18,
    unit: 'g',
    step: 0.1,
    min: 0,
    max: 30,
    onCommit: vi.fn(),
  }));

  const editButton = screen.getByRole('button', { name: 'Edit GRIND' });
  editButton.focus();
  expect(document.activeElement).toBe(editButton);

  fireEvent.click(editButton);
  const input = screen.getByRole('textbox', { name: 'Edit GRIND' });
  input.focus();
  expect(document.activeElement).toBe(input);
});

test('does not suppress the focus-visible outline while editing a numeric value', () => {
  render(h(EditableNumBlock, {
    label: 'GRIND',
    value: 18,
    unit: 'g',
    step: 0.1,
    min: 0,
    max: 30,
    onCommit: vi.fn(),
  }));

  fireEvent.click(screen.getByRole('button', { name: 'Edit GRIND' }));
  const input = screen.getByRole('textbox', { name: 'Edit GRIND' });
  input.focus();

  expect(input.style.outline).not.toBe('none');
});

test('returns focus to the numeric edit trigger after confirming with Enter', () => {
  render(h(EditableNumBlock, {
    label: 'GRIND',
    value: 18,
    unit: 'g',
    step: 0.1,
    min: 0,
    max: 30,
    onCommit: vi.fn(),
  }));

  const editButton = screen.getByRole('button', { name: 'Edit GRIND' });
  fireEvent.click(editButton);
  const input = screen.getByRole('textbox', { name: 'Edit GRIND' });
  fireEvent.keyDown(input, { key: 'Enter' });

  expect(document.activeElement).toBe(screen.getByRole('button', { name: 'Edit GRIND' }));
});

test('returns focus to the numeric edit trigger after cancelling with Escape', () => {
  render(h(EditableNumBlock, {
    label: 'GRIND',
    value: 18,
    unit: 'g',
    step: 0.1,
    min: 0,
    max: 30,
    onCommit: vi.fn(),
  }));

  const editButton = screen.getByRole('button', { name: 'Edit GRIND' });
  fireEvent.click(editButton);
  const input = screen.getByRole('textbox', { name: 'Edit GRIND' });
  fireEvent.keyDown(input, { key: 'Escape' });

  expect(document.activeElement).toBe(screen.getByRole('button', { name: 'Edit GRIND' }));
});

// ── PRO-630: the TEMP recipe field ──────────────────────────────────────────

test('names the brew temperature field so its edit control is identifiable', () => {
  const onCommit = vi.fn();
  render(h(EditableNumBlock, {
    label: 'TEMP',
    value: 93,
    unit: '°C',
    hint: 'PROFILE TARGET · DEFAULT',
    step: 0.5,
    min: BREW_TEMPERATURE_UI_MIN,
    max: BREW_TEMPERATURE_UI_MAX,
    onCommit,
  }));

  fireEvent.click(screen.getByRole('button', { name: 'Edit TEMP' }));
  const input = screen.getByRole('textbox', { name: 'Edit TEMP' });
  fireEvent.keyDown(input, { key: 'Enter', target: { value: '95' } });
  expect(onCommit).toHaveBeenCalledWith(95);
});

test('exposes the locked brew temperature state and its reason without an edit control', () => {
  render(h(EditableNumBlock, {
    label: 'TEMP',
    value: 93,
    unit: '°C',
    step: 0.5,
    min: BREW_TEMPERATURE_UI_MIN,
    max: BREW_TEMPERATURE_UI_MAX,
    onCommit: vi.fn(),
    disabled: true,
    lockedHint: 'TEMP LOCKED · BREW ACTIVE',
    disabledTitle: 'The machine owns this target',
  }));

  // No edit affordance at all while locked.
  expect(screen.queryByRole('button', { name: 'Edit TEMP' })).toBeNull();
  // The reason replaces the label, and the value is marked non-interactive.
  expect(screen.getByText('TEMP LOCKED · BREW ACTIVE')).toBeTruthy();
  const value = screen.getByTitle('The machine owns this target');
  expect(value.getAttribute('aria-disabled')).toBe('true');
});

test('renders the brew temperature placeholder rather than a fabricated default on older firmware', () => {
  render(h(EditableNumBlock, {
    label: 'TEMP',
    // resolveBrewTemperatureValue -> null, so the dashboard passes the placeholder.
    value: BREW_TEMPERATURE_PLACEHOLDER,
    unit: '°C',
    hint: 'PROFILE TARGET UNAVAILABLE',
    step: 0.5,
    min: BREW_TEMPERATURE_UI_MIN,
    max: BREW_TEMPERATURE_UI_MAX,
    onCommit: vi.fn(),
    disabled: true,
    lockedHint: 'TEMP LOCKED · FIRMWARE TOO OLD',
  }));

  expect(screen.getByText(BREW_TEMPERATURE_PLACEHOLDER)).toBeTruthy();
  expect(screen.queryByText('93.0')).toBeNull();
});
