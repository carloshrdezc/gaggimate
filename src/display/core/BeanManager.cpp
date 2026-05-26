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
    File root = _fs->open(_dir);
    if (!root || !root.isDirectory()) {
        return beans;
    }

    File file = root.openNextFile();
    while (file) {
        String name = file.name();
        if (name.endsWith(".json")) {
            BeanEntry bean{};
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, file);
            if (!err && parseBean(doc.as<JsonObject>(), bean)) {
                if (bean.id.isEmpty()) {
                    String fallbackId = name;
                    int slash = fallbackId.lastIndexOf('/');
                    if (slash >= 0) {
                        fallbackId = fallbackId.substring(slash + 1);
                    }
                    if (fallbackId.endsWith(".json")) {
                        fallbackId = fallbackId.substring(0, fallbackId.length() - 5);
                    }
                    if (isSafeId(fallbackId)) {
                        bean.id = fallbackId;
                    }
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

    return parseBean(doc.as<JsonObject>(), outBean);
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
