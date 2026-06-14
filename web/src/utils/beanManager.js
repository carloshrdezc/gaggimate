const BEANS_STORAGE_KEY = 'gaggimate-beans';
const BEAN_SELECTION_EVENTS_KEY = 'gaggimate-bean-selection-events';
const ACTIVE_BEAN_SELECTION_KEY = 'gaggimate-active-bean-selection';
// CAR-371-style pending set of bean *ids* that were saved while offline (or
// whose online save failed). The device has never received these, so they are
// pushed on the next connected listBeans()/page load and cleared once the
// device echoes them back. This replaces the old one-shot
// `gaggimate-beans-migrated` flag + empty-device migration gate (CAR-373),
// which permanently stranded localStorage-only beans on a populated device.
const BEANS_PENDING_STORAGE_KEY = 'gaggimate-beans-pending';
// Durable one-shot flag (CAR-373 P2): the FIRST time we successfully read the
// device's authoritative list we rescue any cache-only beans that predate the
// pending-set mechanism by seeding them into the pending set. Once set, we
// NEVER re-seed from the cache again — the explicit pending set is the only
// record of genuine unsynced writes. This prevents a stale cached copy of a
// bean DELETED on another client from being resurrected on every later load,
// while still performing the full legacy rescue once regardless of whether the
// device already has beans (unlike the old gate that skipped a populated
// device entirely and stranded legacy beans — CAR-373 Bug 1).
const BEANS_LEGACY_MIGRATED_KEY = 'gaggimate-beans-legacy-migrated';
// PRE-EXISTING flag set by the OLD (pre-pending-set) migration. Browsers that
// upgraded from a version running the old migration already have this set to
// `true` AND a populated `gaggimate-beans` cache that mirrors the device list
// at the time of that migration. seedPendingFromLegacy MUST honor this as
// "already reconciled with a device" — otherwise an upgraded browser whose
// new BEANS_LEGACY_MIGRATED_KEY is unset would re-seed every cache-only id into
// pending and RESURRECT a bean deleted on another client (CAR-373 P2, upgrade
// path). See seedPendingFromLegacy for the deliberate trade-off vs. Bug 1.
const LEGACY_BEAN_MIGRATION_KEY = 'gaggimate-beans-migrated';

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
    // ignore storage failures
  }
}

function createId(prefix) {
  return `${prefix}-${Math.random().toString(36).slice(2, 10)}`;
}

function dispatchBeansChanged(detail = null) {
  if (typeof window !== 'undefined') {
    window.dispatchEvent(new CustomEvent('beans-library-changed', { detail }));
  }
}

export function parseQuantity(value) {
  if (value === '' || value === null || value === undefined) return null;
  const numeric = Number(value);
  if (!Number.isFinite(numeric) || numeric < 0) return null;
  return Math.round((numeric + Number.EPSILON) * 100) / 100;
}

// Legacy beans saved before CAR-102 used Date.now() (Unix milliseconds).
// New format is Unix seconds. Threshold 1e11 is unambiguous in practice:
// 1e11 seconds is year 5138 (no real seconds-timestamp ever reaches it),
// and 1e11 ms is 1973-03-03 (no real ms-timestamp from this project is
// ever below it). Picking the boundary here — instead of 2e9 (May 2033) —
// avoids mis-converting valid Unix-seconds values once we cross into 2033
// or when a client clock is future-skewed.
const LEGACY_BEAN_TIMESTAMP_THRESHOLD = 100000000000; // 1e11
function normalizeBeanTimestamp(value, fallback) {
  const numeric = Number(value);
  if (!Number.isFinite(numeric) || numeric < 0) return fallback;
  // Preserve the firmware's pre-NTP sentinel of 0 instead of replacing it
  // with the browser clock. Firmware's saveBean backfills on the next save
  // once NTP is valid via its `bean.createdAt == 0` guard; if we substitute
  // a client-derived timestamp here, that backfill never runs and a wrong
  // client clock gets locked into the stored record.
  if (numeric === 0) return 0;
  if (numeric > LEGACY_BEAN_TIMESTAMP_THRESHOLD) {
    return Math.floor(numeric / 1000);
  }
  return numeric;
}

