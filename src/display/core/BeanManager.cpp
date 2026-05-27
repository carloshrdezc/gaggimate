#include "BeanManager.h"

#include <algorithm>
#include <ctime>
#include <utility>

BeanManager::BeanManager(fs::FS *fs, String dir) : _fs(fs), _dir(std::move(dir)) {}

void BeanManager::setup() { ensureDirectory(); }

bool BeanManager::ensureDirectory() const {
    if (!_fs->exists(_dir)) {
        return _fs->mkdir(_dir);
    }
    return true;
}

String BeanManager::beanPath(const String &uuid) const { return _dir + "/" + uuid + ".json"; }

std::vector<BeanEntry> BeanManager::listBeans() {
    std::vector<BeanEntry> beans;
    std::vector<String> rescuedIds;
    File root = _fs->open(_dir);
    if (!root || !root.isDirectory()) {
        return beans;
    }

    File file = root.openNextFile();
    while (file) {
        String name = file.name();
        if (name.endsWith(".json")) {
            String stem = name;
            int slash = stem.lastIndexOf('/');
            if (slash >= 0) {
                stem = stem.substring(slash + 1);
            }
            if (stem.endsWith(".json")) {
                stem = stem.substring(0, stem.length() - 5);
            }
            // Skip files we just rescued (saveBean above wrote a new file under
            // a generated safe id; the directory iterator may still hand us
            // that newly-created file in this same pass).
            if (std::find(rescuedIds.begin(), rescuedIds.end(), stem) != rescuedIds.end()) {
                file = root.openNextFile();
                continue;
            }
            BeanEntry bean{};
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, file);
            if (!err && parseBean(doc.as<JsonObject>(), bean)) {
                if (!applyFallbackBeanId(bean, stem)) {
                    // Both JSON id and filename stem are unsafe. Generate a
                    // fresh safe id, persist the rescued bean under it, and
                    // remove the original unsafe file so the UI no longer
                    // sees an unmanageable empty-id row.
                    bean.id = generateShortID();
                    file.close();
                    if (saveBean(bean)) {
                        rescuedIds.push_back(bean.id);
                        // Use beanPath(stem) — not 'name' — to guarantee an
                        // absolute path. file.name() from openNextFile() returns
                        // a platform-dependent value that may omit the leading
                        // '/' (e.g. "b/ID.json" instead of "/b/ID.json"),
                        // causing LittleFS.remove() to silently fail and leaving
                        // the original unsafe file on disk. Every subsequent
                        // listBeans() call would then re-rescue it with a new
                        // random ID, producing unbounded duplicates.
                        if (!_fs->remove(beanPath(stem))) {
                            ESP_LOGW("BeanManager", "Rescued bean %s but could not remove original file", name.c_str());
                        }
                        beans.push_back(bean);
                    } else {
                        ESP_LOGE("BeanManager", "Failed to rescue bean file with generated id: %s", name.c_str());
                    }
                    file = root.openNextFile();
                    continue;
                }
                beans.push_back(bean);
            }
        }
        file = root.openNextFile();
    }

    std::sort(beans.begin(), beans.end(), [](const BeanEntry &left, const BeanEntry &right) {
        if (left.archived != right.archived) {
            return !left.archived;
        }
        if (left.updatedAt != right.updatedAt) {
            return left.updatedAt > right.updatedAt;
        }
        return left.name < right.name;
    });

    return beans;
}

bool BeanManager::loadBean(const String &uuid, BeanEntry &outBean) {
    File file = _fs->open(beanPath(uuid), "r");
    if (!file) {
        return false;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, file);
    file.close();
    if (err) {
        return false;
    }

    if (!parseBean(doc.as<JsonObject>(), outBean)) {
        return false;
    }

    if (!applyFallbackBeanId(outBean, uuid)) {
        ESP_LOGW("BeanManager", "Skipping unrescuable bean file (no safe id derivable): %s", uuid.c_str());
        return false;
    }

    return true;
}

bool BeanManager::saveBean(BeanEntry &bean) {
    if (!ensureDirectory()) {
        return false;
    }

    if (bean.id.isEmpty()) {
        bean.id = generateShortID();
    }

    // Use NTP-derived Unix seconds (matches ShotHistoryPlugin convention).
    // If NTP has not synced yet, leave timestamps at 0; the next save will
    // backfill via the existing `bean.createdAt == 0` guard.
    time_t now;
    time(&now);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    const bool ntpValid = timeinfo.tm_year > (2020 - 1900);
    const unsigned long nowSec = ntpValid ? static_cast<unsigned long>(now) : 0UL;

    if (bean.createdAt == 0 && nowSec != 0) {
        bean.createdAt = nowSec;
    }
    if (nowSec != 0) {
        bean.updatedAt = nowSec;
    }

    File file = _fs->open(beanPath(bean.id), "w");
    if (!file) {
        return false;
    }

    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    writeBean(obj, bean);
    bool ok = serializeJson(doc, file) > 0;
    file.close();
    return ok;
}

bool BeanManager::deleteBean(const String &uuid) { return _fs->remove(beanPath(uuid)); }

bool BeanManager::beanExists(const String &uuid) { return _fs->exists(beanPath(uuid)); }
