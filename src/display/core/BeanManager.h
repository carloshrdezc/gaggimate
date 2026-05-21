#pragma once
#ifndef BEANMANAGER_H
#define BEANMANAGER_H

#include <FS.h>
#include <ArduinoJson.h>
#include <display/core/utils.h>
#include <vector>

struct BeanEntry {
    String id;
    String name;
    String roaster;
    String roastLevel;
    String roastDate;
    String origin;
    String process;
    String notes;
    float quantity = -1.0f;
    bool archived = false;
    unsigned long createdAt = 0;
    unsigned long updatedAt = 0;
};

inline bool parseBean(const JsonObject &obj, BeanEntry &bean) {
    const String candidateId = obj["id"] | "";
    // Reject IDs containing path separators or other unsafe chars before they
    // reach any filesystem helper. Empty IDs are tolerated here because
    // saveBean() generates one when the field is missing.
    if (!candidateId.isEmpty() && !isSafeId(candidateId)) {
        return false;
    }
    bean.id = candidateId;
    bean.name = obj["name"] | "";
    bean.roaster = obj["roaster"] | "";
    bean.roastLevel = obj["roastLevel"] | "";
    bean.roastDate = obj["roastDate"] | "";
    bean.origin = obj["origin"] | "";
    bean.process = obj["process"] | "";
    bean.notes = obj["notes"] | "";
    bean.quantity = obj["quantity"].is<float>() || obj["quantity"].is<double>() || obj["quantity"].is<int>()
                        ? obj["quantity"].as<float>()
                        : -1.0f;
    bean.archived = obj["archived"] | false;
    bean.createdAt = obj["createdAt"] | 0UL;
    bean.updatedAt = obj["updatedAt"] | 0UL;

    // Migrate legacy millis()-based timestamps written by pre-CAR-102 firmware.
    // Any value below 2023-11-14 (1700000000 Unix seconds) is too small to be
    // a real Unix timestamp for this project's lifetime — treat as corrupt
    // and reset to 0. The next saveBean() will backfill from NTP via the
    // existing `bean.createdAt == 0` guard. This is an in-memory correction;
    // the file on disk is rewritten on next save.
    constexpr unsigned long LEGACY_TIMESTAMP_THRESHOLD = 1700000000UL;
    if (bean.createdAt != 0 && bean.createdAt < LEGACY_TIMESTAMP_THRESHOLD) {
        bean.createdAt = 0;
    }
    if (bean.updatedAt != 0 && bean.updatedAt < LEGACY_TIMESTAMP_THRESHOLD) {
        bean.updatedAt = 0;
    }

    return !bean.name.isEmpty();
}

inline void writeBean(JsonObject &obj, const BeanEntry &bean) {
    obj["id"] = bean.id;
    obj["name"] = bean.name;
    obj["roaster"] = bean.roaster;
    obj["roastLevel"] = bean.roastLevel;
    obj["roastDate"] = bean.roastDate;
    obj["origin"] = bean.origin;
    obj["process"] = bean.process;
    obj["notes"] = bean.notes;
    if (bean.quantity >= 0.0f) {
        obj["quantity"] = bean.quantity;
    } else {
        obj["quantity"] = nullptr;
    }
    obj["archived"] = bean.archived;
    obj["createdAt"] = bean.createdAt;
    obj["updatedAt"] = bean.updatedAt;
}

class BeanManager {
  public:
    BeanManager(fs::FS *fs, String dir);

    void setup();
    std::vector<BeanEntry> listBeans();
    bool loadBean(const String &uuid, BeanEntry &outBean);
    bool saveBean(BeanEntry &bean);
    bool deleteBean(const String &uuid);
    bool beanExists(const String &uuid);

  private:
    fs::FS *_fs;
    String _dir;

    bool ensureDirectory() const;
    String beanPath(const String &uuid) const;
};

#endif // BEANMANAGER_H
