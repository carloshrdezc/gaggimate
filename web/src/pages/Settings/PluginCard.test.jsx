import { afterEach, describe, expect, it, vi } from 'vitest';
import { h } from 'preact';
import { useState } from 'preact/hooks';
import { cleanup, fireEvent, render, screen } from '@testing-library/preact';

vi.mock('@fortawesome/react-fontawesome', () => ({ FontAwesomeIcon: () => null }));
vi.mock('../../assets/homekit.png', () => ({ default: 'homekit.png' }));

import { PluginCard } from './PluginCard.jsx';

// Harness that mimics Settings/index.jsx's `onChange(key)` toggle handler for
// the boolean plugin flags, so the sub-card sees enabled flip like it does in
// the real page.
function Harness() {
  const [formData, setFormData] = useState({});
  const onChange = key => () => setFormData(prev => ({ ...prev, [key]: !prev[key] }));
  return h(PluginCard, {
    formData,
    onChange,
    autowakeupSchedules: [],
    addAutoWakeupSchedule: vi.fn(),
    removeAutoWakeupSchedule: vi.fn(),
    updateAutoWakeupTime: vi.fn(),
    updateAutoWakeupDay: vi.fn(),
  });
}

afterEach(() => {
  cleanup();
  vi.restoreAllMocks();
});

// Use HomeKit as the representative sub-card: its body has stable, unique text.
const HOMEKIT_BODY = /Open the Home app on your iOS device/i;

describe('PluginCard sub-card collapse (PRO-572)', () => {
  it('auto-expands the body when the plugin is toggled ON', () => {
    render(h(Harness));

    // Collapsed + disabled initially: no body, no chevron.
    expect(screen.queryByText(HOMEKIT_BODY)).toBeNull();

    // Toggle HomeKit on (it is the 2nd switch; query by accessible container).
    const toggles = screen.getAllByRole('switch');
    // Order matches render order: autowakeup, homekit, boiler, grind, ha.
    fireEvent.click(toggles[1]);

    // Body now visible (auto-expanded on enable).
    expect(screen.getByText(HOMEKIT_BODY)).toBeTruthy();
    // Chevron control appears while enabled.
    expect(screen.getByRole('button', { name: /Collapse HomeKit/ })).toBeTruthy();
  });

  it('lets the chevron manually collapse the body while the toggle stays ON', () => {
    render(h(Harness));
    const toggles = screen.getAllByRole('switch');
    fireEvent.click(toggles[1]); // enable HomeKit
    expect(screen.getByText(HOMEKIT_BODY)).toBeTruthy();

    // Manual collapse via chevron: body hidden, but toggle stays checked.
    fireEvent.click(screen.getByRole('button', { name: /Collapse HomeKit/ }));
    expect(screen.queryByText(HOMEKIT_BODY)).toBeNull();
    expect(screen.getAllByRole('switch')[1].getAttribute('aria-checked')).toBe('true');
    // Chevron still present (enabled), now labelled Expand.
    expect(screen.getByRole('button', { name: /Expand HomeKit/ })).toBeTruthy();

    // Re-expand via chevron.
    fireEvent.click(screen.getByRole('button', { name: /Expand HomeKit/ }));
    expect(screen.getByText(HOMEKIT_BODY)).toBeTruthy();
  });

  it('toggling OFF hides the body and resets open state so the next ON starts expanded', () => {
    render(h(Harness));
    let toggles = screen.getAllByRole('switch');
    fireEvent.click(toggles[1]); // ON -> auto-expand
    expect(screen.getByText(HOMEKIT_BODY)).toBeTruthy();

    // Manually collapse while still on.
    fireEvent.click(screen.getByRole('button', { name: /Collapse HomeKit/ }));
    expect(screen.queryByText(HOMEKIT_BODY)).toBeNull();

    // Toggle OFF: body gone, chevron gone.
    fireEvent.click(screen.getAllByRole('switch')[1]);
    expect(screen.queryByText(HOMEKIT_BODY)).toBeNull();
    expect(screen.queryByRole('button', { name: /HomeKit/ })).toBeNull();

    // Toggle ON again: resets to expanded (not stuck on the earlier manual collapse).
    fireEvent.click(screen.getAllByRole('switch')[1]);
    expect(screen.getByText(HOMEKIT_BODY)).toBeTruthy();
  });
});
