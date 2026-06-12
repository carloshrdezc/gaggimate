// Grinder name persistence for the Shot Notes form.
//
// Mirrors the dual-persistence approach used by beanManager.js: when a device
// WebSocket connection is available, grinder names are stored on the device
// (shared across every browser that connects to the machine); otherwise they
// fall back to browser localStorage. The localStorage copy is also kept in
// sync so suggestions are available instantly before the socket connects.
//
// The stored value is a simple ordered array of name strings, most-recently
// used first, deduplicated case-insensitively and capped.

const GRINDERS_STORAGE_KEY = 'gaggimate-grinders';
const GRINDER_LIST_MAX = 50;
const GRINDER_NAME_MAX_LEN = 64;

function normalize(text) {
  return String(text || '')
    .trim()
    .toLowerCase();
}

function readJson(key, fallback) {
  try {
    const raw = localStorage.getItem(key);
    return raw ? JSON.parse(raw) : fallback;
  } catch {
    return fallback;
  }
}

function writeJson(key, value) {
  try {
    localStorage.setItem(key, JSON.stringify(value));
  } catch {
    // ignore storage failures (private mode / quota)
  }
}

// Sanitize an arbitrary array into a clean, deduped, capped grinder list.
function sanitizeList(input) {
  const seen = new Set();
  const result = [];
  for (const entry of Array.isArray(input) ? input : []) {
    const name = String(entry || '').trim();
    if (!name || name.length > GRINDER_NAME_MAX_LEN) continue;
    const key = name.toLowerCase();
    if (seen.has(key)) continue;
    seen.add(key);
    result.push(name);
    if (result.length >= GRINDER_LIST_MAX) break;
  }
  return result;
}

function listLegacyGrinders() {
  return sanitizeList(readJson(GRINDERS_STORAGE_KEY, []));
}

function saveLegacyGrinders(grinders) {
  writeJson(GRINDERS_STORAGE_KEY, sanitizeList(grinders));
}

// Move `name` to the front of the list, deduplicating case-insensitively.
function promote(grinders, name) {
  const trimmed = String(name || '').trim();
  if (!trimmed || trimmed.length > GRINDER_NAME_MAX_LEN) return sanitizeList(grinders);
  const key = trimmed.toLowerCase();
  const rest = (Array.isArray(grinders) ? grinders : []).filter(
    existing => normalize(existing) !== key,
  );
  return sanitizeList([trimmed, ...rest]);
}

function hasConnectedApi(apiService) {
  return !!(apiService?.socket && apiService.socket.readyState === WebSocket.OPEN);
}

async function requestGrinders(apiService, payload) {
  if (!apiService) return null;
  const response = await apiService.request(payload);
  if (response?.error) {
    throw new Error(response.error);
  }
  return response;
}

async function listDeviceGrinders(apiService) {
  const response = await requestGrinders(apiService, { tp: 'req:grinders:list' });
  return sanitizeList(response?.grinders);
}

/**
 * Returns the saved grinder names, most-recently-used first. Prefers the
 * device list when connected, otherwise falls back to localStorage.
 */
export async function listGrinders(apiService) {
  const legacyGrinders = listLegacyGrinders();
  if (!hasConnectedApi(apiService)) {
    return legacyGrinders;
  }

  try {
    const deviceGrinders = await listDeviceGrinders(apiService);
    // Keep the local cache fresh so suggestions appear instantly next load.
    saveLegacyGrinders(deviceGrinders);
    return deviceGrinders;
  } catch {
    return legacyGrinders;
  }
}

/**
 * Records `name` as the most-recently-used grinder. Persists to the device
 * when connected (and mirrors into localStorage); otherwise localStorage only.
 * No-ops for empty/blank names. Returns the updated list.
 */
export async function recordGrinder(apiService, name) {
  const trimmed = String(name || '').trim();
  if (!trimmed) return listLegacyGrinders();

  // Always update the local cache so the value is available immediately.
  const nextLegacy = promote(listLegacyGrinders(), trimmed);
  saveLegacyGrinders(nextLegacy);

  if (!hasConnectedApi(apiService)) {
    return nextLegacy;
  }

  try {
    const response = await requestGrinders(apiService, {
      tp: 'req:grinders:save',
      name: trimmed,
    });
    const deviceGrinders = sanitizeList(response?.grinders);
    saveLegacyGrinders(deviceGrinders);
    return deviceGrinders;
  } catch {
    return nextLegacy;
  }
}
