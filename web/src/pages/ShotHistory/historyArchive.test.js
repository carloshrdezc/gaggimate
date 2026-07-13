import { describe, test, expect, vi, beforeEach } from 'vitest';

// Mock IndexedDB and NotesService so importShotHistoryArchive runs synchronously
vi.mock('../ShotAnalyzer/services/IndexedDBService', () => ({
  indexedDBService: {
    getShot: vi.fn().mockResolvedValue(null),
    saveShot: vi.fn().mockResolvedValue(undefined),
  },
}));

vi.mock('../ShotAnalyzer/services/NotesService', () => ({
  notesService: {
    getDefaults: vi.fn().mockReturnValue({}),
    saveNotes: vi.fn().mockResolvedValue(undefined),
  },
}));

import { importShotHistoryArchive } from './historyArchive.js';

describe('importShotHistoryArchive — hasCoreHistoryFields guard', () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  test('rejects shot with no samples and missing all core fields', async () => {
    await expect(importShotHistoryArchive([{}])).rejects.toThrow(
      'did not contain any valid shot history entries',
    );
  });

  test('rejects shot where timestamp is undefined (no samples)', async () => {
    const shot = { id: 1, profile: 'Test' }; // timestamp absent
    await expect(importShotHistoryArchive([shot])).rejects.toThrow(
      'did not contain any valid shot history entries',
    );
  });

  test('accepts shot with timestamp=0 (0 != null — not falsy-rejected)', async () => {
    const shot = { id: 1, timestamp: 0, profile: 'Test' };
    const result = await importShotHistoryArchive([shot]);
    expect(result).toHaveLength(1);
    // id=1 is truthy, timestamp=0 is != null, profile is != null
    expect(result[0].id).toBe('1');
  });

  test('accepts shot with id=0 (0 != null)', async () => {
    const shot = { id: 0, timestamp: 1000, profile: 'Test' };
    const result = await importShotHistoryArchive([shot]);
    expect(result).toHaveLength(1);
  });

  test('accepts shot with only samples (no core fields required)', async () => {
    const shot = { samples: [{ cp: 9, ct: 93 }] };
    const result = await importShotHistoryArchive([shot]);
    expect(result).toHaveLength(1);
  });

  test('throws if empty array payload', async () => {
    await expect(importShotHistoryArchive([])).rejects.toThrow('No shots found');
  });
});
