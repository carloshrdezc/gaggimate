// PRO-7 — connection-lost banner.
//
// The banner is driven entirely by the `connectionState` / `nextReconnectAt`
// signals exported from ApiService, plus the ApiService instance from context
// for the "Reconnect now" action. These tests pin: hidden when connected,
// visible + live countdown while reconnecting, red "failed" state, and that
// the button calls reconnectNow(). No live WebSocket is touched.

import { afterEach, beforeEach, describe, expect, test, vi } from 'vitest';
import { h } from 'preact';
import { render, screen, cleanup, fireEvent, act } from '@testing-library/preact';

// Stub FontAwesome (pulls the svg-core graph; irrelevant here) — same pattern
// as Navigation.test.jsx / ImportButton.test.jsx.
vi.mock('@fortawesome/react-fontawesome', () => ({
  FontAwesomeIcon: () => h('span', { 'data-testid': 'fa-icon' }),
}));

import { ConnectionBanner, secondsUntil } from './ConnectionBanner.jsx';
import { ApiServiceContext, connectionState, nextReconnectAt } from '../services/ApiService.js';

const renderBanner = (apiService = {}) =>
  render(h(ApiServiceContext.Provider, { value: apiService }, h(ConnectionBanner, {})));

beforeEach(() => {
  connectionState.value = 'connected';
  nextReconnectAt.value = null;
});

afterEach(() => {
  cleanup();
  vi.useRealTimers();
});

describe('secondsUntil', () => {
  test('rounds up whole seconds and clamps at zero', () => {
    expect(secondsUntil(10_000, 8_500)).toBe(2);
    expect(secondsUntil(10_000, 9_100)).toBe(1);
    expect(secondsUntil(10_000, 10_000)).toBe(0);
    expect(secondsUntil(10_000, 12_000)).toBe(0);
  });

  test('returns null for a non-finite target', () => {
    expect(secondsUntil(null)).toBeNull();
    expect(secondsUntil(undefined)).toBeNull();
  });
});

describe('ConnectionBanner (PRO-7)', () => {
  test('renders nothing while connected', () => {
    const { container } = renderBanner();
    expect(container.textContent).toBe('');
    expect(screen.queryByRole('alert')).toBeNull();
  });

  test('shows the banner with a live countdown while reconnecting', () => {
    vi.useFakeTimers();
    vi.setSystemTime(0);
    connectionState.value = 'reconnecting';
    nextReconnectAt.value = 3000; // 3s out

    renderBanner();

    expect(screen.getByRole('alert')).toBeTruthy();
    expect(screen.getByText('Connection lost')).toBeTruthy();
    expect(screen.getByText(/Retrying in 3s/)).toBeTruthy();

    // Advance the clock; the 1s interval re-renders and the countdown ticks.
    act(() => {
      vi.advanceTimersByTime(1000);
    });
    expect(screen.getByText(/Retrying in 2s/)).toBeTruthy();
  });

  test('auto-dismisses when the connection is restored', () => {
    connectionState.value = 'reconnecting';
    nextReconnectAt.value = Date.now() + 5000;
    const { rerender } = renderBanner();
    expect(screen.getByRole('alert')).toBeTruthy();

    // Signal flips back to connected -> banner disappears (no manual dismiss).
    connectionState.value = 'connected';
    nextReconnectAt.value = null;
    rerender(h(ApiServiceContext.Provider, { value: {} }, h(ConnectionBanner, {})));

    expect(screen.queryByRole('alert')).toBeNull();
  });

  test('"Reconnect now" calls reconnectNow() on the ApiService', () => {
    connectionState.value = 'reconnecting';
    nextReconnectAt.value = Date.now() + 5000;
    const reconnectNow = vi.fn();

    renderBanner({ reconnectNow });
    fireEvent.click(screen.getByRole('button', { name: /Reconnect now/i }));

    expect(reconnectNow).toHaveBeenCalledTimes(1);
  });

  test('failed state renders without a countdown', () => {
    connectionState.value = 'failed';
    nextReconnectAt.value = null;

    renderBanner();

    expect(screen.getByRole('alert')).toBeTruthy();
    expect(screen.getByText(/Automatic reconnection stopped/)).toBeTruthy();
  });

  // PRO-409 — quiet the per-second countdown for screen-reader users.
  // `role='alert'` already implies an assertive live region, so the "Connection
  // lost" heading announces once. The explicit `aria-live='assertive'` was
  // redundant and, combined with the 1s countdown update, caused AT to
  // re-announce the whole banner on every tick. We drop the redundant
  // aria-live from the root and mark the ticking countdown span aria-live='off'
  // so only the static heading is announced. Mirrors MigrationWarningBanner,
  // which uses role='alert' alone.
  test('does not use an explicit assertive live region and silences the countdown', () => {
    connectionState.value = 'reconnecting';
    nextReconnectAt.value = Date.now() + 5000;

    renderBanner();

    const alert = screen.getByRole('alert');
    // role='alert' alone — no redundant explicit aria-live on the root.
    expect(alert.getAttribute('aria-live')).toBeNull();

    // The per-second countdown text must not re-announce every tick.
    const countdown = screen.getByText(/Retrying in/);
    expect(countdown.getAttribute('aria-live')).toBe('off');
  });
});