function normalizeBeanPayload(beanInput = {}) {
  // Unix seconds (matches firmware BeanManager + ShotHistoryPlugin convention).
  const now = Math.floor(Date.now() / 1000);
  return {
    id: String(beanInput.id || '').trim(),
    name: String(beanInput.name || '').trim(),
    roaster: String(beanInput.roaster || '').trim(),
    roastLevel: String(beanInput.roastLevel || '').trim(),
    roastDate: String(beanInput.roastDate || '').trim(),
    origin: String(beanInput.origin || '').trim(),
    process: String(beanInput.process || '').trim(),
    notes: String(beanInput.notes || '').trim(),
    quantity: parseQuantity(beanInput.quantity),
    archived: !!beanInput.archived,
    createdAt: normalizeBeanTimestamp(beanInput.createdAt, now),
    updatedAt: normalizeBeanTimestamp(beanInput.updatedAt, now),
  };
}

function sortBeans(beans) {
  return [...beans].sort((a, b) => {
    if (!!a.archived !== !!b.archived) return a.archived ? 1 : -1;
    if ((b.updatedAt || 0) !== (a.updatedAt || 0)) return (b.updatedAt || 0) - (a.updatedAt || 0);
    return String(a.name || '').localeCompare(String(b.name || ''));
  });
}

function listLegacyBeans() {
  const beans = readJson(BEANS_STORAGE_KEY, []);
  return Array.isArray(beans) ? sortBeans(beans.map(normalizeBeanPayload)) : [];
}

function writeLegacyBeansCache(beans) {
  // Local-mirror write WITHOUT firing the public change event. Used by the
  // read paths (drainPendingBeans' no-mutation branches) so that simply
  // refreshing the cache from the authoritative device list does NOT dispatch
  // `beans-library-changed` — otherwise listeners (ShotHistory, Beans pages)
  // re-trigger listBeans() on the event, which writes the cache again, which
  // dispatches again: a self-feeding event loop on every connected, no-pending
  // read (CAR-373 P1).
  writeJson(BEANS_STORAGE_KEY, sortBeans((beans || []).map(normalizeBeanPayload)));
}

function saveLegacyBeans(beans) {
  writeLegacyBeansCache(beans);
  dispatchBeansChanged(listLegacyBeans());
}

function upsertLegacyBean(bean, { silent = false } = {}) {
  const normalizedBean = normalizeBeanPayload(bean);
  const beans = listLegacyBeans();
  const nextBeans = beans.some(existing => existing.id === normalizedBean.id)
    ? beans.map(existing => (existing.id === normalizedBean.id ? { ...existing, ...normalizedBean } : existing))
    : [normalizedBean, ...beans];
  // M2 (CAR-373 review): `silent` writes the local mirror WITHOUT dispatching
  // `beans-library-changed`. Callers that batch many writes and dispatch once
  // themselves (e.g. restoreBeanData via saveBean(..., { suppressEvent: true }))
  // need the per-bean local persist to stay quiet so a restore of N beans does
  // not fire N redundant refreshes. Default (silent=false) keeps the original
  // dispatch-on-write behavior.
  if (silent) {
    writeLegacyBeansCache(nextBeans);
  } else {
    saveLegacyBeans(nextBeans);
  }
  return normalizedBean;
}

function removeLegacyBean(beanId) {
  const nextBeans = listLegacyBeans().filter(bean => bean.id !== beanId);
  saveLegacyBeans(nextBeans);
  return nextBeans;
}

