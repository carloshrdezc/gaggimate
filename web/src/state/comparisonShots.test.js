// PRO-468 — unit tests for the comparisonShots.js signal state module
// introduced in PRO-30 (#452).
//
// comparisonShots is a module-level signal, so it must be reset between
// tests via vi.resetModules() + a fresh import (pattern from
// ApiService.reconnect.test.js) to avoid state bleeding across tests.

import { beforeEach, describe, expect, it, vi } from 'vitest';

let comparisonShots;
let addToComparison;
let removeFromComparison;
let clearComparison;
let isInComparison;

beforeEach(async () => {
  vi.resetModules();
  const mod = await import('./comparisonShots.js');
  comparisonShots = mod.comparisonShots;
  addToComparison = mod.addToComparison;
  removeFromComparison = mod.removeFromComparison;
  clearComparison = mod.clearComparison;
  isInComparison = mod.isInComparison;
});

function makeShot(overrides = {}) {
  return { id: 'shot-1', name: 'Shot 1', ...overrides };
}

describe('comparisonShots', () => {
  it('starts empty', () => {
    expect(comparisonShots.value).toEqual([]);
  });

  describe('addToComparison', () => {
    it('adds a shot', () => {
      const shot = makeShot();
      addToComparison(shot);
      expect(comparisonShots.value).toEqual([shot]);
    });

    it('dedup guard: adding the same shot ID twice keeps only one', () => {
      const shot = makeShot({ id: 'dup-1' });
      addToComparison(shot);
      addToComparison({ ...shot, name: 'Different name, same id' });

      expect(comparisonShots.value.length).toBe(1);
      expect(comparisonShots.value[0].id).toBe('dup-1');
      // The original entry is kept as-is; the duplicate add was a no-op.
      expect(comparisonShots.value[0].name).toBe('Shot 1');
    });

    it('max-4 cap: a 5th shot is silently ignored', () => {
      addToComparison(makeShot({ id: 's1' }));
      addToComparison(makeShot({ id: 's2' }));
      addToComparison(makeShot({ id: 's3' }));
      addToComparison(makeShot({ id: 's4' }));
      addToComparison(makeShot({ id: 's5' }));

      expect(comparisonShots.value.length).toBe(4);
      expect(comparisonShots.value.map(s => s.id)).toEqual(['s1', 's2', 's3', 's4']);
    });

    it('dedup is a no-op even when at capacity', () => {
      addToComparison(makeShot({ id: 's1' }));
      addToComparison(makeShot({ id: 's2' }));
      addToComparison(makeShot({ id: 's3' }));
      addToComparison(makeShot({ id: 's4' }));
      addToComparison(makeShot({ id: 's1' })); // duplicate while at cap

      expect(comparisonShots.value.length).toBe(4);
    });
  });

  describe('removeFromComparison', () => {
    it('removes the correct shot by ID', () => {
      addToComparison(makeShot({ id: 's1' }));
      addToComparison(makeShot({ id: 's2' }));
      addToComparison(makeShot({ id: 's3' }));

      removeFromComparison('s2');

      expect(comparisonShots.value.map(s => s.id)).toEqual(['s1', 's3']);
    });

    it('is a no-op when the ID is not present', () => {
      addToComparison(makeShot({ id: 's1' }));
      removeFromComparison('does-not-exist');
      expect(comparisonShots.value.map(s => s.id)).toEqual(['s1']);
    });
  });

  describe('clearComparison', () => {
    it('empties the array', () => {
      addToComparison(makeShot({ id: 's1' }));
      addToComparison(makeShot({ id: 's2' }));

      clearComparison();

      expect(comparisonShots.value).toEqual([]);
    });
  });

  describe('isInComparison', () => {
    it('returns true when the shot is in the set', () => {
      addToComparison(makeShot({ id: 's1' }));
      expect(isInComparison('s1')).toBe(true);
    });

    it('returns false when the shot is not in the set', () => {
      addToComparison(makeShot({ id: 's1' }));
      expect(isInComparison('not-there')).toBe(false);
    });
  });
});
