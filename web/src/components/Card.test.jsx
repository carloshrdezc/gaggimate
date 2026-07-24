import { afterEach, describe, expect, it, vi } from 'vitest';
import { h } from 'preact';
import { cleanup, fireEvent, render, screen } from '@testing-library/preact';

// ResizeHandle imports Home page code that pulls signals/services we don't need
// here; stub it so Card can be tested in isolation.
vi.mock('../pages/Home/ResizeHandle.jsx', () => ({ default: () => null }));
vi.mock('@fortawesome/react-fontawesome', () => ({ FontAwesomeIcon: () => null }));

import Card from './Card.jsx';

afterEach(() => {
  cleanup();
  vi.restoreAllMocks();
});

describe('Card', () => {
  it('renders unchanged (no collapse chrome, body always shown) when collapsible is omitted', () => {
    render(h(Card, { title: 'Plain Card' }, h('p', { 'data-testid': 'body' }, 'content')));

    // Title renders as a plain heading, not a toggle button.
    expect(screen.getByRole('heading', { name: 'Plain Card' })).toBeTruthy();
    expect(screen.queryByRole('button')).toBeNull();
    // Body is present.
    expect(screen.getByTestId('body')).toBeTruthy();
  });

  it('uncontrolled mode toggles open state on header click (default collapsed)', () => {
    render(
      h(Card, { title: 'Collapsible', collapsible: true }, h('p', { 'data-testid': 'body' }, 'x')),
    );

    const header = screen.getByRole('button', { name: 'Collapsible' });
    // Default collapsed: body unmounted, aria-expanded false.
    expect(header.getAttribute('aria-expanded')).toBe('false');
    expect(screen.queryByTestId('body')).toBeNull();

    // Click opens.
    fireEvent.click(header);
    expect(header.getAttribute('aria-expanded')).toBe('true');
    expect(screen.getByTestId('body')).toBeTruthy();

    // Click closes again.
    fireEvent.click(header);
    expect(header.getAttribute('aria-expanded')).toBe('false');
    expect(screen.queryByTestId('body')).toBeNull();
  });

  it('respects defaultOpen in uncontrolled mode', () => {
    render(
      h(
        Card,
        { title: 'Open By Default', collapsible: true, defaultOpen: true },
        h('p', { 'data-testid': 'body' }, 'x'),
      ),
    );
    expect(screen.getByTestId('body')).toBeTruthy();
    expect(
      screen.getByRole('button', { name: 'Open By Default' }).getAttribute('aria-expanded'),
    ).toBe('true');
  });

  it('controlled mode reflects the open prop and delegates toggling via onToggle', () => {
    const onToggle = vi.fn();
    const { rerender } = render(
      h(
        Card,
        { title: 'Controlled', collapsible: true, open: false, onToggle },
        h('p', { 'data-testid': 'body' }, 'x'),
      ),
    );

    const header = screen.getByRole('button', { name: 'Controlled' });
    expect(header.getAttribute('aria-expanded')).toBe('false');
    expect(screen.queryByTestId('body')).toBeNull();

    // Clicking calls onToggle but does NOT change its own state (parent owns it).
    fireEvent.click(header);
    expect(onToggle).toHaveBeenCalledTimes(1);
    expect(screen.getByRole('button', { name: 'Controlled' }).getAttribute('aria-expanded')).toBe(
      'false',
    );
    expect(screen.queryByTestId('body')).toBeNull();

    // Parent flips the prop -> body appears.
    rerender(
      h(
        Card,
        { title: 'Controlled', collapsible: true, open: true, onToggle },
        h('p', { 'data-testid': 'body' }, 'x'),
      ),
    );
    expect(screen.getByTestId('body')).toBeTruthy();
    expect(screen.getByRole('button', { name: 'Controlled' }).getAttribute('aria-expanded')).toBe(
      'true',
    );
  });
});
