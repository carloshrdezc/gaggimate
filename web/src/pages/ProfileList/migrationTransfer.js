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
// P1-2). The same reasoning extends to the MULTI-FILE read+parse+aggregate step:
// aggregateImportFiles() lives here and is unit-tested, so the silent
// partial-loss path (a corrupt file in a multi-select) is closed behind a tested
// seam rather than living inline in onUpload (PRO-218 NEW-1/NEW-2).

import { parseProfile } from './utils.js';

// DENYLIST (PRO-218 P3-3): everything is exported EXCEPT the per-device runtime
// flags listed here. We intentionally use a denylist rather than an allowlist of
// portable fields: this is a data-preservation BACKUP, so the failure we must
// avoid is silently dropping a profile field the user cares about. An allowlist
// would drop any future portable field that isn't added to it; a denylist only
// ever risks leaking a new device-local flag (cosmetic, and harmlessly stripped
// again by the firmware on import). `id` is kept so a re-imported profile stays
// addressable (deletable/selectable/favoritable).
//
// KEEP IN SYNC: when the firmware adds a new per-device runtime flag (a field
// that is meaningful only on the device that set it and must NOT travel in a
// backup), add it to DEVICE_LOCAL_FIELDS.
export const DEVICE_LOCAL_FIELDS = ['selected', 'favorite'];

/**
 * Strip device-local state from a profile for export.
 *
 * @param {object} profile a profile as returned by `req:profiles:list`
 * @returns {object} the profile minus device-local fields
 */
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
 * Thrown when the pre-flight `req:profiles:list` fetch fails before the restore
 * loop runs. Distinguished from per-item save failures so the caller can tell
 * the user nothing was overwritten and the restore is safe to retry wholesale
 * (PRO-218 NEW-3).
 */
export class ListProfilesError extends Error {
  constructor(cause) {
    super('Could not read the existing profiles from the device');
    this.name = 'ListProfilesError';
    this.cause = cause;
  }
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

