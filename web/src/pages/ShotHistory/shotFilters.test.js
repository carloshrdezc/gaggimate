import { describe, test, expect } from 'vitest';

import {
  DEFAULT_FILTERS,
  defaultFilters,
  isDefaultFilters,
  hasActiveFilters,
  applyShotFilters,
  availableProfiles,
  availableBeans,
  filtersFromQuery,
  filtersToSearchParams,
  filtersToQueryString,
  mergeFiltersIntoSearch,
} from './shotFilters.js';

// Build a Unix-seconds timestamp for a given local date.
function ts(y, m, d, hh = 12) {
  return Math.floor(new Date(y, m - 1, d, hh).getTime() / 1000);
}

const SHOTS = [
  {
    id: 1,
    profile: 'Espresso',
    beanId: 'bean-a',
    beanName: 'Ethiopia',
    timestamp: ts(2026, 1, 10),
    duration: 25000, // 25s
    notes: { notes: 'bright and fruity' },
  },
  {
    id: 2,
    profile: 'Turbo',
    beanId: 'bean-b',
    beanName: 'Colombia',
    timestamp: ts(2026, 2, 15),
    duration: 18000, // 18s
    notes: 'sour shot',
  },
  {
    id: 3,
    profile: 'Espresso',
    beanId: 'bean-a',
    beanName: 'Ethiopia',
    timestamp: ts(2026, 3, 20),
    duration: 40000, // 40s
    notes: { notes: 'over-extracted, bitter' },
  },
  {
    id: 4,
    profile: 'Lungo',
    beanId: '',
    beanName: '',
    timestamp: ts(2026, 3, 25),
    duration: 55000, // 55s
    // no notes
  },
];

describe('defaults', () => {
  test('defaultFilters returns a fresh copy of the defaults', () => {
    const a = defaultFilters();
    const b = defaultFilters();
    expect(a).toEqual(DEFAULT_FILTERS);
    expect(a).not.toBe(b);
    a.text = 'x';
    expect(b.text).toBe('');
  });

  test('isDefaultFilters / hasActiveFilters', () => {
    expect(isDefaultFilters(defaultFilters())).toBe(true);
    expect(hasActiveFilters(defaultFilters())).toBe(false);
    expect(hasActiveFilters({ ...defaultFilters(), profile: 'Espresso' })).toBe(true);
  });
});

describe('applyShotFilters', () => {
  test('no filters returns all shots (new array, no mutation)', () => {
    const result = applyShotFilters(SHOTS, defaultFilters());
    expect(result).toHaveLength(4);
    expect(result).not.toBe(SHOTS);
  });

  test('date range is inclusive on both ends', () => {
    const result = applyShotFilters(SHOTS, {
      ...defaultFilters(),
      dateFrom: '2026-02-15',
      dateTo: '2026-03-20',
    });
    expect(result.map(s => s.id)).toEqual([2, 3]);
  });

  test('dateFrom only (open upper bound)', () => {
    const result = applyShotFilters(SHOTS, { ...defaultFilters(), dateFrom: '2026-03-01' });
    expect(result.map(s => s.id)).toEqual([3, 4]);
  });

  test('profile exact match', () => {
    const result = applyShotFilters(SHOTS, { ...defaultFilters(), profile: 'Espresso' });
    expect(result.map(s => s.id)).toEqual([1, 3]);
  });

  test('bean id exact match', () => {
    const result = applyShotFilters(SHOTS, { ...defaultFilters(), beanId: 'bean-b' });
    expect(result.map(s => s.id)).toEqual([2]);
  });

  test('duration range in seconds (against ms-stored duration)', () => {
    const result = applyShotFilters(SHOTS, {
      ...defaultFilters(),
      durationMin: '20',
      durationMax: '45',
    });
    expect(result.map(s => s.id)).toEqual([1, 3]);
  });

  test('free text searches notes (object and string forms)', () => {
    expect(applyShotFilters(SHOTS, { ...defaultFilters(), text: 'bitter' }).map(s => s.id)).toEqual(
      [3],
    );
    expect(applyShotFilters(SHOTS, { ...defaultFilters(), text: 'sour' }).map(s => s.id)).toEqual([
      2,
    ]);
    // case-insensitive
    expect(applyShotFilters(SHOTS, { ...defaultFilters(), text: 'FRUITY' }).map(s => s.id)).toEqual(
      [1],
    );
    // shot without notes never matches a text filter
    expect(applyShotFilters(SHOTS, { ...defaultFilters(), text: 'lungo' }).map(s => s.id)).toEqual(
      [],
    );
  });

  test('filters compose as AND', () => {
    const result = applyShotFilters(SHOTS, {
      ...defaultFilters(),
      profile: 'Espresso',
      durationMin: '30',
    });
    expect(result.map(s => s.id)).toEqual([3]);
  });

  test('handles null / empty input safely', () => {
    expect(applyShotFilters(null, defaultFilters())).toEqual([]);
    expect(applyShotFilters(undefined, undefined)).toEqual([]);
  });
});

