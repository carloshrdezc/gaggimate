import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { h } from 'preact';
import { cleanup, fireEvent, render, screen } from '@testing-library/preact';

const { authenticatedFetch, fetchedSettings } = vi.hoisted(() => ({
  authenticatedFetch: vi.fn(),
  fetchedSettings: {
    pid: '3.000,0.100,40.000,0.000',
    autowakeupSchedules: '07:00|1111111',
    standbyBrightness: 8,
    wifiSsid: 'net',
    wifiPassword: '',
    mdnsName: 'gaggimate',
  },
}));

// Controllable loading flag so we can exercise both the in-page render and the
// loading/recovery screen.
const queryState = { isLoading: false, data: fetchedSettings };
vi.mock('preact-fetching', () => ({
  useQuery: () => queryState,
}));

vi.mock('../../services/localAuthFetch.js', () => ({
  authenticatedFetch,
  bootstrapLocalAuth: vi.fn(),
  getLocalAuthToken: () => null,
  isValidLocalAuthToken: token => /^[0-9a-f]{32}$/.test(String(token).trim()),
  LOCAL_AUTH_TOKEN_ERROR: 'err',
  localAuthDownloadUrl: url => url,
  localAuthHandoffUrl: hostname => `http://${hostname}.local/#localAuthToken=t`,
  bootstrapLocalAuthFromHash: vi.fn(),
  MDNS_NAME_ERROR: 'mdns err',
}));

// Real Card (collapse behaviour under test); stub the heavy plugin/backup cards
// to a real collapsible Card so they still participate in the openMap wiring
// without dragging in Google Drive / signals machinery.
vi.mock('./PluginCard.jsx', () => ({ PluginCard: () => h('div', {}, 'plugins-body') }));
vi.mock('./GoogleDriveBackupCard.jsx', async () => {
  const { default: Card } = await import('../../components/Card.jsx');
  return {
    GoogleDriveBackupCard: ({ collapsible, open, onToggle }) =>
      h(
        Card,
        { sm: 10, lg: 5, title: 'Google Drive Backup', collapsible, open, onToggle },
        h('div', {}, 'gdrive-body'),
      ),
  };
});
vi.mock('../../components/ImportButton.jsx', () => ({ ImportButton: () => null }));
vi.mock('@fortawesome/react-fontawesome', () => ({ FontAwesomeIcon: () => null }));

import { ApiServiceContext, machine } from '../../services/ApiService.js';
import { Settings } from './index.jsx';

// The 10 top-level card titles governed by Expand All / Collapse All.
const TOP_LEVEL_TITLES = [
  'Temperature Settings',
  'User Preferences',
  'Web Settings',
  'Google Drive Backup',
  'System Preferences',
  'Machine Settings',
  'Display Settings',
  'Sunrise Settings',
  'Plugins',
  'Remote Access',
];

function renderSettings() {
  render(h(ApiServiceContext.Provider, { value: { authenticateLocal: vi.fn() } }, h(Settings)));
}

beforeEach(() => {
  queryState.isLoading = false;
  queryState.data = fetchedSettings;
  authenticatedFetch.mockResolvedValue({ ok: true, json: async () => ({}) });
  vi.stubGlobal('alert', vi.fn());
  vi.spyOn(console, 'error').mockImplementation(() => {});
  vi.stubGlobal('navigator', { clipboard: { writeText: vi.fn() } });
  // Enable Sunrise (ledControl) so all 10 top-level cards render.
  machine.value = {
    ...machine.value,
    capabilities: { ...machine.value.capabilities, ledControl: true },
  };
});

afterEach(() => {
  cleanup();
  vi.restoreAllMocks();
  vi.unstubAllGlobals();
});

// A card body stays MOUNTED while collapsed and is hidden via the `hidden`
// attribute (PRO-572), so Settings' `new FormData(form)` still captures fields
// edited before collapse. Card headers are toggle buttons whose accessible name
// is the title; aria-expanded reflects open state.
function cardHeader(title) {
  return screen.getByRole('button', { name: title });
}
// "collapsed" now means present-but-inside-a-hidden-wrapper, not unmounted.
function isCollapsedHidden(el) {
  return el != null && el.closest('[hidden]') != null;
}

describe('Settings collapsible cards (PRO-572)', () => {
  it('renders all 10 top-level cards collapsed on load', () => {
    renderSettings();
    for (const title of TOP_LEVEL_TITLES) {
      expect(cardHeader(title).getAttribute('aria-expanded')).toBe('false');
    }
    // Bodies stay mounted while collapsed but are hidden (sample a couple).
    expect(isCollapsedHidden(screen.queryByLabelText('Default Steam Temperature'))).toBe(true);
    expect(isCollapsedHidden(screen.queryByText('plugins-body'))).toBe(true);
  });

  it('Expand All opens all 10 cards and flips the button label to Collapse All', () => {
    renderSettings();
    const btn = screen.getByRole('button', { name: 'Expand all settings sections' });
    expect(btn.textContent).toContain('Expand All');

    fireEvent.click(btn);

    for (const title of TOP_LEVEL_TITLES) {
      expect(cardHeader(title).getAttribute('aria-expanded')).toBe('true');
    }
    // A previously-hidden field is now mounted.
    expect(screen.getByLabelText('Default Steam Temperature')).toBeTruthy();
    // Label flipped.
    const collapseBtn = screen.getByRole('button', { name: 'Collapse all settings sections' });
    expect(collapseBtn.textContent).toContain('Collapse All');

    // Collapse All closes everything and flips back.
    fireEvent.click(collapseBtn);
    for (const title of TOP_LEVEL_TITLES) {
      expect(cardHeader(title).getAttribute('aria-expanded')).toBe('false');
    }
    expect(
      screen.getByRole('button', { name: 'Expand all settings sections' }).textContent,
    ).toContain('Expand All');
  });

  it('toggling one card individually does not affect the others (and does not flip to Collapse All)', () => {
    renderSettings();
    fireEvent.click(cardHeader('Machine Settings'));

    expect(cardHeader('Machine Settings').getAttribute('aria-expanded')).toBe('true');
    for (const title of TOP_LEVEL_TITLES.filter(t => t !== 'Machine Settings')) {
      expect(cardHeader(title).getAttribute('aria-expanded')).toBe('false');
    }
    // Not all open -> button still reads Expand All.
    expect(screen.getByRole('button', { name: 'Expand all settings sections' })).toBeTruthy();
  });

  it('forces the loading-screen LocalAuthRecoveryCard open and non-collapsible (no toggle header)', () => {
    queryState.isLoading = true;
    queryState.data = undefined;
    renderSettings();

    // The recovery card body is visible (forced open) on the loading screen...
    expect(screen.getByLabelText('Paste admin token')).toBeTruthy();
    // ...and its title is NOT a collapse toggle button here.
    expect(screen.queryByRole('button', { name: 'Local admin token' })).toBeNull();
    // The Expand/Collapse All control does not render on the loading screen.
    expect(screen.queryByRole('button', { name: /settings sections/ })).toBeNull();
  });

  it('in-page LocalAuthRecoveryCard is collapsible and default collapsed', () => {
    renderSettings();
    const header = screen.getByRole('button', { name: 'Local admin token' });
    expect(header.getAttribute('aria-expanded')).toBe('false');
    // Body stays mounted but hidden until opened.
    expect(isCollapsedHidden(screen.queryByLabelText('Paste admin token'))).toBe(true);
    fireEvent.click(header);
    expect(screen.getByLabelText('Paste admin token')).toBeTruthy();
  });
});
