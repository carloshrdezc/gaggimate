// Pure transforms + the import orchestrator for the off-device profile
// export/import round-trip.
//
// This is the migration story for the SPIFFS->LittleFS upgrade (PRO-218): a
// user exports all profiles OFF the device before flashing, the new firmware
// clean-formats LittleFS and seeds the Default profile, then the user
// re-imports the exported file. Because the data lives off-device across the
// format boundary, a silent on-device wipe is structurally impossible.
//
// The leaf transforms (toExportProfile / buildExportPayload /
// remapImportedProfile) AND the import orchestrator (importProfiles) live here
// so the round-trip is unit-tested against the SHIPPED algorithm: both the real
// onUpload handler (wiring apiService.request adapters) and the tests (wiring a
// device simulator) call importProfiles. If onUpload re-implemented the loop
// inline, green tests could no longer prove the product's behaviour (PRO-218
// P1-2).

/**
 * Strip device-local state from a profile for export.
 *
 * DENYLIST (PRO-218 P3-3): everything is exported EXCEPT the per-device runtime
 * flags listed in DEVICE_LOCAL_FIELDS. We intentionally use a denylist rather
 * than an allowlist of portable fields: this is a data-preservation BACKUP, so
 * the failure we must avoid is silently dropping a profile field the user cares
 * about. An allowlist would drop any future portable field that isn't added to
 * it; a denylist only ever risks leaking a new device-local flag (cosmetic, and
 * harmlessly stripped again by the firmware on import). `id` is kept so a
 * re-imported profile stays addressable (deletable/selectable/favoritable).
 *
 * KEEP IN SYNC: when the firmware adds a new per-device runtime flag (a field
 * that is meaningful only on the device that set it and must NOT travel in a
 * backup), add it to DEVICE_LOCAL_FIELDS below.
 *
 * @param {object} profile a profile as returned by `req:profiles:list`
 * @returns {object} the profile minus device-local fields
 */
export const DEVICE_LOCAL_FIELDS = ['selected', 'favorite'];

export function toExportProfile(profile) {
  const rest = { ...(profile ?? {}) };
  for (const field of DEVICE_LOCAL_FIELDS) {
    delete rest[field];
  }
  return rest;
}

/**
 * Build the full export payload (the array written to `profiles.json`).
 *
 * @param {object[]} profiles the device profile list
 * @returns {object[]} export-ready profiles
 */
export function buildExportPayload(profiles) {
  return (profiles ?? []).map(toExportProfile);
}

/**
 * Decide the id-collision remap for a single imported profile against a set of
 * ids already claimed on the device (or already claimed earlier in the same
 * import batch).
 *
 * `req:profiles:save` opens `<id>.json` with "w", so importing a profile whose
 * id matches an existing one would clobber the device copy. The safe default is
 * to treat a colliding import as a NEW copy: drop its id so the firmware mints a
 * fresh safe id via generateShortID() (CAR-331). A non-colliding id is kept.
 *
 * NOTE: `req:profiles:list` enumerates by FILENAME STEM but reports the IN-FILE
 * id, so `claimedIds` only covers in-file-id collisions. On-disk filename-stem
 * collisions are deliberately NOT guarded here — the firmware
 * ProfileManager::saveProfile() guard is the authoritative check for that case.
 *
 * @param {object} profile an imported profile
 * @param {Set<string>} claimedIds ids already present on device or claimed in batch
 * @returns {object} the profile to send to `req:profiles:save` (id dropped if colliding)
 */
export function remapImportedProfile(profile, claimedIds) {
  if (profile?.id && claimedIds?.has(profile.id)) {
    const { id: _omitId, ...rest } = profile;
    return rest;
  }
  return profile;
}

/**
 * Human-friendly label for an imported profile in restore-progress messages.
 *
 * @param {object} profile an imported profile
 * @param {number} index zero-based position in the import batch
 * @returns {string}
 */
function importLabel(profile, index) {
  return profile?.label || profile?.id || `profile #${index + 1}`;
}

