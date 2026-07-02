// PRO-398 — shared ImportButton uses the button + hidden-file-input-via-ref
// idiom, NOT a <label htmlFor> / <input id> pairing. The id-coupling idiom is
// the bug class PRO-395 and PRO-399 had to repair (a stale/mismatched id or
// htmlFor silently detaches the label from its input). These tests pin the
// robust behavior: clicking the button opens the hidden input, and the change
// handler + multi-file support are preserved.

import { afterEach, describe, expect, test, vi } from 'vitest';
import { h } from 'preact';
import { render, screen, cleanup, fireEvent } from '@testing-library/preact';

// Stub the FontAwesome icon component — it pulls the fontawesome-svg-core graph
// which trips jsdom's attribute handling and is irrelevant to the trigger/ref
// behavior these tests pin. Same pattern as Navigation.test.jsx.
vi.mock('@fortawesome/react-fontawesome', () => ({
  FontAwesomeIcon: () => h('span', { 'data-testid': 'fa-icon' }),
}));

import { ImportButton } from './ImportButton.jsx';

afterEach(() => {
  cleanup();
});

describe('ImportButton (PRO-398)', () => {
  test('renders an accessible trigger and a hidden file input (no id coupling)', () => {
    const { container } = render(h(ImportButton, { onChange: vi.fn(), title: 'Import Profiles' }));

    const button = screen.getByRole('button', { name: 'Import Profiles' });
    expect(button).toBeTruthy();

    const input = container.querySelector('input[type="file"]');
    expect(input).toBeTruthy();
    // The hidden input must NOT rely on id/htmlFor wiring.
    expect(input.getAttribute('id')).toBeNull();
    expect(container.querySelector('label')).toBeNull();
  });

  test('clicking the button triggers the hidden file input', () => {
    const { container } = render(h(ImportButton, { onChange: vi.fn() }));
    const input = container.querySelector('input[type="file"]');
    const clickSpy = vi.spyOn(input, 'click');

    fireEvent.click(screen.getByRole('button'));

    expect(clickSpy).toHaveBeenCalledTimes(1);
  });

  test('forwards change events to onChange', () => {
    const onChange = vi.fn();
    const { container } = render(h(ImportButton, { onChange }));
    const input = container.querySelector('input[type="file"]');

    fireEvent.change(input);

    expect(onChange).toHaveBeenCalledTimes(1);
  });

  test('honors multiple and accept props', () => {
    const { container } = render(
      h(ImportButton, { onChange: vi.fn(), multiple: true, accept: '.json,application/json' }),
    );
    const input = container.querySelector('input[type="file"]');

    expect(input.multiple).toBe(true);
    expect(input.getAttribute('accept')).toBe('.json,application/json');
  });

  test('disabled prop disables the trigger button', () => {
    render(h(ImportButton, { onChange: vi.fn(), disabled: true }));
    expect(screen.getByRole('button').disabled).toBe(true);
  });
});
