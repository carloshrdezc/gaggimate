#include "ProfileManager.h"
#include "HeapDiag.h"
#include "ProfileEnumeration.h"
#include "SdReadRetryPolicy.h"
#include <ArduinoJson.h>

#include <algorithm>

#include <utility>

#if !defined(GAGGIMATE_SIM)
#include <Arduino.h> // delay()
#endif

namespace {
// PRO-349: forward declaration so the boot-time enumeration / id-migration
// scans below can route their SD reads through the SAME bounded, ENOMEM-aware,
// size-capped helper that loadProfile() uses (defined later in this anon
// namespace). Before PRO-349 these scans streamed deserializeJson() straight
// off an open SD File handle with no internal-DRAM pre-flight gate and no size
// cap -- the exact pattern PRO-334 fixed for loadProfile().
bool readProfileFileBounded(fs::FS *fs, const String &path, String &outJson);

std::vector<std::pair<String, String>> collectProfileIdMigrations(fs::FS *fs, const String &dir) {
    std::vector<std::pair<String, String>> migrations;
    File root = fs->open(dir);
    if (!root || !root.isDirectory()) {
        return migrations;
    }

    // PRO-349: enumerate the candidate filenames first, then read each through
    // readProfileFileBounded() below. The previous implementation streamed
    // deserializeJson() straight off the live openNextFile() handle with no
    // internal-DRAM pre-flight gate and no size cap; at boot (HomeSpan/BLE/WiFi/
    // mDNS all initializing) the internal DMA pool is most contended, so that
    // ungated stream is the exact ENOMEM-spin pattern PRO-334 fixed. File::name()
    // returns the bare basename on the ESP32 FS backends, so rebuild the
    // directory-qualified path (as remintUnsafeProfileIds()/profilePath() do)
    // before re-opening for the bounded read.
    std::vector<String> names;
    {
        File file = root.openNextFile();
        while (file) {
            String name = file.name();
            if (name.endsWith(".json")) {
                names.push_back(name);
            }
            file = root.openNextFile();
        }
    }

    for (const String &name : names) {
        const String stem = filenameStem(name);
        const String path = reconstructProfilePath(dir, name);
        String json;
        if (!readProfileFileBounded(fs, path, json)) {
            // Gate-closed (internal DRAM below floor), oversized/corrupt, or a
            // transient read failure -> skip this entry rather than parse a
            // partial/unbounded buffer. Mirrors loadProfile()'s graceful
            // degradation; a missed id-migration on a starved boot is recoverable
            // on the next (healthier) boot, a wedged async/boot task is not.
            continue;
        }
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, json);
        if (!err) {
            JsonObject obj = doc.as<JsonObject>();
            const String rawId = obj["id"] | "";
            Profile profile{};
            if (parseProfile(obj, profile)) {
                if (profile.id.isEmpty()) {
                    profile.id = filenameStem(name);
                }
                if (!rawId.isEmpty() && !isSafeId(rawId) && isSafeId(profile.id) && profile.id != rawId &&
                    std::find_if(migrations.begin(), migrations.end(),
                                 [&](const auto &migration) { return migration.first == rawId; }) == migrations.end()) {
                    migrations.emplace_back(rawId, profile.id);
                }
            }
        }
    }

    return migrations;
}

