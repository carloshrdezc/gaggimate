import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { h } from 'preact';
import { fireEvent, screen } from '@testing-library/preact';

const { authenticatedFetch, fetchedSettings, themeManagerMock } = vi.hoisted(() => ({
  authenticatedFetch: vi.fn(),
  fetchedSettings: {
    pid: '3.000,0.100,40.000,0.000',
    autowakeupSchedules: '07:00|1111111',
    standbyBrightness: 8,
    wifiSsid: 'net',
    wifiPassword: '',
    mdnsName: 'gaggimate',
  },
  // The Settings page must be a controlled view over themeManager.js: it never
  // touches localStorage or root styles itself. Owning the manager's return
  // values here is what lets us assert that contract.
  themeManagerMock: {
    preferences: { theme: 'midnight', appAccent: null, dashboardAccent: null },
    getThemePreferences: vi.fn(),
    setStoredTheme: vi.fn(),
    setAppAccent: vi.fn(),
    resetAppAccent: vi.fn(),
    setDashboardAccent: vi.fn(),
    resetDashboardAccent: vi.fn(),
  },
}));

const queryState = { isLoading: false, data: fetchedSettings };
vi.mock('preact-fetching', () => ({ useQuery: () => queryState }));

vi.mock('../../utils/themeManager.js', () => ({
  THEME_STORAGE_KEY: 'gaggimate-daisyui-theme',
  getThemePreferences: themeManagerMock.getThemePreferences,
  getStoredTheme: () => themeManagerMock.preferences.theme,
  setStoredTheme: themeManagerMock.setStoredTheme,
  setAppAccent: themeManagerMock.setAppAccent,
  resetAppAccent: themeManagerMock.resetAppAccent,
  setDashboardAccent: themeManagerMock.setDashboardAccent,
  resetDashboardAccent: themeManagerMock.resetDashboardAccent,
  normalizeHexColor: value => {
    const match = /^#?([0-9a-f]{6})$/i.exec(String(value).trim());
    return match ? `#${match[1].toLowerCase()}` : null;
  },
  getThemeAccentDefault: theme =>
    ({ midnight: '#d71921', espresso: '#c07839', matcha: '#5aaa52', blueprint: '#4d8fd1' })[theme],
  getEffectiveAccents: ({ theme, appAccent, dashboardAccent }) => {
    const app =
      appAccent ??
      { midnight: '#d71921', espresso: '#c07839', matcha: '#5aaa52', blueprint: '#4d8fd1' }[theme];
    return { app, dashboard: dashboardAccent ?? app };
  },
  getAvailableThemes: () => [
    { value: 'midnight', label: 'Midnight' },
    { value: 'espresso', label: 'Espresso' },
    { value: 'matcha', label: 'Matcha' },
    { value: 'blueprint', label: 'Blueprint' },
  ],
  applyThemePreferences: vi.fn(),
  initializeTheme: vi.fn(),
  handleThemeChange: vi.fn(),
}));

vi.mock('./PluginCard.jsx', () => ({ PluginCard: () => h('div', {}, 'plugins-body') }));
vi.mock('./GoogleDriveBackupCard.jsx', () => ({ GoogleDriveBackupCard: () => null }));
vi.mock('../../services/localAuthFetch.js', async () => {
  const { localAuthFetchMock } = await import('./Settings.testUtils.jsx');
  return localAuthFetchMock(authenticatedFetch);
});
vi.mock('../../components/ImportButton.jsx', async () => {
  const { importButtonMock } = await import('./Settings.testUtils.jsx');
  return importButtonMock();
});
vi.mock('@fortawesome/react-fontawesome', async () => {
  const { fontAwesomeMock } = await import('./Settings.testUtils.jsx');
  return fontAwesomeMock();
});

import { ApiServiceContext, machine } from '../../services/ApiService.js';
import { Settings } from './index.jsx';
import {
  installSettingsTestGlobals,
  renderSettingsWithInjectedComponent,
  teardownSettingsTest,
} from './Settings.testUtils.jsx';

const {
  getThemePreferences,
  resetAppAccent,
  resetDashboardAccent,
  setAppAccent,
  setDashboardAccent,
  setStoredTheme,
} = themeManagerMock;

function setPreferences(next) {
  themeManagerMock.preferences = { ...themeManagerMock.preferences, ...next };
}

// The accent controls live inside the collapsible Web Settings card (PRO-572),
// which mounts collapsed, so open it before interacting.
function renderSettings() {
  const result = renderSettingsWithInjectedComponent(ApiServiceContext, Settings);
  fireEvent.click(screen.getByRole('button', { name: 'Web Settings' }));
  return result;
}