// --- Pending offline beans ----------------------------------------------------
//
// A set of bean *ids* that were created/updated while no device connection was
// available (or whose online save threw). Unlike grinders (a capped, eviction-
// prone string list), beans are uncapped and id-keyed, so dedup/merge is a
// clean by-id operation and there is no eviction to model. A pending id is
// cleared only once the device echoes that bean back from req:beans:list,
// guaranteeing the write actually landed; a failed push leaves the id pending
// so it is retried on the next connected call. The matching bean payload lives
// in the localStorage mirror (BEANS_STORAGE_KEY), so the pending set only needs
// to track ids.

function listPendingBeanIds() {
  const ids = readJson(BEANS_PENDING_STORAGE_KEY, []);
  if (!Array.isArray(ids)) return [];
  // De-dup defensively and drop blanks.
  return [...new Set(ids.map(id => String(id || '').trim()).filter(Boolean))];
}

function savePendingBeanIds(ids) {
  writeJson(
    BEANS_PENDING_STORAGE_KEY,
    [...new Set((Array.isArray(ids) ? ids : []).map(id => String(id || '').trim()).filter(Boolean))],
  );
}

// Mark a bean id as a pending offline write.
function addPendingBeanId(beanId) {
  const id = String(beanId || '').trim();
  if (!id) return;
  const ids = listPendingBeanIds();
  if (ids.includes(id)) return;
  savePendingBeanIds([...ids, id]);
}

// Drop a single bean id from the pending set once it is confirmed on device.
function clearPendingBeanId(beanId) {
  const id = String(beanId || '').trim();
  if (!id) return;
  savePendingBeanIds(listPendingBeanIds().filter(existing => existing !== id));
}

function hasConnectedApi(apiService) {
  return !!(apiService?.socket && apiService.socket.readyState === WebSocket.OPEN);
}

async function requestBeans(apiService, payload) {
  if (!apiService) return null;
  const response = await apiService.request(payload);
  if (response?.error) {
    throw new Error(response.error);
  }
  return response;
}

async function listDeviceBeans(apiService) {
  const response = await requestBeans(apiService, { tp: 'req:beans:list' });
  const beans = Array.isArray(response?.beans) ? response.beans.map(normalizeBeanPayload) : [];
  return sortBeans(beans);
}