// Self-heal profiles whose addressable id is neither safe in-file nor safe as a
// filename stem (e.g. legacy/imported entries keyed on a 36-char UUID). Such a
// profile is visible in listProfiles() but unaddressable: every WebUI action
// (delete/select/favorite) gates on isSafeId() and parseProfile() blanks the
// unsafe in-file id, so the entry can never be deleted, selected, or favorited.
//
// For each affected file we mint a fresh safe generateShortID(), rewrite the
// file's "id" field, and rename the file to <newId>.json so filename stem and
// in-file id agree. The returned (oldKey -> newId) mappings are fed to
// Settings::migrateProfileIds so any persisted selected/favorite/order
// reference that pointed at the old (unsafe) key follows the rename. We emit a
// mapping for the raw in-file id when present and for the filename stem when it
// differs, since either could be the value a prior build persisted.
//
// A file is only reminted when BOTH the in-file id and the filename stem are
// unsafe (the safe-fallback path is already handled by
// collectProfileIdMigrations()). Files that parse cleanly with a usable safe id
// are left untouched.
std::vector<std::pair<String, String>> remintUnsafeProfileIds(fs::FS *fs, const String &dir) {
    std::vector<std::pair<String, String>> migrations;
    File root = fs->open(dir);
    if (!root || !root.isDirectory()) {
        return migrations;
    }

    // Collect candidate filenames first; rewriting/renaming while iterating the
    // directory handle is unsafe on the SD/FS backends.
    std::vector<String> names;
    {
        File file = root.openNextFile();
        while (file) {
            String name = file.name();
            if (name.endsWith(".json")) {
                names.push_back(name);
            }
            file = root.openNextFile();
        }
    }

    int remintedCount = 0;
    for (const String &name : names) {
        const String stem = filenameStem(name);
        // Canonical full path for this entry. File::name() on the ESP32 Arduino
        // FS backends (LittleFS/SD) returns the BARE BASENAME (e.g. "abc.json"),
        // not the directory-qualified path, so re-opening or removing `name`
        // directly would resolve against the FS root and miss the file in `dir`.
        // Rebuild the path the same way profilePath() does so open/remove hit
        // the real file. (The directory-listing scanners read from the live
        // openNextFile() handle and never re-open by name, which is why only
        // this rewrite-and-rename pass needs the canonical form.)
        const String oldPath = reconstructProfilePath(dir, name);
        String rawId;
        Profile profile{};
        bool parsed = false;
        {
            // PRO-349: route the remint read through the bounded, ENOMEM-aware,
            // size-capped helper instead of streaming deserializeJson() off the
            // open handle. A gate-closed/oversized/transient-failure read returns
            // false; skip this entry (it is retried on the next, healthier boot)
            // rather than thrash the starving allocator at boot time.
            String json;
            if (!readProfileFileBounded(fs, oldPath, json)) {
                ESP_LOGW("ProfileManager", "Remint: bounded read of %s refused/failed; skipping", oldPath.c_str());
                continue;
            }
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, json);
            if (err) {
                ESP_LOGW("ProfileManager", "Remint: failed to parse %s (%s); skipping", oldPath.c_str(), err.c_str());
                continue;
            }
            JsonObject obj = doc.as<JsonObject>();
            rawId = obj["id"] | "";
            parsed = parseProfile(obj, profile);
        }
        if (!parsed) {
            ESP_LOGW("ProfileManager", "Remint: invalid profile schema in %s; skipping", oldPath.c_str());
            continue;
        }

        // If the entry already resolves to a safe addressable id, it is
        // reachable from the WebUI -- skip it. An empty result means the entry
        // is unaddressable and must be reminted. Pass the POST-parse id
        // (parseProfile blanks an unsafe in-file id) so this mirrors exactly how
        // loadProfile() resolves the addressable id: a safe in-file id wins,
        // otherwise a safe filename stem is adopted. Using the raw pre-parse id
        // here would wrongly remint an entry whose in-file id is unsafe but whose
        // filename stem IS safe -- that entry is already addressable via the stem.
        if (!resolveAddressableProfileId(profile.id, stem).isEmpty()) {
            continue;
        }

        // Unaddressable: mint a fresh safe id and rewrite + rename the file.
        // Guard against clobbering an existing profile: generateShortID() is
        // random, so on the (vanishingly unlikely) chance the minted id collides
        // with a file already on disk, draw again rather than truncate it open.
        // Mirrors saveProfile()'s collision guard.
        String newId = generateShortID();
        String newPath = dir + "/" + newId + ".json";
        for (int attempt = 0; attempt < 8 && fs->exists(newPath); ++attempt) {
            newId = generateShortID();
            newPath = dir + "/" + newId + ".json";
        }
        if (fs->exists(newPath)) {
            // Still colliding after retries -- skip rather than risk a clobber.
            ESP_LOGW("ProfileManager", "Remint: minted id space exhausted (last tried %s) for %s; skipping", newPath.c_str(),
                     oldPath.c_str());
            continue;
        }
        profile.id = newId;

        File out = fs->open(newPath, "w");
        if (!out) {
            ESP_LOGW("ProfileManager", "Remint: failed to open %s for write; skipping", newPath.c_str());
            continue;
        }
        JsonDocument outDoc;
        JsonObject outObj = outDoc.to<JsonObject>();
        writeProfile(outObj, profile);
        // Treat the write as successful only when the full document was written.
        // serializeJson() can return a positive-but-short count on a full
        // filesystem; comparing against measureJson() avoids deleting the only
        // good copy (below) after a truncated write left newPath corrupt.
        const size_t expected = measureJson(outDoc);
        const size_t written = serializeJson(outDoc, out);
        out.close();
        if (written != expected || expected == 0) {
            ESP_LOGW("ProfileManager", "Remint: truncated write to %s (%u/%u bytes); rolling back", newPath.c_str(),
                     static_cast<unsigned>(written), static_cast<unsigned>(expected));
            fs->remove(newPath);
            continue;
        }

        // Remove the stale original file unless the rename was a no-op (the new
        // id happened to match the old stem, which generateShortID makes
        // vanishingly unlikely but is harmless to guard). The remove is CHECKED:
        // if it fails (transient SD error / read-only FS) the old unaddressable
        // file would otherwise survive next to the new one, get reminted AGAIN
        // on the next boot with a fresh random id, and accumulate orphan copies
        // while the ref-remapping drifts. Roll back the just-written newPath and
        // skip emitting this migration so the next boot CONVERGES (it retries the
        // same unaddressable original) instead of spawning yet another id.
        if (oldPath != newPath) {
            if (!fs->remove(oldPath)) {
                ESP_LOGW("ProfileManager", "Remint: failed to remove stale %s after writing %s; rolling back to retry next boot",
                         oldPath.c_str(), newPath.c_str());
                fs->remove(newPath);
                continue;
            }
        }

        ESP_LOGW("ProfileManager", "Reminted unaddressable profile id (stem=%s, rawId=%s) -> %s", stem.c_str(), rawId.c_str(),
                 newId.c_str());
        ++remintedCount;

        auto alreadyMapped = [&](const String &from) {
            return std::find_if(migrations.begin(), migrations.end(), [&](const auto &m) { return m.first == from; }) !=
                   migrations.end();
        };
        if (!rawId.isEmpty() && rawId != newId && !alreadyMapped(rawId)) {
            migrations.emplace_back(rawId, newId);
        }
        if (stem != newId && stem != rawId && !alreadyMapped(stem)) {
            migrations.emplace_back(stem, newId);
        }
    }

    if (remintedCount > 0) {
        ESP_LOGW("ProfileManager", "Reminted %d unaddressable profile(s)", remintedCount);
    }

    return migrations;
}

