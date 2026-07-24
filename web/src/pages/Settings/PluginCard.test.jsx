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

// The body stays mounted while enabled and is hidden via the `hidden` attribute
// when collapsed (PRO-572) — so FormData in Settings' onSubmit still captures
// its inputs. "Visible" therefore means: present in the DOM AND not inside a
// `hidden` wrapper. "Collapsed" means: present in the DOM but inside `hidden`.
function bodyVisible() {
  const el = screen.queryByText(HOMEKIT_BODY);
  return el != null && el.closest('[hidden]') == null;
}
function bodyMountedButHidden() {
  const el = screen.queryByText(HOMEKIT_BODY);
  return el != null && el.closest('[hidden]') != null;
}

describe('PluginCard sub-card collapse (PRO-572)', () => {
  it('auto-expands the body when the plugin is toggled ON', () => {
    render(h(Harness));

    // Collapsed + disabled initially: no body at all, no chevron.
    expect(screen.queryByText(HOMEKIT_BODY)).toBeNull();

    // Toggle HomeKit on (it is the 2nd switch; query by accessible container).
    const toggles = screen.getAllByRole('switch');
    // Order matches render order: autowakeup, homekit, boiler, grind, ha.
    fireEvent.click(toggles[1]);

    // Body now visible (auto-expanded on enable).
    expect(bodyVisible()).toBe(true);
    // Chevron control appears while enabled.
    expect(screen.getByRole('button', { name: /Collapse HomeKit/ })).toBeTruthy();
  });

  it('lets the chevron manually collapse the body while the toggle stays ON', () => {
    render(h(Harness));
    const toggles = screen.getAllByRole('switch');
    fireEvent.click(toggles[1]); // enable HomeKit
    expect(bodyVisible()).toBe(true);

    // Manual collapse via chevron: body stays MOUNTED (so FormData still sees
    // its fields) but is hidden, and the toggle stays checked.
    fireEvent.click(screen.getByRole('button', { name: /Collapse HomeKit/ }));
    expect(bodyMountedButHidden()).toBe(true);
    expect(screen.getAllByRole('switch')[1].getAttribute('aria-checked')).toBe('true');
    // Chevron still present (enabled), now labelled Expand.
    expect(screen.getByRole('button', { name: /Expand HomeKit/ })).toBeTruthy();

    // Re-expand via chevron.
    fireEvent.click(screen.getByRole('button', { name: /Expand HomeKit/ }));
    expect(bodyVisible()).toBe(true);
  });

  it('toggling OFF unmounts the body and resets open state so the next ON starts expanded', () => {
    render(h(Harness));
    let toggles = screen.getAllByRole('switch');
    fireEvent.click(toggles[1]); // ON -> auto-expand
    expect(bodyVisible()).toBe(true);

    // Manually collapse while still on: mounted but hidden.
    fireEvent.click(screen.getByRole('button', { name: /Collapse HomeKit/ }));
    expect(bodyMountedButHidden()).toBe(true);

    // Toggle OFF: body fully unmounted (disabled plugins must not send fields),
    // chevron gone.
    fireEvent.click(screen.getAllByRole('switch')[1]);
    expect(screen.queryByText(HOMEKIT_BODY)).toBeNull();
    expect(screen.queryByRole('button', { name: /HomeKit/ })).toBeNull();

    // Toggle ON again: resets to expanded (not stuck on the earlier manual collapse).
    fireEvent.click(screen.getAllByRole('switch')[1]);
    expect(bodyVisible()).toBe(true);
  });

  it('starts the sub-card expanded when the plugin is already enabled at initial mount (PRO-573)', () => {
    // A plugin fetched as enabled from /api/settings on page load must reveal
    // its body immediately — no click needed. Pre-PRO-573 the internal open
    // state hardcoded `false`, so an already-enabled plugin rendered collapsed
    // and the user had to expand the sub-card manually. Harness mounts with
    // HomeKit already enabled (mirrors formData.homekit === true from the API).
    function PreEnabledHarness() {
      const [formData, setFormData] = useState({ homekit: true });
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
    render(h(PreEnabledHarness));

    // Body visible on first paint, without any click.
    expect(bodyVisible()).toBe(true);
    // Toggle reflects the enabled state, and the chevron (Collapse) is present.
    expect(screen.getAllByRole('switch')[1].getAttribute('aria-checked')).toBe('true');
    expect(screen.getByRole('button', { name: /Collapse HomeKit/ })).toBeTruthy();
  });

  it('keeps an edited sub-field in FormData after the enabled sub-card is collapsed', () => {
    // The core PRO-572 bug for plugin sub-cards: enable a plugin, edit one of
    // its inputs, collapse the sub-card via the chevron (plugin still ON), then
    // read the enclosing form via `new FormData(form)` the way Settings.onSubmit
    // does. The edited value must survive because the body stays mounted (hidden)
    // instead of being unmounted on collapse.
    //
    // Dedicated harness whose onChange mirrors the real Settings handler: text
    // fields store the event value, boolean flags (the enable toggle) invert.
    function FieldHarness() {
      const [formData, setFormData] = useState({});
      const onChange = key => e => {
        if (key === 'boilerFillActive') {
          setFormData(prev => ({ ...prev, boilerFillActive: !prev.boilerFillActive }));
        } else {
          setFormData(prev => ({ ...prev, [key]: e.currentTarget.value }));
        }
      };
      return h('form', {}, [
        h(PluginCard, {
          formData,
          onChange,
          autowakeupSchedules: [],
          addAutoWakeupSchedule: vi.fn(),
          removeAutoWakeupSchedule: vi.fn(),
          updateAutoWakeupTime: vi.fn(),
          updateAutoWakeupDay: vi.fn(),
        }),
      ]);
    }
    render(h(FieldHarness));

    // Enable Boiler Refill Plugin (3rd switch: autowakeup, homekit, boiler, ...).
    fireEvent.click(screen.getAllByRole('switch')[2]);

    // Edit one of its number fields. Controlled inputs here use Preact onChange,
    // which fires on the native `change` event.
    const input = document.querySelector('input[name="startupFillTime"]');
    expect(input).not.toBeNull();
    fireEvent.change(input, { target: { value: '7' } });

    // Collapse the sub-card via chevron while the plugin stays enabled.
    fireEvent.click(screen.getByRole('button', { name: /Collapse Boiler Refill Plugin/ }));

    // The input is still in the DOM (hidden), so FormData captures it.
    const form = document.querySelector('form');
    const fd = new FormData(form);
    expect(fd.get('startupFillTime')).toBe('7');
  });
});
