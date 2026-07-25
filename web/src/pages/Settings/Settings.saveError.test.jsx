import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { h } from 'preact';
import { fireEvent, waitFor } from '@testing-library/preact';

import {
  fontAwesomeMock,
  importButtonMock,
  installSettingsTestGlobals,
  localAuthFetchMock,
  renderSettings as renderSettingsWith,
  teardownSettingsTest,
} from './Settings.testUtils.jsx';

// Regression test for the PR #565 review nit (PRO-578, ref PRO-577): the
// Settings onSubmit handler parses a 400 validation rejection of the shape
// {error, field, detail} and surfaces the offending field/detail in an alert
// (`Failed to save settings: ${field}: ${detail}`), instead of the generic
// fallback. This suite exercises that branch plus its fallbacks: a 400 whose
// body is not a validation shape, a malformed (non-JSON) 400 body, and a
// non-400 server error — all of which must fall through to the generic
// "Failed to save settings. Please try again." alert.

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

// Submit the Save form and wait for the async onSubmit handler to settle.
// onSubmit does an awaited fetch, then (on the 400 branch) an awaited
// response.clone().json(); every error path this suite exercises ends in an
// alert() call, so we wait on alert having been called rather than draining a
// fixed number of microtask turns.
async function submitAndSettle() {
  const form = document.querySelector('form[action="/api/settings"]');
  fireEvent.submit(form);
  await waitFor(() => expect(globalThis.alert).toHaveBeenCalled());
}

beforeEach(() => {
  queryState.isLoading = false;
  queryState.data = fetchedSettings;
  installSettingsTestGlobals(machine);
});

afterEach(() => {
  teardownSettingsTest();
});

describe('Settings save error handling (PRO-578 / ref PRO-577)', () => {
  it('surfaces the field/detail from a 400 validation rejection body', async () => {
    const validationBody = {
      error: 'Validation failed',
      field: 'pressureScaling',
      detail: 'must be between 0 and 30',
    };
    // response.clone() is called before .json() in the source, so clone() must
    // return an object exposing json().
    const jsonResponse = { json: async () => validationBody };
    authenticatedFetch.mockResolvedValue({
      ok: false,
      status: 400,
      clone: () => jsonResponse,
      json: async () => validationBody,
    });

    renderSettings();
    await submitAndSettle();

    expect(authenticatedFetch).toHaveBeenCalled();
    expect(globalThis.alert).toHaveBeenCalledTimes(1);
    // Matches the source: `Failed to save settings: ${field}: ${detail}`,
    // where the Error message is `${field}: ${detail}`.
    expect(globalThis.alert).toHaveBeenCalledWith(
      'Failed to save settings: pressureScaling: must be between 0 and 30',
    );
  });

  it('falls back to the generic alert when a 400 body lacks field/detail', async () => {
    const nonValidationBody = { error: 'Bad request' };
    authenticatedFetch.mockResolvedValue({
      ok: false,
      status: 400,
      clone: () => ({ json: async () => nonValidationBody }),
      json: async () => nonValidationBody,
    });

    renderSettings();
    await submitAndSettle();

    expect(globalThis.alert).toHaveBeenCalledTimes(1);
    expect(globalThis.alert).toHaveBeenCalledWith('Failed to save settings. Please try again.');
  });

  it('falls back to the generic alert when a 400 body is not valid JSON', async () => {
    authenticatedFetch.mockResolvedValue({
      ok: false,
      status: 400,
      clone: () => ({
        json: async () => {
          throw new SyntaxError('Unexpected token < in JSON');
        },
      }),
      json: async () => {
        throw new SyntaxError('Unexpected token < in JSON');
      },
    });

    renderSettings();
    await submitAndSettle();

    expect(globalThis.alert).toHaveBeenCalledTimes(1);
    expect(globalThis.alert).toHaveBeenCalledWith('Failed to save settings. Please try again.');
  });

  it('falls back to the generic alert on a non-400 server error', async () => {
    authenticatedFetch.mockResolvedValue({
      ok: false,
      status: 500,
      clone: () => ({ json: async () => ({}) }),
      json: async () => ({}),
    });

    renderSettings();
    await submitAndSettle();

    expect(globalThis.alert).toHaveBeenCalledTimes(1);
    expect(globalThis.alert).toHaveBeenCalledWith('Failed to save settings. Please try again.');
  });
});
