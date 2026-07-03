// shotFilters.js
//
// Pure, side-effect-free filter logic for the Shot History page (PRO-31).
// Kept separate from index.jsx so the AND-composed filtering, defaults, and
// URL <-> filter-state serialization can be unit tested without rendering.
//
// Filter types (all applied CLIENT-SIDE against the already-loaded history
// snapshot; no new WebSocket requests):
//   * dateFrom / dateTo  — inclusive day bounds on the shot timestamp
//   * profile            — exact match on the shot's profile name
//   * beanId             — exact match on the inferred bean id
//   * durationMin/Max    — inclusive range in SECONDS on the shot duration
//   * text               — free-text substring search over the shot's notes
//
// Timestamp unit: shot.timestamp is Unix SECONDS (Uint32 from the binary
// index / firmware convention). Duration unit: shot.duration is MILLISECONDS
// in state (HistoryCard renders `shot.duration / 1000`), so the seconds-based
// duration filter compares against `shot.duration / 1000`.

export const DEFAULT_FILTERS = Object.freeze({
  dateFrom: '', // 'YYYY-MM-DD'
  dateTo: '', // 'YYYY-MM-DD'
  profile: '', // profile name, '' = any
  beanId: '', // inferred bean id, '' = any
  durationMin: '', // seconds, '' = no lower bound
  durationMax: '', // seconds, '' = no upper bound
  text: '', // free-text notes search
});

// Map from filter key -> URL query param name. Short names keep the URL tidy.
const QUERY_KEYS = {
  dateFrom: 'from',
  dateTo: 'to',
  profile: 'profile',
  beanId: 'bean',
  durationMin: 'dmin',
  durationMax: 'dmax',
  text: 'q',
};

export function defaultFilters() {
  return { ...DEFAULT_FILTERS };
}

export function isDefaultFilters(filters) {
  return Object.keys(DEFAULT_FILTERS).every(
    key => String(filters?.[key] ?? '') === String(DEFAULT_FILTERS[key]),
  );
}

// True when no filter is active (all defaults). Used to decide whether the
// filter panel badge / clear button should signal an active state.
export function hasActiveFilters(filters) {
  return !isDefaultFilters(filters);
}

// Extract the free-text notes string from a shot regardless of whether notes
// are stored as an object ({ notes: '...' }) or a raw string.
function shotNotesText(shot) {
  const notes = shot?.notes;
  if (!notes) return '';
  if (typeof notes === 'string') return notes;
  if (typeof notes === 'object') return String(notes.notes || '');
  return '';
}

// Convert a 'YYYY-MM-DD' local-date string to a Unix-seconds bound.
// `endOfDay` pushes to 23:59:59 of that day so a `to` bound is inclusive.
function dateStringToUnixSeconds(dateStr, endOfDay) {
  if (!dateStr) return null;
  const parts = String(dateStr).split('-').map(Number);
  if (parts.length !== 3 || parts.some(n => !Number.isFinite(n))) return null;
  const [year, month, day] = parts;
  const date = endOfDay
    ? new Date(year, month - 1, day, 23, 59, 59, 999)
    : new Date(year, month - 1, day, 0, 0, 0, 0);
  const ms = date.getTime();
  if (!Number.isFinite(ms)) return null;
  return Math.floor(ms / 1000);
}

function toFiniteNumber(value) {
  if (value === '' || value === null || value === undefined) return null;
  const n = Number(value);
  return Number.isFinite(n) ? n : null;
}

/**
 * Apply all active filters to a shot list, composed as AND.
 * Pure: returns a new array, never mutates the input.
 */
export function applyShotFilters(shots, filters) {
  const list = Array.isArray(shots) ? shots : [];
  const f = { ...DEFAULT_FILTERS, ...(filters || {}) };

  const fromTs = dateStringToUnixSeconds(f.dateFrom, false);
  const toTs = dateStringToUnixSeconds(f.dateTo, true);
  const durMin = toFiniteNumber(f.durationMin);
  const durMax = toFiniteNumber(f.durationMax);
  const profile = String(f.profile || '').trim();
  const beanId = String(f.beanId || '').trim();
  const text = String(f.text || '')
    .trim()
    .toLowerCase();

  return list.filter(shot => {
    // Date range (Unix seconds).
    if (fromTs !== null && !(Number(shot.timestamp) >= fromTs)) return false;
    if (toTs !== null && !(Number(shot.timestamp) <= toTs)) return false;

    // Profile (exact name match).
    if (profile && String(shot.profile || '') !== profile) return false;

    // Bean (exact inferred id match).
    if (beanId && String(shot.beanId || '') !== beanId) return false;

    // Duration range in seconds (shot.duration is milliseconds).
    if (durMin !== null || durMax !== null) {
      const durSec = Number(shot.duration || 0) / 1000;
      if (durMin !== null && durSec < durMin) return false;
      if (durMax !== null && durSec > durMax) return false;
    }

    // Free-text search over notes.
    if (text && !shotNotesText(shot).toLowerCase().includes(text)) return false;

    return true;
  });
}

/**
 * Collect the distinct profile names present in the shot history, sorted
 * alphabetically. Used to populate the profile filter dropdown.
 */
export function availableProfiles(shots) {
  const set = new Set();
  for (const shot of Array.isArray(shots) ? shots : []) {
    const name = String(shot.profile || '').trim();
    if (name) set.add(name);
  }
  return [...set].sort((a, b) => a.localeCompare(b));
}

/**
 * Collect the distinct beans referenced by the shot history as
 * { id, name } pairs, sorted by name. Falls back to the shot's inferred
 * beanName when a bean id is present but not in the supplied bean library.
 */
export function availableBeans(shots, beans) {
  const beanById = new Map((Array.isArray(beans) ? beans : []).map(b => [b.id, b]));
  const seen = new Map();
  for (const shot of Array.isArray(shots) ? shots : []) {
    const id = String(shot.beanId || '').trim();
    if (!id || seen.has(id)) continue;
    const libBean = beanById.get(id);
    const name = String(libBean?.name || shot.beanName || id).trim();
    seen.set(id, { id, name });
  }
  return [...seen.values()].sort((a, b) => a.name.localeCompare(b.name));
}

/**
 * Parse filter state from a URL query string (e.g. window.location.search).
 * Unknown params are ignored; missing params fall back to defaults.
 */
export function filtersFromQuery(search) {
  const params = new URLSearchParams(search || '');
  const filters = defaultFilters();
  for (const [key, param] of Object.entries(QUERY_KEYS)) {
    const value = params.get(param);
    if (value !== null) filters[key] = value;
  }
  return filters;
}

/**
 * Serialize active filters to a URLSearchParams. Default-valued filters are
 * omitted so a cleared filter set produces an empty query string.
 */
export function filtersToSearchParams(filters) {
  const params = new URLSearchParams();
  const f = { ...DEFAULT_FILTERS, ...(filters || {}) };
  for (const [key, param] of Object.entries(QUERY_KEYS)) {
    const value = String(f[key] ?? '').trim();
    if (value !== '') params.set(param, value);
  }
  return params;
}

/**
 * Serialize active filters to a query string. Returns '' when all filters are
 * at their defaults (so the URL has no trailing '?').
 */
export function filtersToQueryString(filters) {
  const params = filtersToSearchParams(filters);
  const str = params.toString();
  return str ? `?${str}` : '';
}