beforeEach(() => {
  queryState.isLoading = false;
  queryState.data = fetchedSettings;
  authenticatedFetch.mockResolvedValue({ ok: true, json: async () => ({}) });
  // The manager spies live in vi.hoisted state, so they survive across tests;
  // clear call history (and re-install implementations below) every time.
  for (const spy of Object.values(themeManagerMock)) {
    if (typeof spy?.mockClear === 'function') spy.mockClear();
  }
  setPreferences({ theme: 'midnight', appAccent: null, dashboardAccent: null });
  getThemePreferences.mockImplementation(() => ({ ...themeManagerMock.preferences }));
  // A real manager only mutates on a valid value; mirror that so Settings'
  // "commit only when accepted" logic is genuinely exercised.
  setAppAccent.mockImplementation(color => {
    const ok = /^#[0-9a-f]{6}$/i.test(color);
    if (ok) setPreferences({ appAccent: color.toLowerCase() });
    return ok;
  });
  setDashboardAccent.mockImplementation(color => {
    const ok = /^#[0-9a-f]{6}$/i.test(color);
    if (ok) setPreferences({ dashboardAccent: color.toLowerCase() });
    return ok;
  });
  resetAppAccent.mockImplementation(() => {
    setPreferences({ appAccent: null });
    return true;
  });
  resetDashboardAccent.mockImplementation(() => {
    setPreferences({ dashboardAccent: null });
    return true;
  });
  setStoredTheme.mockImplementation(theme => {
    setPreferences({ theme });
    return true;
  });
  installSettingsTestGlobals(machine);
});

afterEach(teardownSettingsTest);

