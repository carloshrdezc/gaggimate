import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';

// Everything the bundle touches other than themeManager is stubbed: this suite
// exists to pin the PRO-643 non-goal that custom accents stay browser-local and
// never enter the backup contract, so the real themeManager is used on purpose.
vi.mock('../services/localAuthFetch.js', () => ({
  authenticatedFetch: vi.fn(async () => ({
    ok: true,
    json: async () => ({}),
    arrayBuffer: async () => new ArrayBuffer(0),
  })),
}));
vi.mock('../pages/ShotHistory/parseBinaryIndex.js', () => ({
  parseBinaryIndex: () => ({}),
  indexToShotList: () => [],
}));
vi.mock('../pages/ShotHistory/parseBinaryShot.js', () => ({ parseBinaryShot: () => ({}) }));
vi.mock('../pages/ShotHistory/historyArchive.js', () => ({
  buildShotHistoryArchive: async () => ({ shots: [] }),
  importShotHistoryArchive: vi.fn(),
}));
vi.mock('../pages/ShotAnalyzer/services/IndexedDBService.js', () => ({
  indexedDBService: { init: vi.fn(), getAllShots: async () => [] },
}));
vi.mock('../pages/ShotAnalyzer/services/NotesService.js', () => ({
  notesService: { setApiService: vi.fn(), loadNotes: async () => null },
}));
vi.mock('./beanManager.js', () => ({
  exportBeanData: async () => ({ beans: [] }),
  getCurrentBeanSelection: () => ({ beanName: '' }),
  restoreBeanData: vi.fn(),
}));
vi.mock('./googleDriveBackup.js', () => ({
  getStoredGoogleDriveClientId: () => '',
  setStoredGoogleDriveClientId: vi.fn(),
}));

import { createBackupBundle } from './backupBundle.js';
import { setAppAccent, setDashboardAccent, setStoredTheme } from './themeManager.js';

const apiService = {
  request: vi.fn(async () => ({ profiles: [] })),
  send: vi.fn(),
};

beforeEach(() => {
  localStorage.clear();
  document.documentElement.removeAttribute('style');
  vi.spyOn(globalThis, 'fetch').mockResolvedValue({ ok: true, json: async () => ({}) });
});

afterEach(() => {
  vi.restoreAllMocks();
});

describe('backup bundle accent non-goal (PRO-643)', () => {
  it('exports the named theme string only, never the custom accents', async () => {
    setStoredTheme('espresso');
    setAppAccent('#ff6600');
    setDashboardAccent('#33cc99');

    const bundle = await createBackupBundle(apiService);

    expect(bundle.web).toMatchObject({ theme: 'espresso' });
    expect(bundle.web).not.toHaveProperty('appAccent');
    expect(bundle.web).not.toHaveProperty('dashboardAccent');
    // The theme field must stay a bare string, not the versioned object.
    expect(typeof bundle.web.theme).toBe('string');
  });

  it('still exports a valid theme string when no preference was ever stored', async () => {
    const bundle = await createBackupBundle(apiService);
    expect(bundle.web.theme).toBe('midnight');
  });
});