// Find the on-disk filename stem whose loaded in-file profile id matches the
// requested id. Returns an empty String when no matching file is found.
//
// listProfiles() enumerates profiles by FILENAME STEM, but loadProfile() and
// the WebUI key profiles by the IN-FILE JSON `id`. For legacy/imported/migrated
// files those two can diverge, so a direct profilePath(id) lookup misses the
// file. This scan mirrors the parse used by collectProfileIdMigrations() to
// resolve the requested id back to its actual on-disk filename stem.
String findFilenameStemForId(fs::FS *fs, const String &dir, const String &id) {
    File root = fs->open(dir);
    if (!root || !root.isDirectory()) {
        return String();
    }

    // PRO-349: enumerate filenames first, then read each through the bounded,
    // ENOMEM-aware, size-capped helper (as collectProfileIdMigrations() does)
    // instead of streaming deserializeJson() off the live directory handle. The
    // canonical directory-qualified path is rebuilt from the stem because
    // File::name() returns the bare basename on the ESP32 FS backends.
    std::vector<String> names;
    {
        File file = root.openNextFile();
        while (file) {
            String name = file.name();
            if (name.endsWith(".json")) {
                names.push_back(name);
            }
            file = root.openNextFile();
        }
    }

    for (const String &name : names) {
        String stem = filenameStem(name);
        const String path = reconstructProfilePath(dir, name);
        String json;
        if (!readProfileFileBounded(fs, path, json)) {
            // Gate-closed/oversized/transient -> skip; a missed match degrades to
            // "not found" (the caller's existing miss path), never a wedge.
            continue;
        }
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, json);
        if (!err) {
            JsonObject obj = doc.as<JsonObject>();
            Profile profile{};
            if (parseProfile(obj, profile)) {
                // Resolve the addressable id the same way loadProfile() and
                // remintUnsafeProfileIds() do: in-file id wins, else a safe
                // filename stem. Shared helper keeps the rule in one place.
                if (profile.id.isEmpty()) {
                    profile.id = resolveAddressableProfileId(profile.id, stem);
                }
                if (profile.id == id) {
                    return stem;
                }
            }
        }
    }

    return String();
}

