// PRO-599 — integration harness for "OTA channel Save & Refresh leaves Display
// update blocked". Drives the real <OTA/> component through the firmware's
// message flow so we exercise the full enable-gate at runtime (the pure-logic
// tests in otaLogic.test.js cover the helpers in isolation).
//
// The fix: the firmware now reports authoritative per-component flash
// eligibility (`displayFlashEligible` / `controllerFlashEligible`) computed with
// the same policy it uses to decide the actual flash. The UI trusts that signal
// (with the same anti-stale-`_latest_url` guards) instead of re-deriving
// eligibility from installedChannel + semver, which used to leave a channel
// switch to an equal/lower version stuck disabled.

import { afterEach, beforeEach, describe, expect, test, vi } from 'vitest';
import { h } from 'preact';
import { render, screen, cleanup, fireEvent, act } from '@testing-library/preact';

vi.mock('@fortawesome/react-fontawesome', () => ({
  FontAwesomeIcon: () => h('span', { 'data-testid': 'fa-icon' }),
}));

import { OTA } from './index.jsx';
import { ApiServiceContext, machine } from '../../services/ApiService.js';

// A tiny fake ApiService: records sends and lets the test push server messages
// back through the registered listeners, mirroring the real on/off/send
// event-emitter contract.
function makeApi() {
  const listeners = {};
  return {
    sent: [],
    on(tp, cb) {
      (listeners[tp] ||= []).push(cb);
      return cb;
    },
    off(tp, cb) {
      listeners[tp] = (listeners[tp] || []).filter(x => x !== cb);
    },
    send(msg) {
      this.sent.push(msg);
    },
    __emit(tp, msg) {
      (listeners[tp] || []).forEach(cb => cb(msg));
    },
  };
}

const renderOTA = api => render(h(ApiServiceContext.Provider, { value: api }, h(OTA, {})));

const base = {
  displayVersion: '2.0.10',
  controllerVersion: '2.0.10',
  latestVersion: '2.0.10',
  hardware: 'test-hw',
  availableVersions: ['2.0.10', '2.0.8'],
};

beforeEach(() => {
  vi.useFakeTimers();
  machine.value = { status: { rssi: -55 } };
});

afterEach(() => {
  cleanup();
  vi.useRealTimers();
});

// Leave the initial loading spinner: fire the 500ms req:ota-settings kick, then
// deliver a res:ota-settings.
function boot(api, msg) {
  act(() => {
    vi.advanceTimersByTime(600);
  });
  act(() => {
    api.__emit('res:ota-settings', msg);
  });
}

const displayBtn = () => screen.getByRole('button', { name: /Display$/ });

