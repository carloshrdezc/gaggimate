// PRO-299 — integration test: decimal rating entry through a PARENT surface.
//
// The unit test in components/RatingNumberInput.test.jsx covers the shared
// input in isolation. This test exercises the full edit -> type -> blur ->
// commit path through NotesBarExpanded (the ShotAnalyzer surface whose old
// <input type=number> + per-keystroke normalize was the stated root cause),
// driving onInputChange through real parent state. It asserts that typing
// "7.5" survives the decimal: the committed/displayed rating is 7.5, not 75
// (mid-entry dot strip) — the regression PRO-299 fixes.

import { afterEach, describe, expect, test, vi } from 'vitest';
import { h } from 'preact';
import { useState } from 'preact/hooks';
import { render, cleanup, fireEvent } from '@testing-library/preact';

// Stub FontAwesome: NotesBarExpanded pulls @fortawesome/react-fontawesome ->
// fontawesome-svg-core, whose package.json trips vitest's ESM JSON import-
// attribute handling (see Navigation.test.jsx for the same workaround). The
// icons are irrelevant to decimal rating entry.
vi.mock('@fortawesome/react-fontawesome', () => ({
  FontAwesomeIcon: () => h('span', { 'data-testid': 'fa-icon' }),
}));

import { NotesBarExpanded } from './NotesBarExpanded.jsx';

afterEach(() => {
  cleanup();
});

// Minimal parent harness mirroring NotesBar's handleInputChange: it stores the
// committed value as-is (PRO-299 removed the per-keystroke normalize here).
function Harness() {
  const [notes, setNotes] = useState({ rating: 0, notes: '', balanceTaste: '' });
  const handleInputChange = (field, value) => {
    setNotes(prev => ({ ...prev, [field]: value }));
  };
  return h(NotesBarExpanded, {
    currentShot: { id: 'shot-1' },
    notes,
    isEditing: true,
    saving: false,
    isExpanded: true,
    onInputChange: handleInputChange,
    onEdit: () => {},
    onSave: () => {},
    onCancel: () => {},
    onCollapse: () => {},
  });
}

describe('NotesBarExpanded decimal rating entry (PRO-299)', () => {
  test('typing "7.5" then blurring commits 7.5 — the decimal is not collapsed to 75', () => {
    render(h(Harness, {}));
    // The expanded edit panel renders several inputs; the rating field is the
    // RatingNumberInput, uniquely a text input with the `w-20` width class.
    const input = document.querySelector('input.w-20[inputmode="decimal"]');
    expect(input).not.toBeNull();

    // Keystroke-by-keystroke so the trailing-dot intermediate is exercised.
    fireEvent.input(input, { target: { value: '7' } });
    fireEvent.input(input, { target: { value: '7.' } });
    expect(input.value).toBe('7.'); // dot survives mid-entry
    fireEvent.input(input, { target: { value: '7.5' } });
    expect(input.value).toBe('7.5');

    // Commit on blur: the stored value flows back as a normalized 7.5,
    // NOT 75 (which is what stripping the "." mid-entry would have produced).
    fireEvent.blur(input);
    expect(input.value).toBe('7.5');
  });
});