// PRO-334: Fail-safe profile read for the async-task path.
//
// loadProfile() runs INLINE ON THE AsyncTCP task for `req:profiles:load`. The
// previous implementation streamed deserializeJson() directly off the open SD
// File handle. Under internal-DRAM exhaustion sdmmc_read_sectors fails with
// ESP_ERR_NO_MEM (0x101) and a streaming parse keeps requesting bytes, so the
// async_tcp task never pets its watchdog -> task-WDT abort -> reboot (the
// reboot-on-Brew symptom).
//
// This helper makes the read BOUNDED and ENOMEM-aware:
//   1. Pre-flight gate on internal-DRAM largest-block (shouldAttemptSdRead):
//      below the floor we don't even poke the starving sdmmc allocator -- we
//      return false so the caller surfaces a clean error to the client.
//   2. Read the whole (small) profile file into an in-memory String with a
//      STRICTLY BOUNDED retry schedule (kSdReadMaxAttempts, short capped
//      backoffs that yield the CPU so the watchdog is pet), then parse the
//      buffer. A transient failure becomes a clean `false`, never a spin.
//
// RESIDUAL RISK (PRO-342): the WDT-reboot rationale above is SPECIFIC TO THE
// ASYNC_TCP PATH. The very same pre-flight gate is also reached via
// loadProfile()/loadSelectedProfile() invoked on the DISPLAY-LOOP (UI) task
// (e.g. DefaultUI.cpp profile-load call sites), which pets a DIFFERENT
// watchdog. On that path the gate does NOT prevent a reboot -- there was no
// async-task WDT abort to prevent -- so under internal-DRAM pressure it instead
// degrades to a SPURIOUSLY FAILED PROFILE LOAD: the helper returns false and
// the UI surfaces a "not found"/load failure (degraded UX, no crash) even
// though the profile is present on disk. This is an accepted tradeoff: a clean
// failed load beats thrashing the starving allocator, but the failure mode on
// the loop-task path is a missed load, not the WDT reboot the async-path
// rationale describes. No behavior change is intended here; this note only
// documents the cross-path behavior.
//
// Returns true and fills `outJson` on success; false on a refused/failed read.
bool readProfileFileBounded(fs::FS *fs, const String &path, String &outJson) {
    // Pre-flight: refuse the DMA-backed read when internal DRAM is below the
    // floor rather than thrash the allocator on the async task.
    if (!shouldAttemptSdRead(gmInternalLargestBlock())) {
        ESP_LOGW("ProfileManager", "Skipping SD read of %s: internal DRAM below floor (largest block=%u B < %u B)", path.c_str(),
                 static_cast<unsigned>(gmInternalLargestBlock()), static_cast<unsigned>(kSdReadInternalDramFloorBytes));
        return false;
    }

    for (int attempt = 0; attempt < kSdReadMaxAttempts; ++attempt) {
        if (attempt > 0) {
            const unsigned long backoff = nextSdReadBackoffMs(attempt);
#if !defined(GAGGIMATE_SIM)
            // vTaskDelay-backed delay() yields the CPU so the async_tcp task
            // watchdog is pet between bounded retries.
            if (backoff > 0) {
                delay(backoff);
            }
#else
            (void)backoff;
#endif
        }

        File file = fs->open(path, "r");
        if (!file) {
            // open() failing is usually "no such file" (a genuine not-found),
            // but can also be a transient FS/DMA error under pressure. Retry
            // within the bounded budget; the final attempt's false is the clean
            // give-up.
            continue;
        }

        const size_t size = file.size();
        String json;
        bool readOk = false;
        // A profile JSON is small; read the whole file into memory in one bounded
        // pass instead of a streaming parse off the handle. The size cap
        // (sdReadSizeDecision) is the shared bound every boot-time profile read
        // uses (PRO-349): an oversized/corrupt file is refused rather than forcing
        // a large internal allocation, an empty file is a failed read.
        // reserve()+read keeps the SD access to a single bounded loop with a
        // definite end.
        if (sdReadSizeDecision(size) == SdReadSizeDecision::kRead && json.reserve(size + 1)) {
            size_t total = 0;
            uint8_t buf[512];
            readOk = true;
            while (total < size) {
                const size_t want = (size - total) < sizeof(buf) ? (size - total) : sizeof(buf);
                const int got = file.read(buf, want);
                if (got <= 0) {
                    // Short/failed read (ENOMEM on the sdmmc DMA buffer surfaces
                    // here). Abandon this attempt and let the bounded retry loop
                    // decide; never spin on a zero-progress read.
                    readOk = false;
                    break;
                }
                json.concat(reinterpret_cast<const char *>(buf), static_cast<unsigned int>(got));
                total += static_cast<size_t>(got);
            }
            readOk = readOk && (total == size);
        }
        file.close();

        if (readOk) {
            outJson = std::move(json);
            return true;
        }
        ESP_LOGW("ProfileManager", "SD read of %s failed (attempt %d/%d)", path.c_str(), attempt + 1, kSdReadMaxAttempts);
    }
    return false;
}
} // namespace

