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
//
// In addition to the cached mirror of the device list, we track a *separate*
// set of "pending offline" grinder names: entries that recordGrinder() saved
// to localStorage while no device connection was available, and which the
// device therefore has never seen. Only these genuinely-unsynced names are
// migrated to the device on the next connected call — a stale cache entry that
// the device has merely evicted (the list is capped and most-recent-first) is
// NOT a pending offline write and must not be re-pushed. A pending name is
// cleared only once its device save succeeds; a failed save leaves it pending
// (and in the cache) so it can be retried later instead of being silently lost.

const GRINDERS_STORAGE_KEY = 'gaggimate-grinders';
const GRINDERS_PENDING_STORAGE_KEY = 'gaggimate-grinders-pending';
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

// Notify any other mounted ShotNotesCards so their datalist refreshes when a
// grinder is recorded elsewhere (mirrors beanManager's beans-library-changed).
function dispatchGrindersChanged(grinders) {
  if (typeof window !== 'undefined') {
    window.dispatchEvent(new CustomEvent('grinders-library-changed', { detail: grinders }));
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

// --- Pending offline grinders -------------------------------------------------
//
// Stored as an ordered (most-recent-first) array of name strings, deduplicated
// case-insensitively. These are names recordGrinder() persisted to localStorage
// while offline; the device has never received them. Capped to the same bound
// as the main list so a long offline streak can't grow storage unbounded.

function listPendingGrinders() {
  return sanitizeList(readJson(GRINDERS_PENDING_STORAGE_KEY, []));
}

function savePendingGrinders(names) {
  writeJson(GRINDERS_PENDING_STORAGE_KEY, sanitizeList(names));
}

// Mark `name` as a pending offline addition (most-recent-first).
function addPendingGrinder(name) {
  const trimmed = String(name || '').trim();
  if (!trimmed || trimmed.length > GRINDER_NAME_MAX_LEN) return;
  const key = trimmed.toLowerCase();
  const rest = listPendingGrinders().filter(existing => normalize(existing) !== key);
  savePendingGrinders([trimmed, ...rest]);
}

// Drop `name` from the pending set once it has been synced to the device.
function clearPendingGrinder(name) {
  const key = normalize(name);
  if (!key) return;
  const next = listPendingGrinders().filter(existing => normalize(existing) !== key);
  savePendingGrinders(next);
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

// Push a single name to the device and return the resulting device list.
async function saveDeviceGrinder(apiService, name) {
  const response = await requestGrinders(apiService, { tp: 'req:grinders:save', name });
  return sanitizeList(response?.grinders);
}

/**
 * Returns the saved grinder names, most-recently-used first. Prefers the
 * device list when connected, otherwise falls back to localStorage.
 *
 * On a connected call, only names recorded *offline* (tracked in the pending
 * set) are migrated to the device before the local cache is refreshed —
 * otherwise reconnecting against an empty/partial device list would discard
 * those offline-only suggestions. Crucially, stale cache entries that the
 * device has simply evicted are NOT treated as offline writes, so opening Shot
 * Notes from two browsers can no longer churn the shared list back and forth.
 *
 * Each pending name is cleared only once its device save succeeds. A save that
 * fails (socket drop / device write error) leaves the name pending and keeps it
 * in the local cache so it can be retried on a later connected call instead of
 * being permanently lost. This mirrors the offline/sync intent of beanManager.
 */
export async function listGrinders(apiService) {
  const legacyGrinders = listLegacyGrinders();
  if (!hasConnectedApi(apiService)) {
    return legacyGrinders;
  }

  try {
    let deviceGrinders = await listDeviceGrinders(apiService);

    // Migrate genuinely-unsynced offline additions to the device. We only
    // touch names in the pending set (populated by recordGrinder's offline
    // branch); a name already present on the device is cleared from pending
    // without a redundant save. Idempotent and churn-free.
    const pending = listPendingGrinders();
    if (pending.length) {
      const deviceKeys = new Set(deviceGrinders.map(normalize));
      // Push oldest-first so the most-recently-used pending entry ends up
      // nearest the front of the device list after each save promotes it.
      for (const name of [...pending].reverse()) {
        if (deviceKeys.has(normalize(name))) {
          // Already on the device (e.g. another browser synced it) — just drop
          // it from our pending set, nothing to push.
          clearPendingGrinder(name);
          continue;
        }
        try {
          deviceGrinders = await saveDeviceGrinder(apiService, name);
          deviceKeys.add(normalize(name));
          // Synced successfully — it is no longer a pending offline write.
          clearPendingGrinder(name);
        } catch {
          // Save failed: keep the name pending so it can be retried, and make
          // sure it survives the cache refresh below instead of being dropped.
        }
      }
    }

    // Keep the local cache fresh so suggestions appear instantly next load.
    // Any name still pending (a failed migration) is retained on top of the
    // device list so its only local copy is not destroyed.
    const stillPending = listPendingGrinders();
    saveLegacyGrinders([...stillPending, ...deviceGrinders]);
    // Return the same merged view the cache now holds so callers don't lose
    // sight of a name whose migration is still pending.
    return sanitizeList([...stillPending, ...deviceGrinders]);
  } catch {
    return legacyGrinders;
  }
}

/**
 * Records `name` as the most-recently-used grinder. Persists to the device
 * when connected (and mirrors into localStorage); otherwise localStorage only,
 * marking the name as a pending offline write so listGrinders() can migrate it
 * to the device on the next connected call. No-ops for empty/blank names.
 * Returns the updated list.
 */
export async function recordGrinder(apiService, name) {
  const trimmed = String(name || '').trim();
  if (!trimmed) return listLegacyGrinders();

  // Always update the local cache so the value is available immediately.
  const nextLegacy = promote(listLegacyGrinders(), trimmed);
  saveLegacyGrinders(nextLegacy);

  if (!hasConnectedApi(apiService)) {
    // No device to sync to — remember this as a genuine offline addition so it
    // gets migrated (not just mirrored) once a connection is available.
    addPendingGrinder(trimmed);
    dispatchGrindersChanged(nextLegacy);
    return nextLegacy;
  }

  try {
    const response = await requestGrinders(apiService, {
      tp: 'req:grinders:save',
      name: trimmed,
    });
    const deviceGrinders = sanitizeList(response?.grinders);
    // Successfully persisted to the device — this name is now synced, so it is
    // not (or no longer) a pending offline write.
    clearPendingGrinder(trimmed);
    saveLegacyGrinders(deviceGrinders);
    dispatchGrindersChanged(deviceGrinders);
    return deviceGrinders;
  } catch {
    // Device save failed despite an open socket — treat it like an offline
    // write so the name is retried on a later connected listGrinders() call.
    addPendingGrinder(trimmed);
    dispatchGrindersChanged(nextLegacy);
    return nextLegacy;
  }
}
