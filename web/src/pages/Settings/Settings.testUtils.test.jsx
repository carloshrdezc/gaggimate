import { afterEach, describe, expect, it, vi } from 'vitest';
import { createContext, h } from 'preact';
import { render } from '@testing-library/preact';
import { signal } from '@preact/signals';

import {
  fontAwesomeMock,
  importButtonMock,
  installSettingsTestGlobals,
  localAuthFetchMock,
  renderSettingsWithInjectedComponent,
  teardownSettingsTest,
} from './Settings.testUtils.jsx';

// Direct unit test for the Settings.testUtils.jsx fixture factories (PRO-582,
// ref PRO-580 / PRO-581). The factories are exercised indirectly by every
// Settings.*.test.jsx suite, but nothing asserts their shape/behavior in
// isolation. This suite imports each factory directly and pins the contract so
// a change to a factory that breaks the consuming suites fails here first, with
// a clearer signal than a downstream render error.

afterEach(() => {
  // installSettingsTestGlobals stubs globals; restore after each test even
  // though most cases don't call it, so a stub can't leak across tests.
  vi.restoreAllMocks();
  vi.unstubAllGlobals();
});

describe('localAuthFetchMock', () => {
  it('returns the localAuthFetch module shape with the injected authenticatedFetch reference', () => {
    const authenticatedFetch = vi.fn();
    const mock = localAuthFetchMock(authenticatedFetch);

    // Same reference the caller passed in, so tests can assert against the spy.
    expect(mock.authenticatedFetch).toBe(authenticatedFetch);

    // All exports the real module surface (and the consuming suites) rely on.
    expect(mock).toEqual(
      expect.objectContaining({
        authenticatedFetch,
        bootstrapLocalAuth: expect.any(Function),
        getLocalAuthToken: expect.any(Function),
        isValidLocalAuthToken: expect.any(Function),
        LOCAL_AUTH_TOKEN_ERROR: expect.any(String),
        localAuthDownloadUrl: expect.any(Function),
        localAuthHandoffUrl: expect.any(Function),
        bootstrapLocalAuthFromHash: expect.any(Function),
        MDNS_NAME_ERROR: expect.any(String),
      }),
    );

    // Spot-check the non-trivial helper behaviors.
    expect(mock.getLocalAuthToken()).toBeNull();
    expect(mock.isValidLocalAuthToken('0123456789abcdef0123456789abcdef')).toBe(true);
    expect(mock.isValidLocalAuthToken('not-a-token')).toBe(false);
    expect(mock.localAuthDownloadUrl('http://x/y')).toBe('http://x/y');
    expect(mock.localAuthHandoffUrl('gaggimate')).toBe('http://gaggimate.local/#localAuthToken=t');
  });
});

describe('fontAwesomeMock', () => {
  it('returns a FontAwesomeIcon component that renders null', () => {
    const mock = fontAwesomeMock();
    expect(mock.FontAwesomeIcon).toEqual(expect.any(Function));

    const { container } = render(h(mock.FontAwesomeIcon, {}));
    expect(container.innerHTML).toBe('');
  });
});

describe('importButtonMock', () => {
  it('returns an ImportButton component that renders null', () => {
    const mock = importButtonMock();
    expect(mock.ImportButton).toEqual(expect.any(Function));

    const { container } = render(h(mock.ImportButton, {}));
    expect(container.innerHTML).toBe('');
  });
});

describe('renderSettingsWithInjectedComponent', () => {
  it('renders the injected component inside the injected context provider', () => {
    // Minimal ApiServiceContext + Settings stand-ins: the factory only needs a
    // context with a `.Provider` and a component to render into it.
    const ApiServiceContext = createContext(null);
    const Settings = () => h('div', { 'data-testid': 'injected-settings' }, 'settings');

    const { getByTestId } = renderSettingsWithInjectedComponent(ApiServiceContext, Settings);

    expect(getByTestId('injected-settings').textContent).toBe('settings');
  });
});

describe('installSettingsTestGlobals', () => {
  it('stubs alert / console.error / clipboard and enables the ledControl capability', () => {
    const machine = signal({ capabilities: { ledControl: false } });

    installSettingsTestGlobals(machine);

    // alert is stubbed to a spy (callable, no window popup in jsdom).
    expect(() => alert('hi')).not.toThrow();
    expect(alert).toEqual(expect.any(Function));

    // console.error is silenced via a spy.
    expect(vi.isMockFunction(console.error)).toBe(true);

    // clipboard is stubbed with a writeText spy.
    expect(navigator.clipboard.writeText).toEqual(expect.any(Function));

    // machine.value.capabilities.ledControl is flipped on, other capabilities
    // preserved via spread.
    expect(machine.value.capabilities.ledControl).toBe(true);
  });

  it('preserves existing capabilities and machine fields when enabling ledControl', () => {
    const machine = signal({ connected: true, capabilities: { dimming: true, ledControl: false } });

    installSettingsTestGlobals(machine);

    expect(machine.value.connected).toBe(true);
    expect(machine.value.capabilities).toEqual({ dimming: true, ledControl: true });
  });
});

describe('teardownSettingsTest', () => {
  it('restores mocks and unstubs globals installed by installSettingsTestGlobals', () => {
    const realConsoleError = console.error;
    const realAlert = globalThis.alert;
    const machine = signal({ capabilities: { ledControl: false } });

    installSettingsTestGlobals(machine);
    // Globals are now stubbed.
    expect(vi.isMockFunction(console.error)).toBe(true);
    expect(vi.isMockFunction(globalThis.alert)).toBe(true);

    teardownSettingsTest();

    // restoreAllMocks reverts the console.error spy to the original.
    expect(console.error).toBe(realConsoleError);
    // unstubAllGlobals removes the alert stub, restoring jsdom's original.
    expect(vi.isMockFunction(globalThis.alert)).toBe(false);
    expect(globalThis.alert).toBe(realAlert);
  });
});