ProfileManager::ProfileManager(fs::FS *fs, String dir, Settings &settings, PluginManager *plugin_manager)
    : _plugin_manager(plugin_manager), _settings(settings), _fs(fs), _dir(std::move(dir)) {}

void ProfileManager::setup() {
    ensureDirectory();
    // First self-heal profiles with no usable safe id (UUID-keyed legacy/imported
    // entries) by reminting them; this renames files on disk, so it must run
    // before the safe-fallback migration scan below reads the directory.
    auto migrations = remintUnsafeProfileIds(_fs, _dir);
    auto fallbackMigrations = collectProfileIdMigrations(_fs, _dir);
    migrations.insert(migrations.end(), fallbackMigrations.begin(), fallbackMigrations.end());
    _settings.migrateProfileIds(migrations);
    auto profiles = listProfiles();
    if (getFavoritedProfiles().empty() || profiles.empty() || _settings.getSelectedProfile() == "" ||
        !loadSelectedProfile(selectedProfile)) {
        migrate();
        loadSelectedProfile(selectedProfile);
    }
    _settings.setFavoritedProfiles(getFavoritedProfiles(true));
}

bool ProfileManager::ensureDirectory() const {
    if (!_fs->exists(_dir)) {
        return _fs->mkdir(_dir);
    }
    return true;
}

String ProfileManager::profilePath(const String &uuid) const { return _dir + "/" + uuid + ".json"; }

void ProfileManager::migrate() {
    Profile profile{};
    profile.id = generateShortID();
    profile.label = "Default";
    profile.description = "Default profile";
    profile.temperature = 93;
    profile.type = "standard";
    Phase brewPhase{};
    brewPhase.name = "Brew";
    brewPhase.phase = PhaseType::PHASE_TYPE_BREW;
    brewPhase.valve = 1;
    brewPhase.duration = 28;
    brewPhase.pumpIsSimple = true;
    brewPhase.pumpSimple = 100;
    Target target{};
    target.type = TargetType::TARGET_TYPE_VOLUMETRIC;
    target.operator_ = TargetOperator::GTE;
    target.value = 36;
    brewPhase.targets.push_back(target);
    profile.phases.push_back(brewPhase);
    saveProfile(profile);
    _settings.setSelectedProfile(profile.id);
    for (String id : listProfiles()) {
        addFavoritedProfile(id);
    }
}

std::vector<String> ProfileManager::listProfiles() {
    std::vector<String> uuids;
    File root = _fs->open(_dir);
    if (!root || !root.isDirectory())
        return uuids;

    File file = root.openNextFile();
    while (file) {
        String name = file.name();
        if (isProfileFilename(name)) {
            // PRO-354: use the shared pure stem extractor instead of an inline
            // lastIndexOf('/')/lastIndexOf('.') substring. For a ".json" entry
            // both yield the same value (strip the directory, drop the trailing
            // ".json"), so listing behavior is unchanged; routing through the
            // one helper keeps every directory scanner on a single definition.
            uuids.push_back(filenameStem(name));
        }
        file = root.openNextFile();
    }

    std::vector<String> ordered;
    auto stored = _settings.getProfileOrder();
    for (auto const &id : stored) {
        if (std::find(uuids.begin(), uuids.end(), id) != uuids.end() &&
            std::find(ordered.begin(), ordered.end(), id) == ordered.end()) {
            ordered.push_back(id);
        }
    }
    for (auto const &id : uuids) {
        if (std::find(ordered.begin(), ordered.end(), id) == ordered.end()) {
            ordered.push_back(id);
        }
    }
    return ordered;
}

