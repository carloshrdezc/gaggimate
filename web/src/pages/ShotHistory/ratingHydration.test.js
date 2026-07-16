import { describe, test, expect, vi } from 'vitest';

vi.mock('../ShotAnalyzer/services/IndexedDBService.js', () => ({
  indexedDBService: {
    getNotes: vi.fn(),
  },
}));

import { indexedDBService } from '../ShotAnalyzer/services/IndexedDBService.js';
import { notesService as realNotesService } from '../ShotAnalyzer/services/NotesService.js';
import { hydrateShotRatingsFromNotes } from './ratingHydration.js';

describe('hydrateShotRatingsFromNotes', () => {
  test('uses saved notes rating as authoritative before cards are expanded', async () => {
    const notesService = {
      loadNotes: vi.fn(async id => ({ id, rating: id === '101' ? 8.7 : 7.3, notes: '' })),
    };
    const shots = [
      { id: 101, source: 'gaggimate', rating: 8 },
      { id: 102, source: 'gaggimate', rating: 9 },
    ];

    const hydrated = await hydrateShotRatingsFromNotes(shots, notesService);

    expect(notesService.loadNotes).toHaveBeenCalledWith('101', 'gaggimate');
    expect(hydrated.map(shot => shot.rating)).toEqual([8.7, 7.3]);
    expect(hydrated[0].notes.rating).toBe(8.7);
  });

  test('hydrates only the visible page with bounded concurrency', async () => {
    let activeLoads = 0;
    let maxActiveLoads = 0;
    const notesService = {
      loadNotes: vi.fn(
        id =>
          new Promise(resolve => {
            activeLoads += 1;
            maxActiveLoads = Math.max(maxActiveLoads, activeLoads);
            setTimeout(() => {
              activeLoads -= 1;
              resolve({ id, rating: Number(id) + 0.5 });
            }, 0);
          }),
      ),
    };
    const shots = Array.from({ length: 20 }, (_, index) => ({
      id: index + 1,
      source: 'gaggimate',
      rating: index + 1,
    }));

    const hydrated = await hydrateShotRatingsFromNotes(shots, notesService, {
      limit: 10,
      concurrency: 3,
    });

    expect(notesService.loadNotes).toHaveBeenCalledTimes(10);
    expect(maxActiveLoads).toBeLessThanOrEqual(3);
    expect(hydrated).toHaveLength(10);
    expect(hydrated.at(-1).rating).toBe(10.5);
  });

  test('uses persisted browser notes over stale embedded imported-shot notes', async () => {
    const persistedNotes = { rating: 8.4, notes: 'edited notes-store value' };
    const notesService = {
      loadNotes: vi.fn(async () => persistedNotes),
    };

    const [hydrated] = await hydrateShotRatingsFromNotes(
      [
        {
          id: 'archive-row',
          source: 'browser',
          storageKey: 'indexed-db-key',
          rating: 4.2,
          notes: { rating: 4.2, notes: 'stale embedded archive value' },
        },
      ],
      notesService,
    );

    expect(notesService.loadNotes).toHaveBeenCalledWith('indexed-db-key', 'browser');
    expect(hydrated.rating).toBe(8.4);
    expect(hydrated.notes).toBe(persistedNotes);
  });

  test('hydrates duplicate imported browser ids by their notes persistence key', async () => {
    const notesService = {
      loadNotes: vi.fn(async id => ({
        id,
        rating: id === 'browser-key-a' ? 4.1 : 8.2,
        beanType: id === 'browser-key-a' ? 'Bean A' : 'Bean B',
        notes: '',
      })),
    };

    const hydrated = await hydrateShotRatingsFromNotes(
      [
        { id: 'shared-import-id', source: 'browser', storageKey: 'browser-key-a', rating: 0 },
        { id: 'shared-import-id', source: 'browser', storageKey: 'browser-key-b', rating: 0 },
      ],
      notesService,
    );

    expect(notesService.loadNotes).toHaveBeenCalledWith('browser-key-a', 'browser');
    expect(notesService.loadNotes).toHaveBeenCalledWith('browser-key-b', 'browser');
    expect(hydrated.map(shot => shot.rating)).toEqual([4.1, 8.2]);
    expect(hydrated.map(shot => shot.notes.beanType)).toEqual(['Bean A', 'Bean B']);
  });

  test('keeps embedded browser notes when the notes store has no saved values', async () => {
    const embeddedNotes = { rating: 7.1, notes: 'archive notes only' };
    const notesService = {
      loadNotes: vi.fn(async id => ({ id, rating: 0, notes: '' })),
    };

    const [hydrated] = await hydrateShotRatingsFromNotes(
      [
        {
          id: 'archive-row',
          source: 'browser',
          storageKey: 'indexed-db-key',
          rating: 7.1,
          notes: embeddedNotes,
        },
      ],
      notesService,
    );

    expect(notesService.loadNotes).toHaveBeenCalledWith('indexed-db-key', 'browser');
    expect(hydrated.rating).toBe(7.1);
    expect(hydrated.notes).toBe(embeddedNotes);
  });

  test('uses persisted browser notes even when they look like cleared defaults', async () => {
    indexedDBService.getNotes.mockResolvedValueOnce({
      id: 'indexed-db-key',
      rating: 0,
      beanId: '',
      beanType: '',
      doseIn: '',
      doseOut: '',
      ratio: '',
      grinder: '',
      grindSetting: '',
      balanceTaste: 'balanced',
      notes: '',
    });

    const [hydrated] = await hydrateShotRatingsFromNotes(
      [
        {
          id: 'archive-row',
          source: 'browser',
          storageKey: 'indexed-db-key',
          rating: 7.1,
          notes: { rating: 7.1, notes: 'stale embedded archive value' },
        },
      ],
      realNotesService,
    );

    expect(indexedDBService.getNotes).toHaveBeenCalledWith('indexed-db-key');
    expect(hydrated.rating).toBe(0);
    expect(hydrated.notes).toMatchObject({
      id: 'indexed-db-key',
      rating: 0,
      notes: '',
      balanceTaste: 'balanced',
    });
    expect(Object.keys(hydrated.notes)).not.toContain('__shotHistoryPersistedNotes');
  });

  test('preserves existing ratings when the notes store has no saved values', async () => {
    const notesService = {
      loadNotes: vi.fn(async id => ({ id, rating: 0, notes: '' })),
    };

    const hydrated = await hydrateShotRatingsFromNotes(
      [
        { id: 101, source: 'gaggimate', rating: 8 },
        { id: 'archive-row', source: 'browser', storageKey: 'indexed-db-key', rating: 6.5 },
      ],
      notesService,
    );

    expect(notesService.loadNotes).toHaveBeenCalledWith('101', 'gaggimate');
    expect(notesService.loadNotes).toHaveBeenCalledWith('indexed-db-key', 'browser');
    expect(hydrated.map(shot => shot.rating)).toEqual([8, 6.5]);
    expect(hydrated[0].notes).toBeUndefined();
    expect(hydrated[1].notes).toBeUndefined();
  });
});