  // The collision set is built from a FRESH device list BEFORE the per-item
  // loop, so it is outside the per-item try/catch. A transient drop / 5s timeout
  // on this one call (e.g. the device is busy right after a reformat) used to
  // throw an opaque error and strand an otherwise-recoverable restore. Wrap it
  // so the caller can surface a clear "couldn't read the device list — nothing
  // was changed, retry" message instead (PRO-218 NEW-3).
  let existing;
  try {
    existing = (await listProfiles()) ?? [];
  } catch (error) {
    throw new ListProfilesError(error);
  }
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
 * P2-A (PRO-218): the FINAL alert the user sees must reflect the WHOLE restore
 * picture in one place — both the per-PROFILE outcome (saved vs failed) AND, when
 * a multi-file selection had files that could not be read/parsed, the per-FILE
 * shortfall. The dropped-file warning is shown earlier as a separate `confirm()`,
 * but the last dialog on screen is this summary; if it read an unqualified
 * "Restored N of N from backup" the user could close the flow believing the
 * restore was complete when files/profiles were in fact dropped. So whenever
 * there is ANY shortfall — failed profiles, a saved<total gap, OR dropped files —
 * the message names what was lost and tells the user to re-import it.
 *
 * `fileContext` is optional so the pure round-trip callers (and the existing
 * profile-only tests) keep their two-arg-free contract; the onUpload glue passes
 * the file aggregate so the multi-file story lands in the final alert.
 *
 * @param {{ total:number, savedCount:number, failed:{label:string}[] }} summary
 * @param {{ selectedFiles:number, okFiles:number, failedFiles:{name:string}[] }} [fileContext]
 *   per-FILE outcome from `aggregateImportFiles`; omit for a single-file / pure call.
 * @returns {string}
 */
export function formatRestoreSummary({ total, savedCount, failed }, fileContext) {
  const failedProfileLabels = (failed ?? []).map(f => f.label);
  const droppedFiles = fileContext?.failedFiles ?? [];
  const hasFileShortfall = droppedFiles.length > 0;
  const hasProfileShortfall = failedProfileLabels.length > 0 || savedCount < total;

  if (total === 0) {
    const base =
      'No profiles found in this file — it may be corrupt or the wrong format; nothing was imported.';
    // Even with nothing parsed, if this came from a multi-file selection name the
    // files that were dropped so the picture is complete in one place.
    return hasFileShortfall ? `${base} Failed files: ${fileLabels(droppedFiles)}.` : base;
  }

  // Clean, complete success: no failed profiles, count parity met, and (when a
  // file context was supplied) every selected file contributed. Only here may the
  // message read as unqualified success.
  if (!hasProfileShortfall && !hasFileShortfall) {
    if (fileContext) {
      return (
        `Restored ${savedCount} of ${total} profile${total === 1 ? '' : 's'} ` +
        `from ${fileContext.okFiles} of ${fileContext.selectedFiles} ` +
        `file${fileContext.selectedFiles === 1 ? '' : 's'}.`
      );
    }
    return `Restored ${savedCount} of ${total} profile${total === 1 ? '' : 's'} from backup.`;
  }

  // Something fell short. Build a single message that reflects the whole picture:
  // profiles restored out of total, files read out of selected (when known), and
  // the names of whatever was dropped — so the last thing on screen is never an
  // unqualified success.
  const parts = [];
  if (fileContext) {
    parts.push(
      `Restored ${savedCount} of ${total} profile${total === 1 ? '' : 's'} ` +
        `from ${fileContext.okFiles} of ${fileContext.selectedFiles} ` +
        `file${fileContext.selectedFiles === 1 ? '' : 's'}.`,
    );
  } else {
    parts.push(`Restored ${savedCount} of ${total} profile${total === 1 ? '' : 's'} from backup.`);
  }
  if (failedProfileLabels.length > 0) {
    parts.push(`Failed profiles: ${failedProfileLabels.join(', ')}.`);
  }
  if (hasFileShortfall) {
    parts.push(`Failed files: ${fileLabels(droppedFiles)}. Re-import those files.`);
  }
  return parts.join(' ');
}

function fileLabels(failedFiles) {
  return failedFiles.map(f => f.name).join(', ');
}

/**
 * Default cap on user-confirmed restore retries. A hung transport must not be
 * able to trap the user in an endless confirm() cycle (PRO-218 NEW-6).
 */
export const MAX_RESTORE_RETRIES = 5;

/**
 * Orchestrate the full restore interaction loop: run `importProfiles`, surface a
 * whole-picture summary that folds in the multi-file `fileContext` (P2-A), and
 * drive the bounded retry-the-failed-subset loop (NEW-4/NEW-6).
 *
 * P2-B (PRO-218): this is the glue that used to live inline in the `restoreProfiles`
 * useCallback. It is extracted here, with the user-interaction surface injected as
 * `alert` / `confirm` callbacks and the device as `adapters`, so the abort /
 * proceed / retry / give-up branch logic is unit-testable WITHOUT rendering the
 * component. The React wrapper passes `window.alert` / `window.confirm` and an
 * `onBeforeRetry` that refetches the device list between attempts.
 *
 * The summary alert is qualified ONLY on the FIRST attempt: `fileContext`
 * describes the original multi-file selection, which is meaningless once we are
 * retrying a failed subset of already-parsed profiles. Subsequent attempts use the
 * profile-only summary so we don't claim "from 1 of 3 files" against a retry batch.
 *
 * @param {object} args
 * @param {{ listProfiles:Function, saveProfile:Function }} args.adapters device adapters
 * @param {object[]} args.importedProfiles the parsed batch to restore
 * @param {{ selectedFiles:number, okFiles:number, failedFiles:{name:string}[] }} [args.fileContext]
 *   per-FILE outcome from `aggregateImportFiles`, folded into the first summary (P2-A)
 * @param {(message:string)=>void} args.alert user-facing alert sink
 * @param {(message:string)=>boolean} args.confirm user-facing confirm (returns retry y/n)
 * @param {() => (void|Promise<void>)} [args.onBeforeRetry] hook run before each retry attempt (e.g. refetch device list)
 * @param {number} [args.maxRetries] retry cap (defaults to MAX_RESTORE_RETRIES)
 * @returns {Promise<{ outcome:string, attempts:number, lastSummary:(object|null) }>}
 *   `outcome` ∈ 'complete' | 'partial-stopped' | 'gave-up' | 'list-error' | 'error'
 */
export async function runRestore({
  adapters,
  importedProfiles,
  fileContext,
  alert,
  confirm,
  onBeforeRetry,
  maxRetries = MAX_RESTORE_RETRIES,
}) {
  let batch = importedProfiles;
  let lastSummary = null;
  try {
    for (let attempt = 0; attempt <= maxRetries; attempt++) {
      const summary = await importProfiles(adapters, batch);
      lastSummary = summary;
      // Fold the file shortfall into the FINAL summary only on the first pass —
      // the file context describes the original selection, not a retry subset.
      alert(formatRestoreSummary(summary, attempt === 0 ? fileContext : undefined));
      if (summary.failed.length === 0) {
        return { outcome: 'complete', attempts: attempt + 1, lastSummary };
      }

      if (attempt === maxRetries) {
        const labels = summary.failed.map(f => f.label).join(', ');
        alert(
          `Giving up after ${maxRetries} retries — still failing: ${labels}. ` +
            'Check the device connection and re-import the file manually.',
        );
        return { outcome: 'gave-up', attempts: attempt + 1, lastSummary };
      }

      const labels = summary.failed.map(f => f.label).join(', ');
      const retry = confirm(
        `${summary.failed.length} profile(s) failed to restore: ${labels}.\n\n` +
          'Retry the failed profiles now?',
      );
      if (!retry) {
        return { outcome: 'partial-stopped', attempts: attempt + 1, lastSummary };
      }
      if (onBeforeRetry) await onBeforeRetry();
      batch = summary.failed.map(f => f.profile);
    }
    // Unreachable: the loop returns on every terminal branch. Defensive only.
    return { outcome: 'partial-stopped', attempts: maxRetries + 1, lastSummary };
  } catch (err) {
    if (err instanceof ListProfilesError) {
      // The pre-flight device-list read failed BEFORE any save ran, so nothing on
      // the device was changed. Tell the user it's safe to retry rather than
      // leaving an opaque "Failed to import" (PRO-218 NEW-3).
      alert(
        'Could not read the existing profiles from the device, so the restore ' +
          'was aborted before changing anything. Check the connection and try again.',
      );
      return { outcome: 'list-error', attempts: 0, lastSummary, error: err };
    }
    alert(`Failed to import profiles: ${err.message}`);
    return { outcome: 'error', attempts: 0, lastSummary, error: err };
  }
}

/**
 * Read+parse+aggregate a multi-file backup selection into one restore batch
 * WHILE preserving the per-FILE outcome.
 *
 * PRO-218 NEW-1 (silent multi-file partial loss): a backup may have been saved
 * as several per-profile files; the restore reads ALL of them and merges them
 * into a single dedup batch (PRO-218 P1-3). The hazard the multi-file fix
 * introduced is that a file which failed to read (`FileReader` onerror → null)
 * or parsed to zero profiles (corrupt / truncated / wrong format → parseProfile
 * returns `[]`) used to be filtered out SILENTLY: select 5 files, 2 corrupt →
 * 3 imported and reported as "Restored 3 of 3", with no sign 2 files were lost.
 * After a reformat that is permanent silent partial data loss.
 *
 * This function keeps count parity against SELECTED FILES, not just parsed
 * profiles, so a dropped file can never be invisible. The caller decides what to
 * do with `failedFiles` (the shipped onUpload reports them prominently and lets
 * the user abort or proceed with the survivors).
 *
 * Pure (no FileReader / DOM): the caller does the async file read and passes the
 * already-resolved text in, so this whole seam — the exact place NEW-1 lived —
 * is unit-testable (PRO-218 NEW-2).
 *
 * @param {{name:string,text:(string|null)}[]} fileTexts one entry per selected
 *   file: `name` for reporting, `text` the FileReader result (`null` if the read
 *   failed or returned a non-string).
 * @returns {{
 *   profiles: object[],
 *   fileResults: {name:string,ok:boolean,count:number}[],
 *   selectedCount: number,
 *   okCount: number,
 *   failedFiles: {name:string}[]
 * }} `profiles` is the merged batch from every file that parsed to >=1 profile;
 *   `fileResults` records each file's outcome; `failedFiles` is the subset that
 *   read-failed or parsed to zero.
 */
export function aggregateImportFiles(fileTexts) {
  const entries = fileTexts ?? [];
  const fileResults = entries.map(({ name, text }) => {
    // A read failure (null/non-string) and a parse-to-zero are BOTH failures
    // from the user's point of view: the file contributed nothing to the
    // restore. parseProfile returns [] for corrupt/truncated/wrong-format input.
    const parsed = typeof text === 'string' ? parseProfile(text) : [];
    const count = parsed.length;
    return { name, ok: count > 0, count, profiles: parsed };
  });

  const profiles = fileResults.flatMap(r => r.profiles);
  const failedFiles = fileResults.filter(r => !r.ok).map(r => ({ name: r.name }));

  return {
    profiles,
    fileResults: fileResults.map(({ name, ok, count }) => ({ name, ok, count })),
    selectedCount: entries.length,
    okCount: fileResults.filter(r => r.ok).length,
    failedFiles,
  };
}

/**
 * Build the warning shown when one or more selected files could not be read or
 * contained no profiles (PRO-218 NEW-1). Names the failed files so the user
 * knows exactly which backups to re-check, and distinguishes "files selected"
 * vs "files parsed" so a dropped file is never invisible.
 *
 * @param {{ selectedCount:number, failedFiles:{name:string}[] }} aggregate
 * @returns {string}
 */
export function formatFileAggregateWarning({ selectedCount, failedFiles }) {
  const names = failedFiles.map(f => f.name).join(', ');
  const failedCount = failedFiles.length;
  return (
    `${failedCount} of ${selectedCount} selected file${selectedCount === 1 ? '' : 's'} ` +
    `could not be read or contained no profiles: ${names}. ` +
    `Nothing was imported from ${failedCount === 1 ? 'it' : 'them'}.`
  );
}
