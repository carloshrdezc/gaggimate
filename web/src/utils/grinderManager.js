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
// Maximum accepted grinder-name length, in UTF-8 BYTES, to match the firmware
// (Arduino `String::length()` counts bytes, not characters). We must measure
// bytes here too: JavaScript's `String.length` counts UTF-16 code units, so a
// name of accented/multibyte characters could pass a code-unit check yet exceed
// the firmware's 64-byte limit — it would be accepted into pending storage,
// rejected on every device save, and retried forever without ever syncing.
const GRINDER_NAME_MAX_LEN = 64;

// UTF-8 byte length of a string, mirroring the firmware's byte-based limit.
// Uses TextEncoder when available (browsers/jsdom); falls back to a manual
// code-point byte count (incl. surrogate-pair handling) otherwise.
function utf8ByteLength(text) {
  const str = String(text || '');
  if (typeof TextEncoder !== 'undefined') {
    return new TextEncoder().encode(str).length;
  }
  let bytes = 0;
  for (let i = 0; i < str.length; i++) {
    const code = str.charCodeAt(i);
    if (code < 0x80) bytes += 1;
    else if (code < 0x800) bytes += 2;
    else if (code >= 0xd800 && code <= 0xdbff) {
      // High surrogate: a full code point (4 UTF-8 bytes); skip the low half.
      bytes += 4;
      i++;
    } else bytes += 3;
  }
  return bytes;
}

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
    if (!name || utf8ByteLength(name) > GRINDER_NAME_MAX_LEN) continue;
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
  if (!trimmed || utf8ByteLength(trimmed) > GRINDER_NAME_MAX_LEN) return;
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

// Clear the entire pending set. Used after a successful batch migration, where
// the device's returned list is authoritative for everything we could sync.
function clearAllPendingGrinders() {
  savePendingGrinders([]);
}