// Push every pending bean to the device, then refresh from the authoritative
// device list. The device (BeanManager) owns id/timestamp echo, sort and dedup,
// so the client never models the merge: any pending id that comes back in the
// refreshed list is confirmed synced and cleared; ids that did not land (push
// threw, or device omitted them) stay pending for the next attempt.
//
// Returns the authoritative device bean list. On a transient error mid-drain it
// falls back to merging the still-pending local beans on top of the last known
// device list so nothing is lost from the UI and the ids remain pending.
async function drainPendingBeans(apiService) {
  const pendingIds = listPendingBeanIds();

  if (!pendingIds.length) {
    // Nothing to migrate — just return the authoritative device list. This is a
    // pure READ: refresh the local mirror but do NOT dispatch the change event
    // (would re-trigger listeners' listBeans() -> infinite loop, CAR-373 P1).
    const deviceBeans = await listDeviceBeans(apiService);
    writeLegacyBeansCache(deviceBeans);
    return deviceBeans;
  }

  // Resolve the pending ids to their stored bean payloads (the localStorage
  // mirror is the source of truth for the bean body). Ids without a stored
  // bean are stale — drop them so they cannot be retried forever.
  const localBeans = listLegacyBeans();
  const localById = new Map(localBeans.map(bean => [bean.id, bean]));
  const beansToPush = pendingIds.map(id => localById.get(id)).filter(Boolean);
  const unresolved = pendingIds.filter(id => !localById.has(id));
  if (unresolved.length) {
    savePendingBeanIds(pendingIds.filter(id => localById.has(id)));
  }

  if (!beansToPush.length) {
    // All pending ids were stale (no stored payload) — nothing to push. Pure
    // READ: refresh the mirror silently (no change event), CAR-373 P1.
    const deviceBeans = await listDeviceBeans(apiService);
    writeLegacyBeansCache(deviceBeans);
    return deviceBeans;
  }

  // Push each pending bean and CAPTURE the device's echoed canonical bean.
  // The firmware clears unsafe legacy/imported ids on parse and regenerates a
  // new safe id via generateShortID(), so req:beans:save's response carries a
  // DIFFERENT id than the one we pushed (and the original id will never appear
  // in req:beans:list). We therefore record an original-id -> canonical-bean
  // mapping so the original pending id can be cleared and the stale original
  // payload replaced even when the device regenerated the id. A push that
  // throws (or echoes no canonical bean) leaves its id pending for retry.
  // Mirrors the interactive saveBean() reconciliation (response.bean capture).
  const canonicalByOriginalId = new Map();
  for (const bean of beansToPush) {
    try {
      const response = await requestBeans(apiService, { tp: 'req:beans:save', bean });
      if (response?.bean) {
        canonicalByOriginalId.set(bean.id, normalizeBeanPayload(response.bean));
      }
    } catch {
      // Leave this id pending; it is retried on the next connected call.
    }
  }

  let deviceBeans;
  try {
    deviceBeans = await listDeviceBeans(apiService);
  } catch {
    // Could not confirm — keep all pending ids and surface a best-effort merge
    // (pending local beans on top of whatever we can still read) so the UI does
    // not lose the offline-created beans. Where the device DID echo a canonical
    // bean (id possibly regenerated), prefer that canonical payload over the
    // stale original so a successfully-regenerated bean is not stranded.
    const fallbackDevice = await listDeviceBeans(apiService).catch(() => []);
    const fallbackIds = new Set(fallbackDevice.map(bean => bean.id));
    const fallbackPending = beansToPush
      .map(bean => canonicalByOriginalId.get(bean.id) || bean)
      .filter(bean => !fallbackIds.has(bean.id));
    const merged = sortBeans([...fallbackPending, ...fallbackDevice]);
    saveLegacyBeans(merged);
    return merged;
  }

  // Reconcile each pushed bean against the authoritative device list. A pushed
  // bean is CONFIRMED synced if either:
  //   (a) its original id appears in the refreshed device list, OR
  //   (b) the device echoed a canonical bean for it (id regenerated) — in which
  //       case the original id will NOT appear in the list, so we clear the
  //       ORIGINAL pending id and drop the stale original-id payload from the
  //       local mirror so the regenerated bean (already in the device list) is
  //       neither shadowed nor re-pushed on a later drain (idempotency).
  // Only genuinely-failed pushes (threw -> no canonical echo AND original id not
  // in the device list) stay pending with their original payload for retry.
  const deviceIds = new Set(deviceBeans.map(bean => bean.id));
  const confirmedOriginalIds = new Set();
  const staleOriginalIds = new Set();
  const stillPending = [];
  for (const bean of beansToPush) {
    if (deviceIds.has(bean.id)) {
      // Confirmed under its original id (device kept it).
      confirmedOriginalIds.add(bean.id);
      continue;
    }
    if (canonicalByOriginalId.has(bean.id)) {
      // Confirmed under a regenerated id — the original payload is now stale.
      confirmedOriginalIds.add(bean.id);
      staleOriginalIds.add(bean.id);
      continue;
    }
    // Save failed and not present on device — keep pending for retry.
    stillPending.push(bean);
  }

  // Clear pending ids for every confirmed bean (original ids), keeping only the
  // genuinely-failed ones for a later retry.
  savePendingBeanIds(listPendingBeanIds().filter(id => !confirmedOriginalIds.has(id)));

  // Rebuild the local mirror: device list is authoritative; drop any stale
  // original-id payloads (their regenerated bean already lives in deviceBeans),
  // and keep only the genuinely-still-pending originals on top for retry.
  const survivingPending = stillPending.filter(bean => !staleOriginalIds.has(bean.id));
  const merged = survivingPending.length
    ? sortBeans([...survivingPending, ...deviceBeans])
    : deviceBeans;
  // An ACTUAL mutation happened only if at least one pushed bean was confirmed
  // synced to the device (its pending id cleared / stale payload dropped). In
  // that case fire the public change event so listeners refresh. If nothing was
  // confirmed (every push failed and merely stays pending), this is effectively
  // a no-op read of the device list — write the cache SILENTLY so we do not
  // self-trigger the listener loop (CAR-373 P1).
  if (confirmedOriginalIds.size) {
    saveLegacyBeans(merged);
  } else {
    writeLegacyBeansCache(merged);
  }
  return merged;
}

