#include "GrinderManager.h"

#include <esp_log.h>
#include <utility>

GrinderManager::GrinderManager(fs::FS *fs, String path) : _fs(fs), _path(std::move(path)) {}

void GrinderManager::setup() {
    ensureDirectory();
    if (_mutex == nullptr) {
        _mutex = xSemaphoreCreateMutex();
        if (_mutex == nullptr) {
            // Out of memory creating the mutex: degrade gracefully. Public
            // methods proceed without locking rather than crashing; the only
            // downside is the original (pre-fix) last-write-wins race.
            ESP_LOGE("GrinderManager", "Failed to create grinder mutex; proceeding without locking");
        }
    }
}

bool GrinderManager::ensureDirectory() const {
    // The grinder list lives at e.g. "/g/grinders.json"; make sure the parent
    // directory exists before the first write.
    int slash = _path.lastIndexOf('/');
    if (slash <= 0) {
        return true; // stored at filesystem root, nothing to create
    }
    String dir = _path.substring(0, slash);
    if (!_fs->exists(dir)) {
        return _fs->mkdir(dir);
    }
    return true;
}

std::vector<String> GrinderManager::listGrinders() {
    if (_mutex == nullptr) {
        // Mutex creation failed (logged once in setup()): operate lock-free.
        return listGrindersUnlocked();
    }
    xSemaphoreTake(_mutex, portMAX_DELAY);
    std::vector<String> grinders = listGrindersUnlocked();
    xSemaphoreGive(_mutex);
    return grinders;
}

std::vector<String> GrinderManager::listGrindersUnlocked() {
    std::vector<String> grinders;
    File file = _fs->open(_path, "r");
    if (!file) {
        return grinders;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, file);
    file.close();
    if (err) {
        return grinders;
    }

    JsonArray arr = doc["grinders"].as<JsonArray>();
    if (arr.isNull()) {
        return grinders;
    }
    for (JsonVariant v : arr) {
        String name = v.as<String>();
        name.trim();
        if (!name.isEmpty()) {
            grinders.push_back(name);
        }
        // Cap defensively on read in case the file was hand-edited beyond the
        // write-time limit; recordGrinder() always caps on write.
        if (grinders.size() >= GRINDER_LIST_MAX) {
            break;
        }
    }
    return grinders;
}

bool GrinderManager::recordGrinder(const String &name) {
    String trimmed = name;
    trimmed.trim();
    if (trimmed.isEmpty() || trimmed.length() > GRINDER_NAME_MAX_LEN) {
        return false;
    }

    // Hold the lock across the ENTIRE read-modify-write so a concurrent caller
    // (local WebSocket callback vs. relay task on the other core) can't drop a
    // name via last-write-wins, and so no listGrinders() can observe the file
    // mid-truncate. Use the unlocked read helper to avoid re-taking _mutex.
    const bool locked = (_mutex != nullptr);
    if (locked) {
        xSemaphoreTake(_mutex, portMAX_DELAY);
    }

    std::vector<String> grinders = listGrindersUnlocked();

    // Remove any existing entry matching case-insensitively so the freshly
    // recorded name (with its current casing) moves to the front.
    String lowered = trimmed;
    lowered.toLowerCase();
    for (auto it = grinders.begin(); it != grinders.end();) {
        String existing = *it;
        existing.toLowerCase();
        if (existing == lowered) {
            it = grinders.erase(it);
        } else {
            ++it;
        }
    }

    grinders.insert(grinders.begin(), trimmed);

    if (grinders.size() > GRINDER_LIST_MAX) {
        grinders.resize(GRINDER_LIST_MAX);
    }

    bool ok = writeGrinders(grinders);

    if (locked) {
        xSemaphoreGive(_mutex);
    }
    return ok;
}

bool GrinderManager::writeGrinders(const std::vector<String> &grinders) {
    if (!ensureDirectory()) {
        return false;
    }

    File file = _fs->open(_path, "w");
    if (!file) {
        return false;
    }

    JsonDocument doc;
    JsonArray arr = doc["grinders"].to<JsonArray>();
    for (const auto &name : grinders) {
        arr.add(name);
    }
    bool ok = serializeJson(doc, file) > 0;
    file.close();
    return ok;
}
