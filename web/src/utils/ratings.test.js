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

  it('rounds away floating-point dust so the width string stays clean (PRO-300)', () => {
    // Regression pin for the Math.round guard in getRatingFillPercent.
    // (5.7 / 10) * 100 evaluates to 57.00000000000001 in IEEE-754, and 2.9
    // gives 28.999999999999996. Without the rounding guard those leak straight
    // into the inline `width` style. This asserts the guard keeps them clean —
    // remove the Math.round in getRatingFillPercent and these expectations fail.
    expect(getRatingFillPercent(5.7)).toBe('57%');
    expect(getRatingFillPercent(2.9)).toBe('29%');
  });
});