bool ProfileManager::loadProfile(const String &uuid, Profile &outProfile) {
    // PRO-334: read via the bounded, ENOMEM-aware helper instead of streaming
    // deserializeJson() off the open File handle. loadProfile() runs on the
    // AsyncTCP task for req:profiles:load / req:profiles:list; a streaming parse
    // under internal-DRAM exhaustion could spin the async task into a task-WDT
    // reboot. The helper pre-flight-gates on internal-DRAM headroom and bounds
    // the read+retries, returning a clean false (surfaced to the client as
    // "Profile not found") rather than ever hanging the task.
    String json;
    if (!readProfileFileBounded(_fs, profilePath(uuid), json)) {
        return false;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err)
        return false;

    if (!parseProfile(doc.as<JsonObject>(), outProfile)) {
        return false;
    }
    // Apply filename-based fallback only when the stem is itself a safe id.
    // An unsafe filename stem (legacy non-conforming file on disk) would make
    // the profile visible in the list but inoperable via the WebUI (all profile
    // actions reject non-safe ids). Keep the id empty in that case so the UI
    // treats it as a broken entry rather than silently creating a duplicate.
    // (ProfileManager::setup self-heals such entries on boot via reminting.)
    if (outProfile.id.isEmpty()) {
        outProfile.id = resolveAddressableProfileId(outProfile.id, uuid);
    }
    outProfile.selected = outProfile.id == _settings.getSelectedProfile();
    std::vector<String> favoritedProfiles = _settings.getFavoritedProfiles();
    outProfile.favorite = std::find(favoritedProfiles.begin(), favoritedProfiles.end(), outProfile.id) != favoritedProfiles.end();
    return true;
}

bool ProfileManager::saveProfile(Profile &profile) {
    if (!ensureDirectory())
        return false;
    bool isNew = false;

    if (profile.id == nullptr || profile.id.isEmpty()) {
        profile.id = generateShortID();
        isNew = true;
    }

    // Guard against clobbering an unrelated profile. saveProfile writes to
    // <id>.json, but legacy/imported files can have a filename stem that differs
    // from their in-file id (e.g. a.json holding id "b"). If <profile.id>.json
    // already exists AND the profile it actually contains is NOT this one,
    // opening it "w" would destroy that unrelated profile. loadProfile(uuid)
    // opens exactly the file this save would clobber, so an id mismatch means a
    // collision; mint a fresh id instead. A matching id is a legitimate in-place
    // edit and is left untouched.
    if (!isNew && profileExists(profile.id)) {
        // PRO-341 (Ref PRO-334, PR #330, finding #1): loadProfile() now sits
        // behind the internal-DRAM pre-flight gate (readProfileFileBounded,
        // shouldAttemptSdRead). Below the floor it returns false for a MEMORY
        // reason, which is indistinguishable from "not found" at this call
        // site. If we let that false short-circuit the collision check, the
        // guard is silently skipped and the open("w") below could clobber an
        // unrelated (legacy mismatched-stem) profile. Fail safe instead: when
        // the gate would refuse the read, treat it exactly like a detected
        // collision and mint a fresh id rather than risk an overwrite. The
        // normal (sufficient-DRAM) path is unchanged: the gate is true, this
        // branch is skipped, and the read-based check runs as before.
        //
        // PRO-344 (Ref PRO-341, PR #333, finding #1): below the floor this
        // branch fires for EVERY existing-id save, including the common safe
        // case of editing your own profile (stem == in-file id, no real
        // collision). Without the read it is refusing, the gate cannot tell a
        // safe in-place edit from a dangerous mismatched-stem collision, so it
        // conservatively mints a fresh id, sets isNew = true, and the tail
        // auto-favorites it. The net effect: a safe in-place edit is converted
        // into a renamed, auto-favorited duplicate and the ORIGINAL file is
        // left untouched. That is the unavoidable price of failing safe, and an
        // accepted tradeoff -- the "normal-path unchanged" guarantee therefore
        // holds only AT/ABOVE the floor.
        if (!shouldAttemptSdRead(gmInternalLargestBlock())) {
            // Log BEFORE overwriting profile.id so %s prints the ORIGINAL id
            // the user was trying to save (the diagnostically useful value).
            ESP_LOGW("ProfileManager", "saveProfile: internal DRAM below floor; failing safe to a fresh id for %s",
                     profile.id.c_str());
            profile.id = generateShortID();
            isNew = true;
        } else {
            Profile existing{};
            if (loadProfile(profile.id, existing) && existing.id != profile.id) {
                profile.id = generateShortID();
                isNew = true;
            }
        }
    }

    ESP_LOGI("ProfileManager", "Saving profile %s", profile.id.c_str());

    File file = _fs->open(profilePath(profile.id), "w");
    if (!file)
        return false;

    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    writeProfile(obj, profile);

    bool ok = serializeJson(doc, file) > 0;
    file.close();
    if (profile.id == selectedProfile.id) {
        selectedProfile = Profile{};
        loadSelectedProfile(selectedProfile);
    }
    selectProfile(_settings.getSelectedProfile());
    _plugin_manager->trigger("profiles:profile:save", "id", profile.id);
    if (isNew) {
        addFavoritedProfile(profile.id);
    }
    return ok;
}