// Move `name` to the front of the list, deduplicating case-insensitively.
function promote(grinders, name) {
  const trimmed = String(name || '').trim();
  if (!trimmed || utf8ByteLength(trimmed) > GRINDER_NAME_MAX_LEN) return sanitizeList(grinders);
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

// Push one or more names to the device in a single request and return the
// canonical list the device produces. The device owns the merge/dedup/cap, so
// the client never has to model eviction. `names` is sent oldest-first so the
// most-recently-used entry ends up nearest the front of the device list.
async function saveDeviceGrinders(apiService, names) {
  const response = await requestGrinders(apiService, { tp: 'req:grinders:save', names });
  return sanitizeList(response?.grinders);
}

/**
 * Returns the saved grinder names, most-recently-used first. Prefers the
 * device list when connected, otherwise falls back to localStorage.
 *
 * On a connected call, any names recorded *offline* (tracked in the pending
 * set) are migrated to the device in ONE batch save. The device performs the
 * merge/dedup/cap and returns the canonical list — the client does not model
 * which entry the cap evicts, which removes the whole class of eviction bugs.
 * On success the entire pending set is cleared and the cache is replaced with
 * the device's list. If the batch fails (socket drop / device error) the
 * pending names are kept (and merged on top of the cache) so they survive and
 * are retried on the next connected call.
 */
export async function listGrinders(apiService) {
  const legacyGrinders = listLegacyGrinders();
  if (!hasConnectedApi(apiService)) {
    return legacyGrinders;
  }

  try {
    const pending = listPendingGrinders();

    if (!pending.length) {
      // Nothing to migrate — just refresh from the authoritative device list.
      const deviceGrinders = await listDeviceGrinders(apiService);
      saveLegacyGrinders(deviceGrinders);
      return deviceGrinders;
    }

    // Migrate all pending names in a single batch. Send oldest-first so the
    // most-recently-used pending entry ends up nearest the front.
    let deviceGrinders;
    try {
      deviceGrinders = await saveDeviceGrinders(apiService, [...pending].reverse());
    } catch {
      // Batch failed: keep pending intact and merge it on top of the last
      // known device list so the offline names are not lost and are retried.
      const fallbackDevice = await listDeviceGrinders(apiService).catch(() => []);
      const merged = sanitizeList([...pending, ...fallbackDevice]);
      saveLegacyGrinders(merged);
      return merged;
    }

    // Batch succeeded: the device list is now authoritative and includes
    // everything we could migrate, so the pending set is fully resolved.
    clearAllPendingGrinders();
    saveLegacyGrinders(deviceGrinders);
    return deviceGrinders;
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

// --- Grinder selection events (PRO-425) --------------------------------------
//
// Mirrors beanManager's recordBeanSelection / resolveSelectionEventForShot /
// inferBeanForShot: a per-browser localStorage log of "which grinder + grind
// setting was selected on the dashboard, for which profile, when". Shot Notes
// uses it to PRE-FILL (not lock) the grinder / grindSetting fields for a shot
// pulled by the same profile after the selection. The device does not persist
// a per-shot grinder, so — like beans' legacy fallback — this log is the only
// source for the auto-fill until firmware records it on the shot itself.

const GRINDER_SELECTION_EVENTS_KEY = 'gaggimate-grinder-selection-events';
const ACTIVE_GRINDER_SELECTION_KEY = 'gaggimate-active-grinder-selection';

function createSelectionId() {
  return `grinder-selection-${Math.random().toString(36).slice(2, 10)}`;
}

/**
 * Records a dashboard grinder selection for the active profile. `grinder` is
 * the selected grinder name; `grindSetting` is the human-readable current
 * grind-target label (e.g. "18g" or "28s") captured at selection time so a
 * later shot's notes can default to it. No-ops for a blank grinder name.
 * Returns the recorded event (or null).
 */
export function recordGrinderSelection({ profileId, profileLabel, grinder, grindSetting }) {
  const name = String(grinder || '').trim();
  if (!name) return null;

  const events = readJson(GRINDER_SELECTION_EVENTS_KEY, []);
  const nextEvent = {
    id: createSelectionId(),
    profileId: String(profileId || ''),
    profileLabel: String(profileLabel || ''),
    grinder: name,
    grindSetting: String(grindSetting || ''),
    selectedAtMs: Date.now(),
  };

  const nextEvents = [nextEvent, ...(Array.isArray(events) ? events : [])].slice(0, 500);
  writeJson(GRINDER_SELECTION_EVENTS_KEY, nextEvents);
  writeJson(ACTIVE_GRINDER_SELECTION_KEY, nextEvent);
  if (typeof window !== 'undefined') {
    window.dispatchEvent(new CustomEvent('grinder-selection-changed', { detail: nextEvent }));
  }
  return nextEvent;
}

export function getCurrentGrinderSelection() {
  return readJson(ACTIVE_GRINDER_SELECTION_KEY, null);
}

// --- Manual grind-setting log (PRO-431) --------------------------------------
//
// A per-browser localStorage log of the manual grinder-dial setting the user
// typed into the standby dashboard's GRIND field, keyed to the active profile.
// This is the physical grinder dial number (decimals, clicks, worm-drive, …) —
// UX history only, NEVER sent to the device (mirrors the DOSE_KEY localStorage
// seed in DashboardMerged, minus the `req:dose:set` device send).
//
// It has the SAME shape as the grinder-selection log above but a SEPARATE key,
// so a manually-entered dial number takes clean PRECEDENCE over the machine
// grind-TARGET label (formatGrindTarget, recorded in the selection log) when a
// shot's Shot Notes grindSetting is pre-filled — see inferGrindSettingForShot.

const MANUAL_GRIND_SETTING_EVENTS_KEY = 'gaggimate-manual-grind-setting-events';

function createManualGrindId() {
  return `manual-grind-${Math.random().toString(36).slice(2, 10)}`;
}

/**
 * Records the manually-entered grinder-dial setting for the active profile so a
 * later shot of that profile pre-fills its Shot Notes grindSetting with it.
 * `grindSetting` is the raw number the user typed (stored as a string, matching
 * the selection log). No-ops for a blank/whitespace value. Per-browser
 * localStorage only — never touches the device. Returns the recorded event (or
 * null).
 */
export function recordManualGrindSetting({ profileId, profileLabel, grindSetting }) {
  const value = String(grindSetting ?? '').trim();
  if (!value) return null;

  const events = readJson(MANUAL_GRIND_SETTING_EVENTS_KEY, []);
  const nextEvent = {
    id: createManualGrindId(),
    profileId: String(profileId || ''),
    profileLabel: String(profileLabel || ''),
    grindSetting: value,
    selectedAtMs: Date.now(),
  };

  const nextEvents = [nextEvent, ...(Array.isArray(events) ? events : [])].slice(0, 500);
  writeJson(MANUAL_GRIND_SETTING_EVENTS_KEY, nextEvents);
  if (typeof window !== 'undefined') {
    window.dispatchEvent(new CustomEvent('manual-grind-setting-changed', { detail: nextEvent }));
  }
  return nextEvent;
}

// Resolve the most recent manual grind-setting event that (a) matches the
// shot's profile and (b) happened at or before the shot was pulled. Same
// profile/timestamp matching as resolveGrinderSelectionForShot.
function resolveManualGrindSettingForShot(shot) {
  const events = readJson(MANUAL_GRIND_SETTING_EVENTS_KEY, []);
  const shotProfile = normalize(shot?.profile || shot?.profileName || '');
  const shotTimestampMs = Number(shot?.timestamp || 0) * 1000;

  if (!shotProfile || !Number.isFinite(shotTimestampMs) || shotTimestampMs <= 0) {
    return null;
  }

  return (
    (Array.isArray(events) ? events : [])
      .filter(event => normalize(event.profileLabel) === shotProfile)
      .filter(event => Number(event.selectedAtMs || 0) <= shotTimestampMs)
      .sort((a, b) => Number(b.selectedAtMs || 0) - Number(a.selectedAtMs || 0))[0] || null
  );
}

// Resolve the most recent grinder-selection event that (a) matches the shot's
// profile and (b) happened at or before the shot was pulled. Mirrors
// beanManager.resolveSelectionEventForShot exactly.
function resolveGrinderSelectionForShot(shot) {
  const events = readJson(GRINDER_SELECTION_EVENTS_KEY, []);
  const shotProfile = normalize(shot?.profile || shot?.profileName || '');
  const shotTimestampMs = Number(shot?.timestamp || 0) * 1000;

  if (!shotProfile || !Number.isFinite(shotTimestampMs) || shotTimestampMs <= 0) {
    return null;
  }

  return (
    (Array.isArray(events) ? events : [])
      .filter(event => normalize(event.profileLabel) === shotProfile)
      .filter(event => Number(event.selectedAtMs || 0) <= shotTimestampMs)
      .sort((a, b) => Number(b.selectedAtMs || 0) - Number(a.selectedAtMs || 0))[0] || null
  );
}

// PRE-FILL default for a shot's `grinder` notes field. The DEVICE-RECORDED
// grinder wins over the per-browser localStorage guess: PRO-428 firmware now
// stamps the selected grinder NAME into a shot's notes under `grinderName`
// (name-only — Settings stores grinder by name, there is no grinder-id analog),
// so that authoritative value must outrank the localStorage selection-event log.
// Precedence: device-recorded notes.grinderName -> shot.grinderName ->
// existing user-entered notes.grinder -> shot.grinder -> localStorage guess.
export function inferGrinderForShot(shot) {
  if (shot?.notes?.grinderName) return shot.notes.grinderName;
  if (shot?.grinderName) return shot.grinderName;
  if (shot?.notes?.grinder) return shot.notes.grinder;
  if (shot?.grinder) return shot.grinder;
  return resolveGrinderSelectionForShot(shot)?.grinder || '';
}

// PRO-430: true when the grinder on this shot was recorded by the DEVICE
// (the PRO-428 firmware `grinderName` field — or an explicit shot grinder —
// present on the shot record or its notes), rather than guessed from the
// per-browser localStorage selection-event log. Mirrors isBeanRecordedForShot:
// only a device-recorded grinder is authoritative, never the localStorage
// fallback. `grinderName` is the firmware contract stamped by PRO-428.
export function isGrinderRecordedForShot(shot) {
  return Boolean(
    shot?.notes?.grinderName || shot?.grinderName || shot?.notes?.grinder || shot?.grinder,
  );
}

// PRE-FILL default for a shot's `grindSetting` notes field.
//
// Precedence (PRO-431): a saved note always wins; then an explicit value on the
// shot; then the MANUAL grinder-dial number the user typed on the dashboard
// (recordManualGrindSetting) — this deliberately outranks the machine
// grind-TARGET label so a real dial setting is preferred over the auto-grind
// target; finally, fall back to the machine target label captured in the
// grinder-selection log when no manual value was entered.
export function inferGrindSettingForShot(shot) {
  if (shot?.notes?.grindSetting) return shot.notes.grindSetting;
  if (shot?.grindSetting) return shot.grindSetting;
  const manual = resolveManualGrindSettingForShot(shot)?.grindSetting;
  if (manual) return manual;
  return resolveGrinderSelectionForShot(shot)?.grindSetting || '';
}
