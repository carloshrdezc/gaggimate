import { afterEach, describe, expect, it, vi } from 'vitest';
import {
  authenticatedFetch,
  getLocalAuthToken,
  importLocalAuthHandoff,
  isValidLocalAuthToken,
  localAuthDownloadUrl,
  localAuthHandoffUrl,
} from './localAuthFetch.js';

const TOKEN_KEY = 'gaggimate_local_admin_token';

afterEach(() => {
  localStorage.clear();
  vi.restoreAllMocks();
});

describe('authenticatedFetch', () => {
  it('adds the stored bearer token without dropping caller headers', async () => {
    localStorage.setItem(TOKEN_KEY, 'unit-token');
    const fetchMock = vi.fn().mockResolvedValue({ ok: true });
    vi.stubGlobal('fetch', fetchMock);

    await authenticatedFetch('/api/scales/list', { headers: { Accept: 'application/json' } });

    expect(fetchMock).toHaveBeenCalledWith('/api/scales/list', {
      headers: { Accept: 'application/json', Authorization: 'Bearer unit-token' },
    });
  });

  it('preserves AP bootstrap requests when no token is stored', async () => {
    const fetchMock = vi.fn().mockResolvedValue({ ok: true });
    vi.stubGlobal('fetch', fetchMock);

    await authenticatedFetch('/api/settings');

    expect(fetchMock).toHaveBeenCalledWith('/api/settings', { headers: {} });
  });
});

describe('isValidLocalAuthToken', () => {
  it('accepts a 32-char lowercase hex token (matches the firmware %08lx x4 mint format)', () => {
    expect(isValidLocalAuthToken('a1b2c3d4e5f60718293a4b5c6d7e8f90')).toBe(true);
  });

  it('trims surrounding whitespace before validating a pasted token', () => {
    expect(isValidLocalAuthToken('  a1b2c3d4e5f60718293a4b5c6d7e8f90  ')).toBe(true);
  });

  it.each([
    ['too short', 'a1b2c3d4'],
    ['too long', 'a1b2c3d4e5f60718293a4b5c6d7e8f900'],
    ['uppercase hex', 'A1B2C3D4E5F60718293A4B5C6D7E8F90'],
    ['non-hex chars', 'z1b2c3d4e5f60718293a4b5c6d7e8f90'],
    ['empty', ''],
  ])('rejects an invalid token (%s)', (_desc, token) => {
    expect(isValidLocalAuthToken(token)).toBe(false);
  });

  it('rejects non-string input without throwing', () => {
    expect(isValidLocalAuthToken(null)).toBe(false);
    expect(isValidLocalAuthToken(undefined)).toBe(false);
    expect(isValidLocalAuthToken(1234)).toBe(false);
  });
});

describe('getLocalAuthToken', () => {
  it('returns the stored token', () => {
    localStorage.setItem(TOKEN_KEY, 'stored-token');
    expect(getLocalAuthToken()).toBe('stored-token');
  });

  it('returns null when no token is stored', () => {
    expect(getLocalAuthToken()).toBeNull();
  });
});

describe('localAuthDownloadUrl', () => {
  it('adds the stored token as the firmware download-link query parameter', () => {
    localStorage.setItem(TOKEN_KEY, 'unit-token');

    expect(localAuthDownloadUrl('/api/diag/log.txt')).toBe(
      '/api/diag/log.txt?localAuthToken=unit-token',
    );
  });

  it('does not add a query credential to non-download routes', () => {
    localStorage.setItem(TOKEN_KEY, 'unit-token');

    expect(localAuthDownloadUrl('/api/settings')).toBe('/api/settings');
    expect(localAuthDownloadUrl('/api/scales/list')).toBe('/api/scales/list');
  });

  it('leaves bootstrap/download URLs unchanged without a stored token', () => {
    expect(localAuthDownloadUrl('/api/diag/log.txt')).toBe('/api/diag/log.txt');
  });
});

describe('localAuthHandoff', () => {
  it('uses an mDNS URL fragment so the AP-to-STA credential is never sent in an HTTP request', () => {
    localStorage.setItem(TOKEN_KEY, 'unit-token');

    expect(localAuthHandoffUrl('gaggimate')).toBe(
      'http://gaggimate.local/#localAuthToken=unit-token',
    );
  });

  it.each([
    'attacker.example/',
    'attacker?example',
    'attacker#example',
    'attacker@example',
    'attacker example',
    '-attacker',
    'attacker-',
  ])('rejects unsafe hostname %s instead of producing a handoff URL', hostname => {
    localStorage.setItem(TOKEN_KEY, 'unit-token');

    expect(localAuthHandoffUrl(hostname)).toBeNull();
  });

  it('uses the parsed validated hostname as the exact STA handoff origin', () => {
    localStorage.setItem(TOKEN_KEY, 'unit-token');

    const handoff = new URL(localAuthHandoffUrl('new-gaggimate'));

    expect(handoff.origin).toBe('http://new-gaggimate.local');
    expect(handoff.hostname).toBe('new-gaggimate.local');
    expect(handoff.hash).toBe('#localAuthToken=unit-token');
  });

  it('canonicalizes a valid mixed-case label before asserting its parsed STA origin', () => {
    localStorage.setItem(TOKEN_KEY, 'unit-token');

    const handoff = new URL(localAuthHandoffUrl('New-GaggiMate'));

    expect(handoff.origin).toBe('http://new-gaggimate.local');
    expect(handoff.hostname).toBe('new-gaggimate.local');
  });

  it('imports a handoff token into the STA origin, authenticates the socket, and clears the fragment', () => {
    const apiService = { authenticateLocal: vi.fn() };
    const replaceState = vi.spyOn(history, 'replaceState');

    expect(
      importLocalAuthHandoff(apiService, {
        hash: '#localAuthToken=sta-token',
        pathname: '/',
        search: '',
      }),
    ).toBe(true);
    expect(localStorage.getItem(TOKEN_KEY)).toBe('sta-token');
    expect(apiService.authenticateLocal).toHaveBeenCalledWith('sta-token');
    expect(replaceState).toHaveBeenCalledWith(null, '', '/');
  });

  it('does not alter storage when no handoff fragment is present', () => {
    const apiService = { authenticateLocal: vi.fn() };

    expect(importLocalAuthHandoff(apiService, { hash: '', pathname: '/', search: '' })).toBe(false);
    expect(apiService.authenticateLocal).not.toHaveBeenCalled();
  });
});