bool ProfileManager::deleteProfile(const String &uuid) {
    removeFavoritedProfile(uuid);
    // Fast path: a file named <uuid>.json exists AND its parsed identity is
    // actually `uuid`. The id check is essential: with colliding mappings
    // (e.g. a.json holds id "b" while another file holds id "a", possible with
    // imported/restored profiles) a bare filename match would delete the wrong
    // profile. When the direct file's identity does not match, fall through to
    // the scan so the file genuinely owning `uuid` is the one removed.
    if (profileExists(uuid)) {
        Profile direct{};
        if (loadProfile(uuid, direct) && direct.id == uuid) {
            return _fs->remove(profilePath(uuid));
        }
    }
    // Legacy/imported/migrated files: the in-file id can differ from the
    // filename stem, so profilePath(uuid) misses (or misidentifies) the file.
    // Resolve the id back to its actual on-disk filename and remove that.
    // Return false only when no matching file exists on disk.
    String stem = findFilenameStemForId(_fs, _dir, uuid);
    if (stem.isEmpty()) {
        return false;
    }
    return _fs->remove(profilePath(stem));
}

bool ProfileManager::profileExists(const String &uuid) { return _fs->exists(profilePath(uuid)); }

void ProfileManager::selectProfile(const String &uuid) {
    ESP_LOGI("ProfileManager", "Selecting profile %s", uuid.c_str());
    _settings.setSelectedProfile(uuid);
    selectedProfile = Profile{};
    loadSelectedProfile(selectedProfile);
    _plugin_manager->trigger("profiles:profile:select", "id", uuid);
}

Profile &ProfileManager::getSelectedProfile() { return selectedProfile; }

// NOTE (PRO-342): loadSelectedProfile()/loadProfile() are also invoked on the
// DISPLAY-LOOP (UI) task (see DefaultUI.cpp call sites), not just AsyncTCP. The
// shared internal-DRAM pre-flight gate in readProfileFileBounded() degrades to
// a failed profile load (not the async-path WDT reboot) under memory pressure
// here -- see the residual-risk note on that helper above.
bool ProfileManager::loadSelectedProfile(Profile &outProfile) { return loadProfile(_settings.getSelectedProfile(), outProfile); }

std::vector<String> ProfileManager::getFavoritedProfiles(bool validate) {

    auto rawFavorites = _settings.getFavoritedProfiles();
    std::vector<String> result;

    auto storedProfileOrder = _settings.getProfileOrder();
    for (const auto &id : storedProfileOrder) {
        if (std::find(rawFavorites.begin(), rawFavorites.end(), id) != rawFavorites.end()) {
            if (!validate || profileExists(id)) {
                if (std::find(result.begin(), result.end(), id) == result.end()) {
                    result.push_back(id);
                }
            }
        }
    }

    for (const auto &fav : rawFavorites) {
        if (std::find(result.begin(), result.end(), fav) == result.end()) {
            if (!validate || profileExists(fav)) {
                result.push_back(fav);
            }
        }
    }

    if (result.empty()) {
        String sel = _settings.getSelectedProfile();
        bool selValid = (!validate) || profileExists(sel);
        if (selValid) {
            result.push_back(sel);
        }
    }
    return result;
}

void ProfileManager::removeFavoritedProfile(String id) {
    _settings.removeFavoritedProfile(id);
    _plugin_manager->trigger("profiles:profile:unfavorite", "id", id);
}

void ProfileManager::addFavoritedProfile(String id) {
    _settings.addFavoritedProfile(id);
    _plugin_manager->trigger("profiles:profile:favorite", "id", id);
}