describe('availableProfiles / availableBeans', () => {
  test('availableProfiles returns distinct, sorted names', () => {
    expect(availableProfiles(SHOTS)).toEqual(['Espresso', 'Lungo', 'Turbo']);
  });

  test('availableBeans dedupes by id, prefers library name, sorts by name', () => {
    const beans = [{ id: 'bean-a', name: 'Ethiopia Yirgacheffe' }];
    const result = availableBeans(SHOTS, beans);
    expect(result).toEqual(
      [
        { id: 'bean-a', name: 'Ethiopia Yirgacheffe' },
        { id: 'bean-b', name: 'Colombia' },
      ].sort((x, y) => x.name.localeCompare(y.name)),
    );
    // bean-a picks the library name, bean-b falls back to shot.beanName
    expect(result.find(b => b.id === 'bean-a').name).toBe('Ethiopia Yirgacheffe');
    expect(result.find(b => b.id === 'bean-b').name).toBe('Colombia');
  });

  test('availableBeans uses "Unknown bean" fallback when name is blank, not the raw id (PRO-407)', () => {
    const shots = [
      // beanId present, but not in the library AND shot.beanName is blank.
      { id: 10, beanId: 'bean-x', beanName: '' },
      // beanId in the library, but the library entry's name is blank/whitespace.
      { id: 11, beanId: 'bean-y', beanName: '   ' },
    ];
    const beans = [{ id: 'bean-y', name: '  ' }];
    const result = availableBeans(shots, beans);

    // The display label must be the friendly fallback family, never the raw id.
    // Distinct blank-name beans are disambiguated with a numeric suffix (PRO-410).
    for (const opt of result) {
      expect(opt.name).toMatch(/^Unknown bean( \(\d+\))?$/);
      expect(opt.name).not.toBe(opt.id);
    }
    // The id field is preserved so the dropdown option still filters correctly.
    expect(result.map(b => b.id).sort()).toEqual(['bean-x', 'bean-y']);
  });

  test('availableBeans disambiguates multiple blank-name beans with numeric suffixes (PRO-410)', () => {
    const shots = [
      { id: 20, beanId: 'bean-1', beanName: '' },
      { id: 21, beanId: 'bean-2', beanName: '   ' },
      { id: 22, beanId: 'bean-3', beanName: '' },
    ];
    // bean-2 is in the library but with a whitespace-only name (also a fallback).
    const beans = [{ id: 'bean-2', name: '   ' }];
    const result = availableBeans(shots, beans);

    // Labels are distinct and follow the deterministic insertion-order sequence.
    const byId = new Map(result.map(b => [b.id, b.name]));
    expect(byId.get('bean-1')).toBe('Unknown bean');
    expect(byId.get('bean-2')).toBe('Unknown bean (2)');
    expect(byId.get('bean-3')).toBe('Unknown bean (3)');

    // No two rows share a label, and each keeps its own id for filtering.
    const labels = result.map(b => b.name);
    expect(new Set(labels).size).toBe(labels.length);
    expect(result.map(b => b.id).sort()).toEqual(['bean-1', 'bean-2', 'bean-3']);
  });

  test('availableBeans sorts disambiguation suffixes numerically, not lexically (PRO-414)', () => {
    // 12 distinct blank-name beans -> 'Unknown bean', 'Unknown bean (2)' … '(12)'.
    const shots = Array.from({ length: 12 }, (_, i) => ({
      id: 100 + i,
      beanId: `bean-${i + 1}`,
      beanName: '',
    }));
    const result = availableBeans(shots, []);

    const labels = result.map(b => b.name);
    // Numeric-aware order: '(2)' precedes '(10)' (lexical would put '(10)' first).
    expect(labels).toEqual([
      'Unknown bean',
      'Unknown bean (2)',
      'Unknown bean (3)',
      'Unknown bean (4)',
      'Unknown bean (5)',
      'Unknown bean (6)',
      'Unknown bean (7)',
      'Unknown bean (8)',
      'Unknown bean (9)',
      'Unknown bean (10)',
      'Unknown bean (11)',
      'Unknown bean (12)',
    ]);
    expect(labels.indexOf('Unknown bean (2)')).toBeLessThan(labels.indexOf('Unknown bean (10)'));
  });

  test('availableBeans only disambiguates fallback rows, never real names (PRO-410)', () => {
    const shots = [
      { id: 30, beanId: 'bean-real', beanName: 'Colombia' },
      { id: 31, beanId: 'bean-blank', beanName: '' },
    ];
    const beans = [{ id: 'bean-real', name: 'Colombia' }];
    const result = availableBeans(shots, beans);

    // The real name is untouched; the single blank row gets the bare fallback.
    const byId = new Map(result.map(b => [b.id, b.name]));
    expect(byId.get('bean-real')).toBe('Colombia');
    expect(byId.get('bean-blank')).toBe('Unknown bean');
  });
});

