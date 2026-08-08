import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import {
  applyThemePreferences,
  getEffectiveAccents,
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

describe('accent token application', () => {
  it('applies the app accent globally and inherits it for the dashboard', () => {
    applyThemePreferences({ theme: 'blueprint', appAccent: '#ffffff', dashboardAccent: null });
    const root = document.documentElement;
    expect(root.dataset.theme).toBe('blueprint');
    expect(root.style.getPropertyValue('--gm-app-accent')).toBe('#ffffff');
    expect(root.style.getPropertyValue('--gm-app-accent-content')).toBe('#000000');
    expect(root.style.getPropertyValue('--gm-dashboard-accent')).toBe('#ffffff');
    expect(root.style.getPropertyValue('--gm-dashboard-accent-content')).toBe('#000000');
  });

  it('uses the dashboard override without changing the global app accent', () => {
    applyThemePreferences({ theme: 'midnight', appAccent: '#ffffff', dashboardAccent: '#0000ff' });
    const root = document.documentElement;
    expect(root.style.getPropertyValue('--gm-app-accent')).toBe('#ffffff');
    expect(root.style.getPropertyValue('--gm-dashboard-accent')).toBe('#0000ff');
  });

  it('picks a white content color for a dark accent', () => {
    applyThemePreferences({ theme: 'midnight', appAccent: '#000000', dashboardAccent: null });
    const root = document.documentElement;
    expect(root.style.getPropertyValue('--gm-app-accent-content')).toBe('#ffffff');
    expect(root.style.getPropertyValue('--gm-dashboard-accent-content')).toBe('#ffffff');
  });

  it('falls back to the selected named theme accent, not midnight, with no override', () => {
    applyThemePreferences({ theme: 'matcha', appAccent: null, dashboardAccent: null });
    const root = document.documentElement;
    expect(root.style.getPropertyValue('--gm-app-accent')).toBe('#5aaa52');
    expect(root.style.getPropertyValue('--gm-dashboard-accent')).toBe('#5aaa52');
  });

  it('sets the full derived token contract for both scopes', () => {
    applyThemePreferences({ theme: 'espresso', appAccent: null, dashboardAccent: null });
    const root = document.documentElement;
    for (const scope of ['app', 'dashboard']) {
      for (const suffix of ['', '-content', '-hover', '-muted', '-border', '-glow']) {
        expect(root.style.getPropertyValue(`--gm-${scope}-accent${suffix}`)).not.toBe('');
      }
    }
  });

  it('re-applies the named theme default after resetting the app accent', () => {
    setStoredTheme('blueprint');
    setAppAccent('#ff6600');
    expect(document.documentElement.style.getPropertyValue('--gm-app-accent')).toBe('#ff6600');

    resetAppAccent();
    expect(document.documentElement.style.getPropertyValue('--gm-app-accent')).toBe('#4d8fd1');
    expect(document.documentElement.style.getPropertyValue('--gm-dashboard-accent')).toBe('#4d8fd1');
  });

  it('reports effective accents including inheritance', () => {
    expect(getEffectiveAccents({ theme: 'matcha', appAccent: null, dashboardAccent: null })).toEqual(
      { app: '#5aaa52', dashboard: '#5aaa52' },
    );
    expect(
      getEffectiveAccents({ theme: 'matcha', appAccent: '#ff6600', dashboardAccent: null }),
    ).toEqual({ app: '#ff6600', dashboard: '#ff6600' });
    expect(
      getEffectiveAccents({ theme: 'matcha', appAccent: '#ff6600', dashboardAccent: '#0000ff' }),
    ).toEqual({ app: '#ff6600', dashboard: '#0000ff' });
  });

  it('ignores malformed application input rather than clearing the tokens', () => {
    applyThemePreferences({ theme: 'nope', appAccent: 'red', dashboardAccent: 7 });
    const root = document.documentElement;
    expect(root.dataset.theme).toBe('midnight');
    expect(root.style.getPropertyValue('--gm-app-accent')).toBe('#d71921');
  });
});

// The manager only sets --gm-* tokens; style.css is what routes them into
// --color-primary and the Dashboard's --dm-accent / glow. Guard that wiring so a
// future CSS edit cannot silently re-hardcode an accent and strand the feature.
describe('style.css accent contract', () => {
  const stylesheet = readFileSync(resolve(process.cwd(), 'src/style.css'), 'utf8');

  it('routes --color-primary through the app accent token', () => {
    expect(stylesheet).toMatch(/--color-primary:\s*var\(--gm-app-accent\)/);
    expect(stylesheet).toMatch(/--color-primary-content:\s*var\(--gm-app-accent-content\)/);
  });

  it('routes the dashboard --dm-accent and glow through the dashboard accent token', () => {
    expect(stylesheet).toMatch(/--dm-accent:\s*var\(--gm-dashboard-accent\)/);
    expect(stylesheet).toMatch(/--app-shell-glow:\s*radial-gradient\([^)]*\n?[^;]*var\(--gm-dashboard-accent-glow\)/);
  });

  it('no longer hardcodes a per-theme --dm-accent inside .dm-shell', () => {
    expect(stylesheet).not.toMatch(/--dm-accent:\s*#[0-9a-f]{6}/i);
  });

  it('keeps semantic warning/success/error tokens off the accent contract', () => {
    for (const token of ['--color-warning', '--color-success']) {
      expect(stylesheet).not.toMatch(new RegExp(`${token}:\\s*var\\(--gm-`));
    }
  });
});
