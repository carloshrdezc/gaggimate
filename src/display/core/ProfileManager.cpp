#include "ProfileManager.h"
#include <ArduinoJson.h>

#include <algorithm>

#include <utility>

namespace {
String filenameStem(const String &name) {
    String stem = name;
    int slash = stem.lastIndexOf('/');
    if (slash >= 0) {
        stem = stem.substring(slash + 1);
    }
    if (stem.endsWith(".json")) {
        stem = stem.substring(0, stem.length() - 5);
    }
    return stem;
}

std::vector<std::pair<String, String>> collectProfileIdMigrations(fs::FS *fs, const String &dir) {
    std::vector<std::pair<String, String>> migrations;
    File root = fs->open(dir);
    if (!root || !root.isDirectory()) {
        return migrations;
    }

    File file = root.openNextFile();
    while (file) {
        String name = file.name();
        if (name.endsWith(".json")) {
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, file);
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
        file = root.openNextFile();
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
        const String oldPath = dir + "/" + stem + ".json";
        String rawId;
        Profile profile{};
        bool parsed = false;
        {
            File file = fs->open(oldPath, "r");
            if (!file) {
                ESP_LOGW("ProfileManager", "Remint: failed to open %s for read; skipping", oldPath.c_str());
                continue;
            }
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, file);
            file.close();
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

    File file = root.openNextFile();
    while (file) {
        String name = file.name();
        if (name.endsWith(".json")) {
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, file);
            if (!err) {
                JsonObject obj = doc.as<JsonObject>();
                Profile profile{};
                if (parseProfile(obj, profile)) {
                    String stem = filenameStem(name);
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
        file = root.openNextFile();
    }

    return String();
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
        if (name.endsWith(".json")) {
            int start = name.lastIndexOf('/') + 1;
            int end = name.lastIndexOf('.');
            uuids.push_back(name.substring(start, end));
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
    File file = _fs->open(profilePath(uuid), "r");
    if (!file)
        return false;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, file);
    file.close();
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
        Profile existing{};
        if (loadProfile(profile.id, existing) && existing.id != profile.id) {
            profile.id = generateShortID();
            isNew = true;
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
