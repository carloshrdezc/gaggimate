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
                        std::find_if(migrations.begin(), migrations.end(), [&](const auto &migration) {
                            return migration.first == rawId;
                        }) == migrations.end()) {
                        migrations.emplace_back(rawId, profile.id);
                    }
                }
            }
        }
        file = root.openNextFile();
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
                    // Mirror loadProfile()'s id resolution: in-file id wins,
                    // falling back to the filename stem only when it is safe.
                    if (profile.id.isEmpty() && isSafeId(stem)) {
                        profile.id = stem;
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
    _settings.migrateProfileIds(collectProfileIdMigrations(_fs, _dir));
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
    if (outProfile.id.isEmpty() && isSafeId(uuid)) {
        outProfile.id = uuid;
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
    // Fast path: the requested id matches the filename stem directly.
    if (profileExists(uuid)) {
        return _fs->remove(profilePath(uuid));
    }
    // Legacy/imported/migrated files: the in-file id can differ from the
    // filename stem, so profilePath(uuid) misses the file. Resolve the id back
    // to its actual on-disk filename and remove that. Return false only when no
    // matching file exists on disk.
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
