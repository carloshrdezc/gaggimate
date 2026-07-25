import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { h } from 'preact';
import { fireEvent, screen } from '@testing-library/preact';

import {
  fontAwesomeMock,
  importButtonMock,
  installSettingsTestGlobals,
  localAuthFetchMock,
  renderSettings as renderSettingsWith,
  teardownSettingsTest,
} from './Settings.testUtils.jsx';

// Regression test for the PR #561 review blocker (PRO-572): collapsing a
// top-level Settings card must NOT drop a field the user edited from the Save
// payload. Before the fix, Card unmounted its body on collapse, so the edited
// <input> left the DOM and `new FormData(form)` in onSubmit silently omitted
// it — the edit was lost with no warning. This test expands a card, edits a
// field, collapses the card, clicks Save, and asserts the edited value is
// present in the submitted FormData.

const { authenticatedFetch, fetchedSettings } = vi.hoisted(() => ({
  authenticatedFetch: vi.fn(),
  fetchedSettings: {
    pid: '3.000,0.100,40.000,0.000',
    autowakeupSchedules: '07:00|1111111',
    standbyBrightness: 8,
    wifiSsid: 'net',
    wifiPassword: '',
    mdnsName: 'gaggimate',
    targetSteamTemp: 135,
    targetWaterTemp: 80,
  },
}));

const queryState = { isLoading: false, data: fetchedSettings };
vi.mock('preact-fetching', () => ({
  useQuery: () => queryState,
}));

vi.mock('../../services/localAuthFetch.js', () => localAuthFetchMock(authenticatedFetch));

// Keep the plugin/backup cards light — this test only exercises a top-level
// non-plugin card (Temperature Settings) so we don't need their internals.
vi.mock('./PluginCard.jsx', () => ({ PluginCard: () => h('div', {}, 'plugins-body') }));
vi.mock('./GoogleDriveBackupCard.jsx', () => ({
  GoogleDriveBackupCard: () => h('div', {}, 'gdrive-body'),
}));
vi.mock('../../components/ImportButton.jsx', () => importButtonMock());
vi.mock('@fortawesome/react-fontawesome', () => fontAwesomeMock());

import { ApiServiceContext, machine } from '../../services/ApiService.js';
import { Settings } from './index.jsx';

function renderSettings() {
  return renderSettingsWith(ApiServiceContext, Settings);
}

beforeEach(() => {
  queryState.isLoading = false;
  queryState.data = fetchedSettings;
  authenticatedFetch.mockResolvedValue({ ok: true, json: async () => ({}) });
  installSettingsTestGlobals(machine);
});

afterEach(() => {
  teardownSettingsTest();
});

describe('Settings save payload survives card collapse (PRO-572 review blocker)', () => {
  it('includes a field edited inside a card that was collapsed before Save', async () => {
    renderSettings();

    // 1. Expand the Temperature Settings card.
    fireEvent.click(screen.getByRole('button', { name: 'Temperature Settings' }));

    // 2. Edit a field inside it. The input is a controlled component wired
    // with Preact `onChange`, which fires on the native `change` event — so we
    // dispatch `change` (not `input`) to drive the state update.
    const input = screen.getByLabelText('Default Steam Temperature');
    fireEvent.change(input, { target: { value: '142' } });

    // 3. Collapse the card again (aria-expanded flips to false).
    fireEvent.click(screen.getByRole('button', { name: 'Temperature Settings' }));
    expect(
      screen.getByRole('button', { name: 'Temperature Settings' }).getAttribute('aria-expanded'),
    ).toBe('false');

    // 4. Submit the form (Save).
    const form = document.querySelector('form[action="/api/settings"]');
    fireEvent.submit(form);

    // Wait a microtask for the async onSubmit to reach authenticatedFetch.
    await Promise.resolve();

    // 5. Assert the edited value made it into the submitted FormData body.
    expect(authenticatedFetch).toHaveBeenCalled();
    const [, options] = authenticatedFetch.mock.calls.at(-1);
    const body = options.body;
    expect(body).toBeInstanceOf(FormData);
    expect(body.get('targetSteamTemp')).toBe('142');
  });
});