// One-time legacy rescue: on the FIRST connected load only, seed the pending
// set from cache-only beans (ids absent from the device). The single
// `gaggimate-beans` key serves double duty — it is BOTH the legacy store AND
// the offline cache of the device-authoritative list — so "absent from device"
// alone cannot distinguish a never-synced legacy bean (must migrate) from a
// cached copy of a bean DELETED on another client (must NOT be resurrected).
// We therefore only trust the cache for this rescue ONCE, then set a durable
// flag so subsequent loads rely solely on the explicit pending set (the only
// reliable record of genuine unsynced writes). After the flag is set, a bean
// deleted elsewhere but still in this browser's cache is simply overwritten by
// the authoritative device list and not re-pushed (CAR-373 P2).
//
// Unlike the OLD gate (which skipped migration entirely when the device was
// non-empty and stranded legacy beans — CAR-373 Bug 1), this performs the full
// legacy seed-and-drain ONCE regardless of device population; it only prevents
// RE-seeding cache-only ids on every later load. Idempotent: a bean already on
// the device is never seeded; an id already pending is not duplicated.
function seedPendingFromLegacy(deviceBeans) {
  // Treat EITHER migration flag as "this browser has already reconciled with a
  // device" and skip the cache-based rescue:
  //   - BEANS_LEGACY_MIGRATED_KEY: the new durable one-shot flag (set below).
  //   - LEGACY_BEAN_MIGRATION_KEY ('gaggimate-beans-migrated'): the PRE-EXISTING
  //     flag from the OLD migration. An upgraded browser that ran the old
  //     migration already has this true plus a `gaggimate-beans` cache that
  //     mirrored the device list at migration time. Without consulting it,
  //     the new flag (unset on first load after upgrade) would let us re-seed
  //     every cache-only id into pending and RESURRECT a bean DELETED on
  //     another client — the same CAR-373 P2 class, via the upgrade path.
  //
  // Deliberate trade-off (do NOT re-introduce CAR-373 Bug 1): the old flag can
  // mean either (a) migration genuinely completed against an empty device
  // (cache == device mirror; resurrecting would corrupt shared device state for
  // all clients on the COMMON deletion path) OR (b) the old Bug-1 race fired and
  // stranded legacy beans (narrow first-connect-to-a-populated-device race).
  // localStorage cannot distinguish them, so we prefer NOT resurrecting:
  // resurrection is unrecoverable corruption on the common path; a rare stranded
  // Bug-1 bean is recoverable (the user re-adds it, and any bean they
  // create/edit on this version goes through saveBean -> the explicit pending
  // set and syncs correctly). We never trust a stale cache to push writes once
  // ANY migration flag indicates this browser already reconciled with a device.
  // Only a genuinely-fresh browser (NEITHER flag set) gets the one-time rescue.
  if (
    readJson(BEANS_LEGACY_MIGRATED_KEY, false) === true ||
    readJson(LEGACY_BEAN_MIGRATION_KEY, false) === true
  ) {
    // Already reconciled (old or new flag) — never (re-)seed from the cache.
    // Set the new flag so subsequent loads short-circuit on it directly.
    writeJson(BEANS_LEGACY_MIGRATED_KEY, true);
    return;
  }
  const deviceIds = new Set((deviceBeans || []).map(bean => bean.id));
  for (const bean of listLegacyBeans()) {
    if (!bean.id) continue;
    if (deviceIds.has(bean.id)) continue;
    addPendingBeanId(bean.id);
  }
  // Mark the durable one-shot flag so this cache-based rescue never runs again.
  writeJson(BEANS_LEGACY_MIGRATED_KEY, true);
}

