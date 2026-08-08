import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import {
  getStoredTheme,
  getThemePreferences,
  normalizeHexColor,
  resetAppAccent,
  resetDashboardAccent,
  setAppAccent,
  setDashboardAccent,
  setStoredTheme,
} from './themeManager.js';

beforeEach(() => {
  localStorage.clear();
  document.documentElement.removeAttribute('data-theme');
  document.documentElement.removeAttribute('style');
});

afterEach(() => vi.restoreAllMocks());

describe('theme preferences', () => {
  it('migrates the legacy named-theme string without custom accents', () => {
    localStorage.setItem('gaggimate-daisyui-theme', 'espresso');
    expect(getThemePreferences()).toEqual({
      theme: 'espresso',
      appAccent: null,
      dashboardAccent: null,
    });
  });

  it('normalizes only six-digit hex colors', () => {
    expect(normalizeHexColor('#A1B2C3')).toBe('#a1b2c3');
    expect(normalizeHexColor('a1b2c3')).toBe('#a1b2c3');
    expect(normalizeHexColor('#abc')).toBeNull();
    expect(normalizeHexColor('#abcdex')).toBeNull();
  });

  it('preserves a dashboard override across an app accent change and resets independently', () => {
    setAppAccent('#ff6600');
    setDashboardAccent('#33cc99');
    setAppAccent('#6633ff');
    expect(getThemePreferences()).toMatchObject({
      appAccent: '#6633ff',
      dashboardAccent: '#33cc99',
    });

    resetDashboardAccent();
    expect(getThemePreferences().dashboardAccent).toBeNull();

    resetAppAccent();
    expect(getThemePreferences().appAccent).toBeNull();
  });

  it('rejects invalid setters without replacing a valid active preference', () => {
    expect(setAppAccent('#123456')).toBe(true);
    expect(setAppAccent('red')).toBe(false);
    expect(getThemePreferences().appAccent).toBe('#123456');
  });

  it('returns defaults when nothing is stored', () => {
    expect(getThemePreferences()).toEqual({
      theme: 'midnight',
      appAccent: null,
      dashboardAccent: null,
    });
    expect(getStoredTheme()).toBe('midnight');
  });

  it('falls back to the default theme for malformed or unknown stored data', () => {
    localStorage.setItem('gaggimate-daisyui-theme', 'not-a-theme');
    expect(getThemePreferences()).toEqual({
      theme: 'midnight',
      appAccent: null,
      dashboardAccent: null,
    });

    localStorage.setItem('gaggimate-daisyui-theme', '{"theme":');
    expect(getThemePreferences()).toEqual({
      theme: 'midnight',
      appAccent: null,
      dashboardAccent: null,
    });

    localStorage.setItem(
      'gaggimate-daisyui-theme',
      JSON.stringify({ theme: 'nope', appAccent: 'blue', dashboardAccent: 42 }),
    );
    expect(getThemePreferences()).toEqual({
      theme: 'midnight',
      appAccent: null,
      dashboardAccent: null,
    });
  });

  it('keeps getStoredTheme() string-compatible for the backup contract', () => {
    setStoredTheme('matcha');
    setAppAccent('#123456');
    expect(getStoredTheme()).toBe('matcha');
    // The persisted payload is the versioned object, not a bare string.
    expect(JSON.parse(localStorage.getItem('gaggimate-daisyui-theme'))).toMatchObject({
      theme: 'matcha',
      appAccent: '#123456',
    });
  });

  it('retains active accent overrides when only the named theme changes', () => {
    setAppAccent('#ff6600');
    setDashboardAccent('#33cc99');
    expect(setStoredTheme('blueprint')).toBe(true);
    expect(getThemePreferences()).toEqual({
      theme: 'blueprint',
      appAccent: '#ff6600',
      dashboardAccent: '#33cc99',
    });
    expect(setStoredTheme('nope')).toBe(false);
    expect(getThemePreferences().theme).toBe('blueprint');
  });

  it('rejects an invalid dashboard accent without clearing the active override', () => {
    expect(setDashboardAccent('#33cc99')).toBe(true);
    expect(setDashboardAccent('#zzz')).toBe(false);
    expect(getThemePreferences().dashboardAccent).toBe('#33cc99');
  });

  it('applies the preference for the session when storage writes fail', () => {
    const setItem = vi.spyOn(Storage.prototype, 'setItem').mockImplementation(() => {
      throw new Error('quota');
    });
    const warn = vi.spyOn(console, 'warn').mockImplementation(() => {});

    expect(setAppAccent('#abcdef')).toBe(true);
    expect(document.documentElement.style.getPropertyValue('--gm-app-accent')).toBe('#abcdef');
    expect(getThemePreferences().appAccent).toBe('#abcdef');
    expect(warn).toHaveBeenCalled();

    setItem.mockRestore();
  });
});