describe('URL query serialization', () => {
  test('filtersToQueryString omits defaults', () => {
    expect(filtersToQueryString(defaultFilters())).toBe('');
  });

  test('round-trips through query string', () => {
    const filters = {
      ...defaultFilters(),
      dateFrom: '2026-01-01',
      dateTo: '2026-12-31',
      profile: 'Espresso',
      beanId: 'bean-a',
      durationMin: '20',
      durationMax: '40',
      text: 'fruity',
    };
    const qs = filtersToQueryString(filters);
    expect(qs.startsWith('?')).toBe(true);
    expect(filtersFromQuery(qs)).toEqual(filters);
  });

  test('filtersFromQuery falls back to defaults for missing params', () => {
    expect(filtersFromQuery('?profile=Turbo')).toEqual({
      ...defaultFilters(),
      profile: 'Turbo',
    });
  });

  test('filtersToSearchParams uses short param names', () => {
    const params = filtersToSearchParams({ ...defaultFilters(), text: 'x', durationMin: '5' });
    expect(params.get('q')).toBe('x');
    expect(params.get('dmin')).toBe('5');
    expect(params.get('from')).toBeNull();
  });
});

describe('mergeFiltersIntoSearch', () => {
  test('all-default filters + no unrelated params -> empty query string', () => {
    expect(mergeFiltersIntoSearch('', defaultFilters())).toBe('');
    expect(mergeFiltersIntoSearch(undefined, defaultFilters())).toBe('');
  });

  test('preserves an unrelated param when a filter is applied', () => {
    const query = mergeFiltersIntoSearch('?ref=abc', { ...defaultFilters(), profile: 'Espresso' });
    const params = new URLSearchParams(query);
    expect(params.get('ref')).toBe('abc');
    expect(params.get('profile')).toBe('Espresso');
  });

  test('clearing a filter removes its own param but keeps the unrelated one', () => {
    // Start with an active filter param plus an unrelated param, then clear filters.
    const query = mergeFiltersIntoSearch('?profile=Espresso&ref=abc', defaultFilters());
    const params = new URLSearchParams(query);
    expect(params.get('profile')).toBeNull();
    expect(params.get('ref')).toBe('abc');
  });

  test('writes active filter params alongside a preserved unrelated param', () => {
    const filters = {
      ...defaultFilters(),
      profile: 'Espresso',
      beanId: 'bean-a',
      text: 'fruity',
    };
    const query = mergeFiltersIntoSearch('?ref=abc', filters);
    const params = new URLSearchParams(query);
    expect(params.get('ref')).toBe('abc');
    expect(params.get('profile')).toBe('Espresso');
    expect(params.get('bean')).toBe('bean-a');
    expect(params.get('q')).toBe('fruity');
  });

  test('owned params are authoritative: stale filter values are replaced, not stacked', () => {
    // A pre-existing owned param at a different value gets overwritten by the
    // currently-active filter rather than appended twice.
    const query = mergeFiltersIntoSearch('?profile=Turbo&ref=abc', {
      ...defaultFilters(),
      profile: 'Espresso',
    });
    const params = new URLSearchParams(query);
    expect(params.getAll('profile')).toEqual(['Espresso']);
    expect(params.get('ref')).toBe('abc');
  });
});
