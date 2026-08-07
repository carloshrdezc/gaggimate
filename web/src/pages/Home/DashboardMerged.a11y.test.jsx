import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { h } from 'preact';
import { afterEach, expect, test, vi } from 'vitest';
import { cleanup, fireEvent, render, screen } from '@testing-library/preact';

import { EditableNumBlock, ModeRail, getRecipeGridStyle, graphLegendRowStyle } from './DashboardMerged.jsx';

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
  expect(getRecipeGridStyle(false).gridTemplateColumns).toBe('1fr auto 1fr auto 1fr auto 1fr');
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
});
