#include "BeanManager.h"

#include <algorithm>
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

    const unsigned long now = millis();
    if (bean.createdAt == 0) {
        bean.createdAt = now;
    }
    bean.updatedAt = now;

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
