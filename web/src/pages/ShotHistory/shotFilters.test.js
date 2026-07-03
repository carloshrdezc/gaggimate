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
