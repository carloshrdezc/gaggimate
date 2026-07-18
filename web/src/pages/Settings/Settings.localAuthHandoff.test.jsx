import { afterEach, beforeEach, expect, test, vi } from 'vitest';
import { h } from 'preact';
import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/preact';

const { authenticatedFetch, localAuthHandoffUrl, fetchedSettings } = vi.hoisted(() => ({
  authenticatedFetch: vi.fn(),
  localAuthHandoffUrl: vi.fn(hostname => `http://${hostname}.local/#localAuthToken=bootstrap-token`),
  fetchedSettings: {
    pid: '3.000,0.100,40.000,0.000',
    autowakeupSchedules: '07:00|1111111',
    standbyBrightness: 8,
    wifiSsid: 'setup-network',
    wifiPassword: '',
    mdnsName: 'gaggimate',
    localAdminToken: 'bootstrap-token',
  },
}));

vi.mock('preact-fetching', () => ({
  useQuery: () => ({
    isLoading: false,
    data: fetchedSettings,
  }),
}));

vi.mock('../../services/localAuthFetch.js', () => ({
  authenticatedFetch,
  bootstrapLocalAuth: vi.fn(),
  localAuthDownloadUrl: url => url,
  localAuthHandoffUrl,
  MDNS_NAME_ERROR: 'Hostname must be 1-63 ASCII letters, digits, or hyphens, without a leading or trailing hyphen.',
}));

vi.mock('../../components/Card.jsx', () => ({ default: ({ children }) => h('section', {}, children) }));
vi.mock('./PluginCard.jsx', () => ({ PluginCard: () => null }));
vi.mock('./GoogleDriveBackupCard.jsx', () => ({ GoogleDriveBackupCard: () => null }));
vi.mock('../../components/ImportButton.jsx', () => ({ ImportButton: () => null }));
vi.mock('@fortawesome/react-fontawesome', () => ({ FontAwesomeIcon: () => null }));

import { ApiServiceContext } from '../../services/ApiService.js';
import { Settings } from './index.jsx';

beforeEach(() => {
  authenticatedFetch.mockResolvedValue({ ok: true, json: async () => ({}) });
  vi.stubGlobal('alert', vi.fn());
  vi.stubGlobal('navigator', { clipboard: { writeText: vi.fn() } });
});

afterEach(() => {
  cleanup();
  vi.restoreAllMocks();
  vi.unstubAllGlobals();
});

test.each(['attacker.example/', 'attacker?example', 'attacker#example', 'attacker@example', 'attacker example'])(
  'rejects unsafe hostname %s before provisioning or copying a handoff URL', async hostname => {
    localAuthHandoffUrl.mockReturnValueOnce(null);
    render(
      h(ApiServiceContext.Provider, { value: { authenticateLocal: vi.fn() } }, h(Settings)),
    );

    fireEvent.change(screen.getByLabelText('Hostname'), { target: { value: hostname } });
    fireEvent.click(screen.getByRole('button', { name: 'Copy Wi-Fi auth handoff link' }));

    expect(authenticatedFetch).not.toHaveBeenCalled();
    expect(navigator.clipboard.writeText).not.toHaveBeenCalled();
    expect(screen.getByText('Hostname must be 1-63 ASCII letters, digits, or hyphens, without a leading or trailing hyphen.')).toBeTruthy();
  },
);

test('persists the changed hostname with AP-to-STA credentials and hands off to that hostname', async () => {
  render(
    h(ApiServiceContext.Provider, { value: { authenticateLocal: vi.fn() } }, h(Settings)),
  );

  const wifiPassword = screen.getByLabelText('Wi-Fi Password');
  fireEvent.change(wifiPassword, { target: { value: 'new-password' } });
  await waitFor(() => expect(wifiPassword.value).toBe('new-password'));
  const hostname = screen.getByLabelText('Hostname');
  fireEvent.change(hostname, { target: { value: 'new-gaggimate' } });
  await waitFor(() => expect(hostname.value).toBe('new-gaggimate'));
  fireEvent.click(screen.getByRole('button', { name: 'Copy Wi-Fi auth handoff link' }));

  await waitFor(() => expect(authenticatedFetch).toHaveBeenCalledTimes(1));

  const [, options] = authenticatedFetch.mock.calls[0];
  expect(options.method).toBe('post');
  expect(Object.fromEntries(options.body.entries())).toEqual({
    wifiSsid: 'setup-network',
    wifiPassword: 'new-password',
    mdnsName: 'new-gaggimate',
    completeLocalAuthProvisioning: '1',
    restart: '1',
  });
  expect(navigator.clipboard.writeText).toHaveBeenCalledWith(
    'http://new-gaggimate.local/#localAuthToken=bootstrap-token',
  );
  expect(screen.getByText('http://new-gaggimate.local/#localAuthToken=bootstrap-token')).toBeTruthy();
});
