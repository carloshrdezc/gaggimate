import { describe, test, expect, vi } from 'vitest';

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

  test('uses browser shot storage keys when hydrating imported shots', async () => {
    const notesService = {
      loadNotes: vi.fn(async () => ({ rating: 6.8 })),
    };

    const [hydrated] = await hydrateShotRatingsFromNotes(
      [{ id: 'archive-row', source: 'browser', storageKey: 'indexed-db-key', rating: 6 }],
      notesService,
    );

    expect(notesService.loadNotes).toHaveBeenCalledWith('indexed-db-key', 'browser');
    expect(hydrated.rating).toBe(6.8);
  });
});