describe('Settings custom accents (PRO-643)', () => {
  it('applies a valid app hex accent and keeps its color input synchronized', () => {
    renderSettings();
    fireEvent.input(screen.getByLabelText('App accent hex'), { target: { value: '#A1B2C3' } });
    fireEvent.blur(screen.getByLabelText('App accent hex'));
    expect(setAppAccent).toHaveBeenCalledWith('#a1b2c3');
    expect(screen.getByLabelText('App accent color').value).toBe('#a1b2c3');
  });

  it('commits a hex draft on Enter as well as blur', () => {
    renderSettings();
    const hex = screen.getByLabelText('App accent hex');
    fireEvent.input(hex, { target: { value: '#0a0b0c' } });
    fireEvent.keyDown(hex, { key: 'Enter' });
    expect(setAppAccent).toHaveBeenCalledWith('#0a0b0c');
  });

  it('commits immediately from the native color picker', () => {
    renderSettings();
    fireEvent.input(screen.getByLabelText('App accent color'), { target: { value: '#00FF00' } });
    expect(setAppAccent).toHaveBeenCalledWith('#00ff00');
    expect(screen.getByLabelText('App accent hex').value).toBe('#00ff00');
  });

  it('keeps the applied accent when invalid hex text is blurred', () => {
    renderSettings();
    fireEvent.input(screen.getByLabelText('App accent hex'), { target: { value: '#bad' } });
    fireEvent.blur(screen.getByLabelText('App accent hex'));
    expect(setAppAccent).not.toHaveBeenCalled();
    expect(screen.getByRole('alert').textContent).toContain('Enter a six-digit hex color');
    // The committed color input is untouched by the rejected draft.
    expect(screen.getByLabelText('App accent color').value).toBe('#d71921');
  });

  it('clears the validation error once a valid value is committed', () => {
    renderSettings();
    const hex = screen.getByLabelText('App accent hex');
    fireEvent.input(hex, { target: { value: 'zzz' } });
    fireEvent.blur(hex);
    expect(screen.getByRole('alert')).toBeTruthy();

    fireEvent.input(hex, { target: { value: '#112233' } });
    fireEvent.blur(hex);
    expect(screen.queryByRole('alert')).toBeNull();
  });

  it('resets the app accent to the selected theme default', () => {
    setPreferences({ appAccent: '#ff6600' });
    renderSettings();
    expect(screen.getByLabelText('App accent color').value).toBe('#ff6600');

    fireEvent.click(screen.getByRole('button', { name: 'Reset to theme default' }));
    expect(resetAppAccent).toHaveBeenCalledOnce();
    expect(screen.getByLabelText('App accent color').value).toBe('#d71921');
  });

  it('inherits the app accent until the dashboard override is enabled', () => {
    renderSettings();
    expect(screen.queryByLabelText('Dashboard accent color')).toBeNull();
    fireEvent.click(screen.getByRole('checkbox', { name: 'Use a separate dashboard accent' }));
    expect(screen.getByLabelText('Dashboard accent color')).toBeTruthy();
  });

  it('seeds a newly enabled dashboard override from the effective app accent', () => {
    setPreferences({ appAccent: '#ff6600' });
    renderSettings();
    fireEvent.click(screen.getByRole('checkbox', { name: 'Use a separate dashboard accent' }));
    expect(setDashboardAccent).toHaveBeenCalledWith('#ff6600');
    expect(screen.getByLabelText('Dashboard accent color').value).toBe('#ff6600');
  });

  it('shows the dashboard picker pre-populated when an override is already stored', () => {
    setPreferences({ appAccent: '#ff6600', dashboardAccent: '#33cc99' });
    renderSettings();
    expect(screen.getByRole('checkbox', { name: 'Use a separate dashboard accent' }).checked).toBe(
      true,
    );
    expect(screen.getByLabelText('Dashboard accent color').value).toBe('#33cc99');
  });

  it('applies a dashboard accent without touching the app accent', () => {
    setPreferences({ appAccent: '#ff6600', dashboardAccent: '#33cc99' });
    renderSettings();
    fireEvent.input(screen.getByLabelText('Dashboard accent hex'), {
      target: { value: '#0000FF' },
    });
    fireEvent.blur(screen.getByLabelText('Dashboard accent hex'));
    expect(setDashboardAccent).toHaveBeenCalledWith('#0000ff');
    expect(setAppAccent).not.toHaveBeenCalled();
    expect(screen.getByLabelText('App accent color').value).toBe('#ff6600');
  });

  it('reports an invalid dashboard draft independently of the app accent field', () => {
    setPreferences({ dashboardAccent: '#33cc99' });
    renderSettings();
    fireEvent.input(screen.getByLabelText('Dashboard accent hex'), { target: { value: '#nope' } });
    fireEvent.blur(screen.getByLabelText('Dashboard accent hex'));
    expect(setDashboardAccent).not.toHaveBeenCalled();
    expect(screen.getAllByRole('alert')).toHaveLength(1);
    expect(screen.getByLabelText('Dashboard accent color').value).toBe('#33cc99');
  });

  it('resets the dashboard override by clearing the manager preference and hiding its picker', () => {
    renderSettings();
    fireEvent.click(screen.getByRole('checkbox', { name: 'Use a separate dashboard accent' }));
    fireEvent.click(screen.getByRole('button', { name: 'Reset dashboard accent' }));
    expect(resetDashboardAccent).toHaveBeenCalledOnce();
    expect(screen.queryByLabelText('Dashboard accent color')).toBeNull();
  });

  it('unchecking the override clears the dashboard preference', () => {
    setPreferences({ dashboardAccent: '#33cc99' });
    renderSettings();
    fireEvent.click(screen.getByRole('checkbox', { name: 'Use a separate dashboard accent' }));
    expect(resetDashboardAccent).toHaveBeenCalledOnce();
    expect(screen.queryByLabelText('Dashboard accent color')).toBeNull();
  });

  it('changing the named theme keeps active overrides and re-reads the manager', () => {
    setPreferences({ appAccent: '#ff6600', dashboardAccent: '#33cc99' });
    renderSettings();
    fireEvent.change(screen.getByLabelText('Theme', { selector: '#webui-theme' }), {
      target: { value: 'matcha' },
    });
    expect(setStoredTheme).toHaveBeenCalledWith('matcha');
    expect(screen.getByLabelText('App accent color').value).toBe('#ff6600');
    expect(screen.getByLabelText('Dashboard accent color').value).toBe('#33cc99');
  });

  it('follows the new theme default accent after a theme change with no override', () => {
    renderSettings();
    fireEvent.change(screen.getByLabelText('Theme', { selector: '#webui-theme' }), {
      target: { value: 'blueprint' },
    });
    expect(screen.getByLabelText('App accent color').value).toBe('#4d8fd1');
  });

  it('sources the theme options from the manager', () => {
    renderSettings();
    const options = [...screen.getByLabelText('Theme', { selector: '#webui-theme' }).options].map(
      o => o.value,
    );
    expect(options).toEqual(['midnight', 'espresso', 'matcha', 'blueprint']);
  });

  it('labels the accent previews with text, not color alone', () => {
    setPreferences({ dashboardAccent: '#33cc99' });
    renderSettings();
    expect(screen.getByText(/App accent preview/)).toBeTruthy();
    expect(screen.getByText(/Dashboard accent preview #33cc99/)).toBeTruthy();
  });
});
