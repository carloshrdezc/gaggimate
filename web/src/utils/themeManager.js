export const THEME_STORAGE_KEY = 'gaggimate-daisyui-theme';

const AVAILABLE_THEMES = ['midnight', 'espresso', 'matcha', 'blueprint'];
const DEFAULT_THEME = 'midnight';

// Accent default per named theme. The manager owns this map (rather than
// reading it back out of CSS) because JavaScript and the Settings UI must be
// able to show the effective inherited dashboard color even when the user has
// no custom app accent. These values mirror the `--dm-accent` defaults declared
// by the `html[data-theme='…'] .dm-shell` blocks in `style.css`.
const THEME_ACCENT_DEFAULTS = {
  midnight: '#d71921',
  espresso: '#c07839',
  matcha: '#5aaa52',
  blueprint: '#4d8fd1',
};

// Only used when a storage WRITE failed: the mutation still has to take effect
// for the current session, so we keep the accepted preference in memory and
// serve it while localStorage has no (newer) value of its own. It is cleared on
// the next successful write so a stale in-memory copy can never shadow storage.
let fallbackPreferences = null;

function defaultPreferences() {
  return { theme: DEFAULT_THEME, appAccent: null, dashboardAccent: null };
}

export function normalizeHexColor(value) {
  const match = /^#?([0-9a-f]{6})$/i.exec(String(value).trim());
  return match ? `#${match[1].toLowerCase()}` : null;
}

function normalizeTheme(value) {
  return AVAILABLE_THEMES.includes(value) ? value : DEFAULT_THEME;
}

// Single validation funnel for anything that claims to be a preference object,
// whether it came from storage, from a caller of applyThemePreferences(), or
// from our own mutations. Invalid members degrade to their defaults instead of
// failing the whole read.
function sanitizePreferences(candidate) {
  if (!candidate || typeof candidate !== 'object') return defaultPreferences();
  return {
    theme: normalizeTheme(candidate.theme),
    appAccent: normalizeHexColor(candidate.appAccent ?? ''),
    dashboardAccent: normalizeHexColor(candidate.dashboardAccent ?? ''),
  };
}

export function getThemePreferences() {
  let raw = null;
  try {
    raw = localStorage.getItem(THEME_STORAGE_KEY);
  } catch (error) {
    console.warn('Failed to get stored theme:', error);
    return fallbackPreferences ? { ...fallbackPreferences } : defaultPreferences();
  }

  if (raw === null || raw === '') {
    return fallbackPreferences ? { ...fallbackPreferences } : defaultPreferences();
  }

  // Legacy format: the bare named-theme string written by older builds.
  if (!raw.trim().startsWith('{')) {
    return { ...defaultPreferences(), theme: normalizeTheme(raw) };
  }

  try {
    return sanitizePreferences(JSON.parse(raw));
  } catch (error) {
    console.warn('Failed to parse stored theme preferences:', error);
    return defaultPreferences();
  }
}

function persistPreferences(preferences) {
  try {
    localStorage.setItem(THEME_STORAGE_KEY, JSON.stringify(preferences));
    fallbackPreferences = null;
  } catch (error) {
    console.warn('Failed to set stored theme:', error);
    fallbackPreferences = { ...preferences };
  }
}

// Every accepted mutation persists (best effort) and then re-applies the root
// tokens, so storage and the DOM can never disagree about what is in effect.
function commitPreferences(preferences) {
  persistPreferences(preferences);
  applyThemePreferences(preferences);
  return true;
}

export function getStoredTheme() {
  return getThemePreferences().theme;
}

export function setStoredTheme(theme) {
  if (!AVAILABLE_THEMES.includes(theme)) return false;
  return commitPreferences({ ...getThemePreferences(), theme });
}

export function setAppAccent(color) {
  const appAccent = normalizeHexColor(color);
  if (!appAccent) return false;
  return commitPreferences({ ...getThemePreferences(), appAccent });
}

