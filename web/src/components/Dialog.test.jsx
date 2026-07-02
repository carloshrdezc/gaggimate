// PRO-180 — Dialog accessibility behaviour.
//
// The shared Dialog (web/src/components/Dialog.jsx) is the single accessible
// dialog primitive on this branch (VisualizerUploadModal is its only consumer).
// This suite locks in the focus-management contract the reviewer flagged as
// untested: Escape closes, Tab / Shift+Tab wrap focus at the focusable
// boundaries (focus trap), body scroll is restored on unmount, and focus
// returns to the previously-focused element when the dialog goes away.
//
// Matches the existing rendered-component test harness (vitest + jsdom +
// @testing-library/preact) used by Navigation.test.jsx / RatingNumberInput.test.jsx.

import { afterEach, describe, expect, test, vi } from 'vitest';
import { h } from 'preact';
import { render, screen, cleanup, fireEvent } from '@testing-library/preact';

import Dialog from './Dialog.jsx';

afterEach(() => {
  cleanup();
  // Defensive: ensure no leaked body-scroll lock between tests.
  document.body.style.overflow = '';
});

// jsdom has no layout, so Dialog's isVisible() check (offsetWidth/offsetHeight/
// getClientRects) would treat every element as invisible and the focus trap
// would find zero focusable elements. Force a non-empty client rect so the
// focusable-element query behaves like a real browser.
const withClientRects = () => {
  const spy = vi
    .spyOn(HTMLElement.prototype, 'getClientRects')
    .mockReturnValue([{ width: 10, height: 10 }]);
  return () => spy.mockRestore();
};

const renderDialog = (props = {}, body) =>
  render(
    h(
      Dialog,
      { open: true, onClose: vi.fn(), title: 'Test dialog', ...props },
      body ??
        h('div', null, [
          h('button', { key: 'first', type: 'button' }, 'First'),
          h('button', { key: 'second', type: 'button' }, 'Second'),
          h('button', { key: 'third', type: 'button' }, 'Third'),
        ]),
    ),
  );

describe('Dialog (PRO-180)', () => {
  test('pressing Escape calls onClose', () => {
    const onClose = vi.fn();
    renderDialog({ onClose });

    fireEvent.keyDown(screen.getByRole('dialog'), { key: 'Escape' });

    expect(onClose).toHaveBeenCalledTimes(1);
  });

  test('Tab from the last focusable element wraps to the first (focus trap)', () => {
    const restore = withClientRects();
    try {
      renderDialog();
      const buttons = screen.getAllByRole('button');
      const first = buttons[0];
      const last = buttons[buttons.length - 1];

      last.focus();
      expect(document.activeElement).toBe(last);

      // Forward Tab at the boundary must wrap back to the first element.
      fireEvent.keyDown(screen.getByRole('dialog'), { key: 'Tab' });
      expect(document.activeElement).toBe(first);
    } finally {
      restore();
    }
  });

  test('Shift+Tab from the first focusable element wraps to the last (focus trap)', () => {
    const restore = withClientRects();
    try {
      renderDialog();
      const buttons = screen.getAllByRole('button');
      const first = buttons[0];
      const last = buttons[buttons.length - 1];

      first.focus();
      expect(document.activeElement).toBe(first);

      // Backward Tab at the boundary must wrap forward to the last element.
      fireEvent.keyDown(screen.getByRole('dialog'), { key: 'Tab', shiftKey: true });
      expect(document.activeElement).toBe(last);
    } finally {
      restore();
    }
  });

  test('document.body.style.overflow is restored after the dialog unmounts', () => {
    document.body.style.overflow = 'scroll';

    const { unmount } = renderDialog();
    // While open the body scroll is locked.
    expect(document.body.style.overflow).toBe('hidden');

    unmount();
    // After unmount the prior overflow value is restored.
    expect(document.body.style.overflow).toBe('scroll');
  });

  test('focus returns to the previously-focused element when the dialog unmounts', () => {
    // An element that holds focus before the dialog opens.
    const trigger = document.createElement('button');
    trigger.textContent = 'Open dialog';
    document.body.appendChild(trigger);
    trigger.focus();
    expect(document.activeElement).toBe(trigger);

    const { unmount } = renderDialog();

    unmount();
    expect(document.activeElement).toBe(trigger);

    document.body.removeChild(trigger);
  });
});