/**
 * Orchestrate the restore of a parsed backup onto the device. THIS is the
 * shipped import algorithm — `onUpload` and the round-trip tests both call it,
 * so the tests validate the product (PRO-218 P1-2).
 *
 * Behaviour:
 * - Builds the id-collision set from a FRESH device list (`listProfiles`) so a
 *   re-imported backup never overwrites a (possibly newer) on-device profile
 *   (CAR-331).
 * - Saves each profile independently inside its OWN try/catch (PRO-218 P1-1):
 *   one failed/stalled save no longer strands the rest. Each result is recorded
 *   as `{ label, status: 'restored' | 'failed', error }`; the loop continues on
 *   failure so a partial restore is reported, not silently truncated.
 * - Treats a falsy `savedId` echoed by the firmware as a HARD per-item error
 *   (PRO-218 P2-1): the intra-batch dedup relies on the firmware echoing the
 *   stored id back into the claimed set, so without it the next same-id profile
 *   would clobber this one. Better to flag it than to silently corrupt.
 * - Returns a structured summary so the caller can surface count parity
 *   (PRO-218 P2-2) and offer to retry only the failed subset.
 *
 * Timeouts (PRO-218 P1-4): `saveProfile` is expected to reject on a hung
 * transport (the real adapter wraps `apiService.request`, which rejects after a
 * built-in 5s timeout and immediately on socket close). Such a rejection is
 * caught per-item here and reported as a failure naming the exact profile and
 * its position, so a stalled restore surfaces "failed: <label>" with a retry
 * offer rather than hanging forever.
 *
 * @param {object} adapters
 * @param {() => Promise<object[]>} adapters.listProfiles fetch the current device profile list
 * @param {(profile: object) => Promise<{ id?: string }>} adapters.saveProfile persist one profile, resolving to the stored profile (incl. its id)
 * @param {object[]} importedProfiles profiles parsed from the backup file
 * @returns {Promise<{ total: number, savedCount: number, results: {label:string,status:string,error:(Error|null)}[], failed: {label:string,error:(Error|null),profile:object}[] }>}
 */
export async function importProfiles({ listProfiles, saveProfile }, importedProfiles) {
  const profiles = importedProfiles ?? [];
  const existing = (await listProfiles()) ?? [];
  const existingIds = new Set(existing.map(entry => entry?.id).filter(Boolean));

  const results = [];
  const failed = [];
  let savedCount = 0;

  for (let index = 0; index < profiles.length; index++) {
    const original = profiles[index];
    const label = importLabel(original, index);
    try {
      const profile = remapImportedProfile(original, existingIds);
      const saveResponse = await saveProfile(profile);
      const savedId = saveResponse?.id;
      if (!savedId) {
        // The dedup contract requires the firmware to echo the stored id so a
        // SECOND same-id profile in this batch also gets remapped. A missing id
        // means we cannot guarantee that, so fail loudly instead of continuing
        // and risking a silent clobber (PRO-218 P2-1).
        throw new Error('device did not return a saved profile id');
      }
      existingIds.add(savedId);
      savedCount += 1;
      results.push({ label, status: 'restored', error: null });
    } catch (error) {
      results.push({ label, status: 'failed', error });
      failed.push({ label, error, profile: original });
    }
  }

  return { total: profiles.length, savedCount, results, failed };
}

/**
 * Format the restore summary shown to the user after `importProfiles` runs.
 *
 * @param {{ total:number, savedCount:number, failed:{label:string}[] }} summary
 * @returns {string}
 */
export function formatRestoreSummary({ total, savedCount, failed }) {
  if (total === 0) {
    return 'No profiles found in this file — it may be corrupt or the wrong format; nothing was imported.';
  }
  if (failed.length === 0) {
    return `Restored ${savedCount} of ${total} profile${total === 1 ? '' : 's'} from backup.`;
  }
  const labels = failed.map(f => f.label).join(', ');
  return `Restored ${savedCount} of ${total} profiles from backup — failed: ${labels}.`;
}