/**
 * Loads the bean library, migrating any localStorage-only beans to the device.
 * Kept as the page-load entry point (Beans/index.jsx) for backward
 * compatibility; it now defers entirely to the pending-set drain instead of the
 * old empty-device + one-shot-flag migration gate (CAR-373).
 *
 * Idempotent across repeated calls: beans already on the device are never
 * re-pushed (id-keyed dedup, device authoritative), and confirmed beans are
 * removed from the pending set.
 */
export async function migrateLegacyBeansToDevice(apiService) {
  const legacyBeans = listLegacyBeans();
  if (!hasConnectedApi(apiService)) {
    return legacyBeans;
  }

  let deviceBeans;
  try {
    deviceBeans = await listDeviceBeans(apiService);
  } catch {
    return legacyBeans;
  }

  // Mark any local-only beans as pending, then drain the pending set to the
  // device and refresh from the authoritative list.
  seedPendingFromLegacy(deviceBeans);

  try {
    // M1 (CAR-373 review): drainPendingBeans is the SINGLE owner of the
    // `beans-library-changed` dispatch. It already fires exactly once when it
    // confirms a real sync (its `confirmedOriginalIds.size` branch calls
    // saveLegacyBeans -> dispatch) and stays SILENT on a pure no-op read (it
    // calls writeLegacyBeansCache, no dispatch — preserving CAR-373 P1). We
    // therefore do NOT dispatch again here: the previous unconditional
    // dispatchBeansChanged() double-fired on every migration that synced >=1
    // bean (one from drain, one from here), making listeners run two redundant
    // req:beans:list round-trips. Letting drain own the single dispatch yields:
    // migration that synced beans -> exactly one event; migration/read that
    // synced nothing -> zero events.
    return await drainPendingBeans(apiService);
  } catch {
    return legacyBeans;
  }
}

export async function listBeans(apiService) {
  const legacyBeans = listLegacyBeans();
  if (!hasConnectedApi(apiService)) {
    return legacyBeans;
  }

  try {
    // Drain any pending offline/failed beans to the device, then return the
    // authoritative device list. This is what lets a bean created while
    // disconnected reach the device on the next connected list/page load.
    return await drainPendingBeans(apiService);
  } catch {
    return legacyBeans;
  }
}

export async function exportBeanData(apiService) {
  return {
    beans: await listBeans(apiService),
    selectionEvents: readJson(BEAN_SELECTION_EVENTS_KEY, []),
    activeSelection: getCurrentBeanSelection(),
  };
}

export async function restoreBeanData(apiService, data) {
  const nextBeans = Array.isArray(data?.beans) ? data.beans.map(normalizeBeanPayload) : [];

  if (apiService) {
    for (const bean of nextBeans) {
      await saveBean(apiService, bean, { suppressEvent: true });
    }
    dispatchBeansChanged();
  } else {
    saveLegacyBeans(nextBeans);
  }

  writeJson(
    BEAN_SELECTION_EVENTS_KEY,
    Array.isArray(data?.selectionEvents) ? data.selectionEvents : [],
  );
  writeJson(ACTIVE_BEAN_SELECTION_KEY, data?.activeSelection || null);

  if (typeof window !== 'undefined') {
    window.dispatchEvent(
      new CustomEvent('bean-selection-changed', { detail: data?.activeSelection || null }),
    );
  }
}

