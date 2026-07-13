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

import { importShotHistoryArchive, buildShotHistoryArchive } from './historyArchive.js';

describe('buildShotHistoryArchive — id serialization', () => {
  test('serializes id=0 as "0", not as ""', () => {
    const archive = buildShotHistoryArchive([{ id: 0, timestamp: 1000, profile: 'Test', samples: [] }]);
    expect(archive.shots[0].id).toBe('0');
  });

  test('serializes positive id correctly', () => {
    const archive = buildShotHistoryArchive([{ id: 42, timestamp: 1000, profile: 'Test', samples: [] }]);
    expect(archive.shots[0].id).toBe('42');
  });

  test('serializes null id as ""', () => {
    const archive = buildShotHistoryArchive([{ id: null, timestamp: 1000, profile: 'Test', samples: [] }]);
    expect(archive.shots[0].id).toBe('');
  });
});

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

  test('accepts shot with id=0 and round-trips shotId as "0" (not replaced by timestamp)', async () => {
    const shot = { id: 0, timestamp: 1000, profile: 'Test' };
    const result = await importShotHistoryArchive([shot]);
    expect(result).toHaveLength(1);
    expect(result[0].id).toBe('0');
  });

  test('accepts shot with timestamp=0 and non-zero id (hasCoreHistoryFields still passes)', async () => {
    const shot = { id: 5, timestamp: 0, profile: 'Test' };
    const result = await importShotHistoryArchive([shot]);
    expect(result).toHaveLength(1);
    expect(result[0].id).toBe('5');
  });

  test('accepts shot with id=0 AND timestamp=0 (profile set) — shotId uses id "0"', async () => {
    const shot = { id: 0, timestamp: 0, profile: 'Test' };
    const result = await importShotHistoryArchive([shot]);
    expect(result).toHaveLength(1);
    expect(result[0].id).toBe('0');
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
