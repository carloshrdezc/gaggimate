import { afterEach, describe, expect, it, vi } from 'vitest';
import { authenticatedFetch, localAuthDownloadUrl } from './localAuthFetch.js';

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

describe('localAuthDownloadUrl', () => {
  it('adds the stored token as the firmware download-link query parameter', () => {
    localStorage.setItem(TOKEN_KEY, 'unit-token');

    expect(localAuthDownloadUrl('/api/diag/log.txt')).toBe('/api/diag/log.txt?localAuthToken=unit-token');
  });

  it('leaves bootstrap/download URLs unchanged without a stored token', () => {
    expect(localAuthDownloadUrl('/api/diag/log.txt')).toBe('/api/diag/log.txt');
  });
});
