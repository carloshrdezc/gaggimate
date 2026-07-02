// PRO-299 — rating numeric inputs must allow smooth decimal entry.
//
// Before this change both Shot History (ShotNotesCard) and Shot Analyzer
// (NotesBarExpanded) normalized the rating on every keystroke, which stripped
// the decimal point before the tenths digit could be typed ("8" then "." ->
// "8." parsed to 8, dropping the "."). The shared RatingNumberInput holds the
// user's raw string while typing and only normalizes + commits on blur.
//
// It uses type=text + inputmode=decimal so the raw "8." intermediate survives
// in the DOM value (a type=number input collapses "8." to "" / "8" before any
// JS can see it, which is the root cause), hence getByRole('textbox') here.

import { afterEach, describe, expect, test, vi } from 'vitest';
import { h } from 'preact';
import { render, screen, cleanup, fireEvent } from '@testing-library/preact';

import { RatingNumberInput } from './RatingNumberInput.jsx';

afterEach(() => {
  cleanup();
});

describe('RatingNumberInput (PRO-299)', () => {
  test('keeps the raw decimal string while typing — does not strip the "."', () => {
    render(h(RatingNumberInput, { value: 0, onCommit: vi.fn() }));
    const input = screen.getByRole('textbox');

    // Keystroke-by-keystroke entry of "8.5". The trailing-dot intermediate
    // must survive — the pre-PRO-299 per-keystroke normalize collapsed "8." to
    // 8 and wrote it back into the controlled value, stranding the user.
    fireEvent.input(input, { target: { value: '8' } });
    expect(input.value).toBe('8');
    fireEvent.input(input, { target: { value: '8.' } });
    expect(input.value).toBe('8.');
    fireEvent.input(input, { target: { value: '8.5' } });
    expect(input.value).toBe('8.5');
  });

  test('commits the normalized numeric value on blur', () => {
    const onCommit = vi.fn();
    render(h(RatingNumberInput, { value: 0, onCommit }));
    const input = screen.getByRole('textbox');

    fireEvent.input(input, { target: { value: '8.5' } });
    fireEvent.blur(input);

    expect(onCommit).toHaveBeenCalledTimes(1);
    expect(onCommit).toHaveBeenCalledWith(8.5);
  });

  test('clamps and rounds via normalizeTenPointRating on blur', () => {
    const onCommit = vi.fn();
    render(h(RatingNumberInput, { value: 0, onCommit }));
    const input = screen.getByRole('textbox');

    fireEvent.input(input, { target: { value: '12.34' } });
    fireEvent.blur(input);

    // > 10 clamps to 10.
    expect(onCommit).toHaveBeenCalledWith(10);
  });

  test('does not commit when the field was never edited', () => {
    const onCommit = vi.fn();
    render(h(RatingNumberInput, { value: 7, onCommit }));
    const input = screen.getByRole('textbox');

    fireEvent.blur(input);
    expect(onCommit).not.toHaveBeenCalled();
  });

  test('mirrors the stored value when not actively editing', () => {
    const { rerender } = render(h(RatingNumberInput, { value: 7, onCommit: vi.fn() }));
    const input = screen.getByRole('textbox');
    expect(input.value).toBe('7');

    // After commit (draft cleared) a new stored value flows back in.
    rerender(h(RatingNumberInput, { value: 8.5, onCommit: vi.fn() }));
    expect(input.value).toBe('8.5');
  });
});

describe('RatingNumberInput id / aria-label passthrough (PRO-302)', () => {
  test('forwards id and aria-label to the underlying input when provided', () => {
    render(
      h(RatingNumberInput, {
        value: 0,
        onCommit: vi.fn(),
        id: 'shot-rating',
        ariaLabel: 'Shot rating (0-10)',
      }),
    );

    // The input is now reachable by its accessible name.
    const input = screen.getByRole('textbox', { name: 'Shot rating (0-10)' });
    expect(input.getAttribute('id')).toBe('shot-rating');
    expect(input.getAttribute('aria-label')).toBe('Shot rating (0-10)');
  });

  test('does not emit id / aria-label attributes when not provided', () => {
    render(h(RatingNumberInput, { value: 0, onCommit: vi.fn() }));
    const input = screen.getByRole('textbox');

    expect(input.hasAttribute('id')).toBe(false);
    expect(input.hasAttribute('aria-label')).toBe(false);
  });
});
