import { describe, it, expect } from 'vitest';
import { normalizeTenPointRating, formatTenPointRating, getRatingFillPercent } from './ratings.js';

describe('normalizeTenPointRating', () => {
  it('rounds to the nearest 0.1', () => {
    expect(normalizeTenPointRating(8.74)).toBe(8.7);
    expect(normalizeTenPointRating(8.75)).toBe(8.8);
    expect(normalizeTenPointRating(8.7)).toBe(8.7); // PRO-297: not snapped to 8.75 or integer
  });

  it('treats <= 0 / non-finite as unrated (0)', () => {
    expect(normalizeTenPointRating(0)).toBe(0);
    expect(normalizeTenPointRating(-3)).toBe(0);
    expect(normalizeTenPointRating('')).toBe(0);
    expect(normalizeTenPointRating('abc')).toBe(0);
  });

  it('clamps to [1, 10] for positive values (existing min-of-1 behavior preserved)', () => {
    expect(normalizeTenPointRating(0.4)).toBe(1);
    expect(normalizeTenPointRating(12)).toBe(10);
    expect(normalizeTenPointRating(10)).toBe(10);
  });

  it('accepts numeric strings', () => {
    expect(normalizeTenPointRating('8.7')).toBe(8.7);
  });
});

describe('formatTenPointRating', () => {
  it('renders one decimal cleanly', () => {
    expect(formatTenPointRating(8.7)).toBe('8.7/10');
  });

  it('strips a trailing .0 for whole numbers', () => {
    expect(formatTenPointRating(9)).toBe('9/10');
    expect(formatTenPointRating(10)).toBe('10/10');
  });

  it('renders an em dash for unrated', () => {
    expect(formatTenPointRating(0)).toBe('\u2014');
  });
});

describe('getRatingFillPercent', () => {
  it('is proportional to the decimal value', () => {
    expect(getRatingFillPercent(8.7)).toBe('87%');
    expect(getRatingFillPercent(10)).toBe('100%');
    expect(getRatingFillPercent(0)).toBe('0%');
  });
});