describe('PRO-599 OTA channel Save & Refresh', () => {
  test('new firmware: switching latest -> beta (equal semver) enables Display via flashEligible', () => {
    const api = makeApi();
    renderOTA(api);
    boot(api, {
      ...base,
      status: '2.0.10',
      channel: 'latest',
      installedChannel: 'latest',
      displayUpdateAvailable: false,
      controllerUpdateAvailable: false,
      displayFlashEligible: false, // no update, no switch on latest
      controllerFlashEligible: false,
    });

    expect(displayBtn().disabled).toBe(true);

    act(() => {
      fireEvent.change(screen.getByRole('combobox'), { target: { value: 'beta' } });
    });
    expect(displayBtn().disabled).toBe(true); // unsaved

    act(() => {
      fireEvent.click(screen.getByRole('button', { name: /Save & Refresh/ }));
    });
    const sent = api.sent.filter(m => m.tp === 'req:ota-settings' && m.update).pop();
    expect(sent.channel).toBe('beta');

    // Firmware acks "Checking..." first (still describes the OLD channel's
    // resolved head — must NOT enable yet).
    act(() => {
      api.__emit('res:ota-settings', {
        ...base,
        status: 'Checking...',
        channel: 'beta',
        installedChannel: 'latest',
        displayUpdateAvailable: false,
        controllerUpdateAvailable: false,
        // stale-true from the previous channel: the status guard must ignore it.
        displayFlashEligible: true,
        controllerFlashEligible: true,
      });
    });
    expect(displayBtn().disabled).toBe(true); // status is Checking... -> blocked

    // Then the resolved beta head. Semver equal (2.0.10) so displayUpdateAvailable
    // is false, but the device authoritatively says the channel switch is
    // flash-eligible.
    act(() => {
      api.__emit('res:ota-settings', {
        ...base,
        status: '2.0.10',
        channel: 'beta',
        installedChannel: 'latest',
        displayUpdateAvailable: false,
        controllerUpdateAvailable: false,
        displayFlashEligible: true,
        controllerFlashEligible: true,
      });
    });

    // ACCEPTANCE: Update Display is enabled on the switch despite semver saying
    // "no update"; the label reflects the switch.
    expect(displayBtn().disabled).toBe(false);
    expect(screen.getByRole('button', { name: /Switch to Beta Display/ })).toBeTruthy();
  });

  test('new firmware: per-component eligibility (display eligible, controller not)', () => {
    const api = makeApi();
    renderOTA(api);
    boot(api, {
      ...base,
      status: '2.0.10',
      channel: 'beta',
      installedChannel: 'latest',
      displayUpdateAvailable: false,
      controllerUpdateAvailable: false,
      displayFlashEligible: true,
      controllerFlashEligible: false,
    });
    expect(screen.getByRole('button', { name: /Display$/ }).disabled).toBe(false);
    expect(screen.getByRole('button', { name: /Controller$/ }).disabled).toBe(true);
  });

  test('new firmware: failed resolve reports flashEligible=false -> Display blocked, Save usable', () => {
    const api = makeApi();
    renderOTA(api);
    boot(api, {
      ...base,
      status: '2.0.10',
      channel: 'latest',
      installedChannel: 'latest',
      displayUpdateAvailable: false,
      controllerUpdateAvailable: false,
      displayFlashEligible: false,
      controllerFlashEligible: false,
    });
    act(() => {
      fireEvent.change(screen.getByRole('combobox'), { target: { value: 'beta' } });
    });
    act(() => {
      fireEvent.click(screen.getByRole('button', { name: /Save & Refresh/ }));
    });
    act(() => {
      api.__emit('res:ota-settings', {
        ...base,
        status: 'Update failed',
        channel: 'beta',
        installedChannel: 'latest',
        displayUpdateAvailable: false,
        controllerUpdateAvailable: false,
        displayFlashEligible: false,
        controllerFlashEligible: false,
      });
    });
    expect(displayBtn().disabled).toBe(true);
    expect(screen.getByRole('button', { name: /Save & Refresh/ }).disabled).toBe(false);
  });

  test('older firmware without flashEligible fields falls back to legacy inference', () => {
    // No displayFlashEligible/controllerFlashEligible in the payload. With a
    // present installedChannel the legacy channel-switch inference still enables
    // the switch, so behavior is unchanged for the common case.
    const api = makeApi();
    renderOTA(api);
    boot(api, {
      ...base,
      status: '2.0.10',
      channel: 'latest',
      installedChannel: 'latest',
      displayUpdateAvailable: false,
      controllerUpdateAvailable: false,
      // no flashEligible fields
    });
    act(() => {
      fireEvent.change(screen.getByRole('combobox'), { target: { value: 'beta' } });
    });
    act(() => {
      fireEvent.click(screen.getByRole('button', { name: /Save & Refresh/ }));
    });
    act(() => {
      api.__emit('res:ota-settings', {
        ...base,
        status: '2.0.10',
        channel: 'beta',
        installedChannel: 'latest',
        displayUpdateAvailable: false,
        controllerUpdateAvailable: false,
        // still no flashEligible fields -> legacy canSwitchChannel path
      });
    });
    // Legacy inference: beta != latest -> switch enabled.
    expect(displayBtn().disabled).toBe(false);
  });
});