export function resetAppAccent() {
  return commitPreferences({ ...getThemePreferences(), appAccent: null });
}

export function setDashboardAccent(color) {
  const dashboardAccent = normalizeHexColor(color);
  if (!dashboardAccent) return false;
  return commitPreferences({ ...getThemePreferences(), dashboardAccent });
}

export function resetDashboardAccent() {
  return commitPreferences({ ...getThemePreferences(), dashboardAccent: null });
}

// ── color derivation ──────────────────────────────────────────────────────────

function hexToRgb(hex) {
  const value = Number.parseInt(hex.slice(1), 16);
  return [(value >> 16) & 0xff, (value >> 8) & 0xff, value & 0xff];
}

// WCAG relative luminance.
function relativeLuminance(hex) {
  const [r, g, b] = hexToRgb(hex).map(channel => {
    const c = channel / 255;
    return c <= 0.03928 ? c / 12.92 : ((c + 0.055) / 1.055) ** 2.4;
  });
  return 0.2126 * r + 0.7152 * g + 0.0722 * b;
}

// Pick black or white for text/icons drawn on the accent, whichever has the
// greater WCAG contrast ratio against it.
function contentColorFor(hex) {
  const luminance = relativeLuminance(hex);
  const contrastWithBlack = (luminance + 0.05) / 0.05;
  const contrastWithWhite = 1.05 / (luminance + 0.05);
  return contrastWithBlack >= contrastWithWhite ? '#000000' : '#ffffff';
}

export function getThemeAccentDefault(theme) {
  return THEME_ACCENT_DEFAULTS[normalizeTheme(theme)];
}

// Effective colors a caller (Settings, tests) needs to describe the current
// state: the app accent actually in force and the dashboard accent it inherits.
export function getEffectiveAccents(preferences = getThemePreferences()) {
  const { theme, appAccent, dashboardAccent } = sanitizePreferences(preferences);
  const appEffective = appAccent ?? getThemeAccentDefault(theme);
  return { app: appEffective, dashboard: dashboardAccent ?? appEffective };
}

function applyAccentTokens(root, scope, color) {
  root.style.setProperty(`--gm-${scope}-accent`, color);
  root.style.setProperty(`--gm-${scope}-accent-content`, contentColorFor(color));
  root.style.setProperty(
    `--gm-${scope}-accent-hover`,
    `color-mix(in srgb, var(--gm-${scope}-accent) 86%, var(--gm-${scope}-accent-content))`,
  );
  root.style.setProperty(
    `--gm-${scope}-accent-muted`,
    `color-mix(in srgb, var(--gm-${scope}-accent) 12%, transparent)`,
  );
  root.style.setProperty(
    `--gm-${scope}-accent-border`,
    `color-mix(in srgb, var(--gm-${scope}-accent) 60%, transparent)`,
  );
  root.style.setProperty(
    `--gm-${scope}-accent-glow`,
    `color-mix(in srgb, var(--gm-${scope}-accent) 12%, transparent)`,
  );
}

export function applyThemePreferences(preferences) {
  const sanitized = sanitizePreferences(preferences);
  const root = document.documentElement;
  root.setAttribute('data-theme', sanitized.theme);

  const effective = getEffectiveAccents(sanitized);
  applyAccentTokens(root, 'app', effective.app);
  applyAccentTokens(root, 'dashboard', effective.dashboard);

  const radio = document.querySelector(`input.theme-controller[value="${sanitized.theme}"]`);
  if (radio) {
    radio.checked = true;
  }
  return sanitized;
}

export function getAvailableThemes() {
  return AVAILABLE_THEMES.map(theme => ({
    value: theme,
    label: theme.charAt(0).toUpperCase() + theme.slice(1),
  }));
}

export function initializeTheme() {
  applyThemePreferences(getThemePreferences());
}

export function handleThemeChange(event) {
  setStoredTheme(event.target.value);
}
