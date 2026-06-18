// Pure transforms for the off-device profile export/import round-trip.
//
// This is the migration story for the SPIFFS->LittleFS upgrade (PRO-218): a
// user exports all profiles OFF the device before flashing, the new firmware
// clean-formats LittleFS and seeds the Default profile, then the user
// re-imports the exported file. Because the data lives off-device across the
// format boundary, a silent on-device wipe is structurally impossible.
//
// The logic here was previously inlined in ProfileList/index.jsx (onExport /
// onUpload). It is extracted as pure functions so the round-trip can be unit
// tested without rendering the component or mocking the WebSocket.

/**
 * Strip device-local state from a profile for export. `selected` and `favorite`
 * are per-device runtime flags and must not travel in a backup; `id` is kept so
 * a re-imported profile stays addressable (deletable/selectable/favoritable).
 *
 * @param {object} profile a profile as returned by `req:profiles:list`
 * @returns {object} the profile minus `selected` / `favorite`
 */
export function toExportProfile(profile) {
  const { selected, favorite, ...rest } = profile ?? {};
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