export async function saveBean(apiService, beanInput, options = {}) {
  const bean = normalizeBeanPayload({
    ...beanInput,
    id: beanInput.id || createId('bean'),
    updatedAt: Math.floor(Date.now() / 1000),
  });

  if (!bean.name) return null;

  if (!hasConnectedApi(apiService)) {
    // No device to sync to — persist locally and remember the id as a pending
    // offline write so listBeans()/migrateLegacyBeansToDevice() pushes it on
    // the next connected call (CAR-373 Bug 2). M2 (CAR-373 review): honor
    // options.suppressEvent — when a batch caller (restoreBeanData) sets it,
    // the local persist must NOT dispatch per bean; the caller dispatches once.
    const saved = upsertLegacyBean(bean, { silent: !!options.suppressEvent });
    addPendingBeanId(saved.id);
    return saved;
  }

  try {
    const response = await requestBeans(apiService, { tp: 'req:beans:save', bean });
    const savedBean = normalizeBeanPayload(response?.bean || bean);
    // Persisted on the device — this id is synced, so it is not (or no longer)
    // a pending offline write.
    clearPendingBeanId(savedBean.id);
    clearPendingBeanId(bean.id);
    if (!options.deviceOnly) {
      upsertLegacyBean(savedBean);
    }
    if (!options.suppressEvent) {
      dispatchBeansChanged(savedBean);
    }
    return savedBean;
  } catch (error) {
    if (options.deviceOnly) {
      throw error;
    }
    // Online save failed despite an open socket — persist locally and mark the
    // id pending so it is retried, instead of being silently dropped
    // (CAR-373 Bug 3). M2 (CAR-373 review): honor options.suppressEvent so a
    // batch caller (restoreBeanData) does not fire a per-bean event when its
    // online save fails mid-restore; the caller dispatches once afterward.
    const saved = upsertLegacyBean(bean, { silent: !!options.suppressEvent });
    addPendingBeanId(saved.id);
    return saved;
  }
}

export async function removeBean(apiService, beanId, options = {}) {
  if (!hasConnectedApi(apiService)) {
    const nextBeans = removeLegacyBean(beanId);
    // A bean removed while offline must not be resurrected by a stale pending
    // entry on the next connected drain.
    clearPendingBeanId(beanId);
    const activeBean = getCurrentBeanSelection();
    if (activeBean?.beanId === beanId) {
      clearCurrentBeanSelection();
    }
    return nextBeans;
  }

  try {
    await requestBeans(apiService, { tp: 'req:beans:delete', id: beanId });
    removeLegacyBean(beanId);
    clearPendingBeanId(beanId);
  } catch (error) {
    if (options.deviceOnly) {
      throw error;
    }
    const nextBeans = removeLegacyBean(beanId);
    clearPendingBeanId(beanId);
    const activeBean = getCurrentBeanSelection();
    if (activeBean?.beanId === beanId) {
      clearCurrentBeanSelection();
    }
    return nextBeans;
  }

  const activeBean = getCurrentBeanSelection();
  if (activeBean?.beanId === beanId) {
    clearCurrentBeanSelection();
  }
  const beans = await listBeans(apiService);
  if (!options.suppressEvent) {
    dispatchBeansChanged(beans);
  }
  return beans;
}

export async function syncBeanUsageFromNotes(apiService, previousNotes, nextNotes) {
  if (!apiService) return null;

  const beans = await listBeans(apiService);
  const previousDose = parseQuantity(previousNotes?.doseIn) || 0;
  const nextDose = parseQuantity(nextNotes?.doseIn) || 0;

  const resolveBean = notes => {
    const beanId = String(notes?.beanId || '').trim();
    const beanType = normalize(notes?.beanType);
    if (beanId) {
      return beans.find(bean => bean.id === beanId) || null;
    }
    if (!beanType) return null;
    return beans.find(bean => normalize(bean.name) === beanType) || null;
  };

  const previousBean = resolveBean(previousNotes);
  const nextBean = resolveBean(nextNotes);

  const updates = new Map();

  const queueAdjustment = (bean, delta) => {
    if (!bean || !Number.isFinite(delta) || delta === 0) return;
    const current = updates.get(bean.id) || { ...bean };
    const currentQuantity = parseQuantity(current.quantity);
    if (currentQuantity === null) return;
    current.quantity = Math.max(0, Math.round((currentQuantity + delta + Number.EPSILON) * 100) / 100);
    updates.set(bean.id, current);
  };

  if (previousBean?.id && nextBean?.id && previousBean.id === nextBean.id) {
    queueAdjustment(nextBean, previousDose - nextDose);
  } else {
    queueAdjustment(previousBean, previousDose);
    queueAdjustment(nextBean, -nextDose);
  }

  for (const bean of updates.values()) {
    await saveBean(apiService, bean, { suppressEvent: true });
  }

  if (updates.size > 0) {
    dispatchBeansChanged();
  }

  return nextBean || null;
}

export function getLastBeanSelectionForProfile(profile) {
  const profileId = String(profile?.id || profile?.profileId || '');
  const profileName = normalize(profile?.label || profile?.name || profile?.profileLabel || '');
  const events = readJson(BEAN_SELECTION_EVENTS_KEY, []);

  return events
    .filter(event => {
      if (profileId && String(event.profileId || '') === profileId) return true;
      return profileName && normalize(event.profileLabel) === profileName;
    })
    .sort((a, b) => Number(b.selectedAtMs || 0) - Number(a.selectedAtMs || 0))[0];
}

export function recordBeanSelection({ profileId, profileLabel, bean }) {
  if (!bean?.id || !bean?.name) return null;

  const events = readJson(BEAN_SELECTION_EVENTS_KEY, []);
  const nextEvent = {
    id: createId('bean-selection'),
    profileId: String(profileId || ''),
    profileLabel: String(profileLabel || ''),
    beanId: bean.id,
    beanName: bean.name,
    beanRoaster: bean.roaster || '',
    beanOrigin: bean.origin || '',
    beanProcess: bean.process || '',
    selectedAtMs: Date.now(),
  };

  const nextEvents = [nextEvent, ...events].slice(0, 500);
  writeJson(BEAN_SELECTION_EVENTS_KEY, nextEvents);
  writeJson(ACTIVE_BEAN_SELECTION_KEY, nextEvent);
  if (typeof window !== 'undefined') {
    window.dispatchEvent(new CustomEvent('bean-selection-changed', { detail: nextEvent }));
  }
  return nextEvent;
}

export function getCurrentBeanSelection() {
  return readJson(ACTIVE_BEAN_SELECTION_KEY, null);
}

export function clearCurrentBeanSelection() {
  writeJson(ACTIVE_BEAN_SELECTION_KEY, null);
  if (typeof window !== 'undefined') {
    window.dispatchEvent(new CustomEvent('bean-selection-changed', { detail: null }));
  }
}

function resolveSelectionEventForShot(shot) {
  const events = readJson(BEAN_SELECTION_EVENTS_KEY, []);
  const shotProfile = normalize(shot?.profile || shot?.profileName || '');
  const shotTimestampMs = Number(shot?.timestamp || 0) * 1000;

  if (!shotProfile || !Number.isFinite(shotTimestampMs) || shotTimestampMs <= 0) {
    return null;
  }

  return (
    events
      .filter(event => normalize(event.profileLabel) === shotProfile)
      .filter(event => Number(event.selectedAtMs || 0) <= shotTimestampMs)
      .sort((a, b) => Number(b.selectedAtMs || 0) - Number(a.selectedAtMs || 0))[0] || null
  );
}

export function inferBeanForShot(shot) {
  if (shot?.beanName) return shot.beanName;
  if (shot?.beanType) return shot.beanType;
  if (shot?.notes?.beanType) return shot.notes.beanType;
  return resolveSelectionEventForShot(shot)?.beanName || '';
}

export function inferBeanIdForShot(shot) {
  if (shot?.beanId) return shot.beanId;
  if (shot?.notes?.beanId) return shot.notes.beanId;
  return resolveSelectionEventForShot(shot)?.beanId || '';
}
