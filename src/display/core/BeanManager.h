#pragma once
#ifndef BEANMANAGER_H
#define BEANMANAGER_H

#include <FS.h>
#include <ArduinoJson.h>
#include <ctime>
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
        // Rescue: clear the unsafe ID so saveBean() will regenerate a safe one
        // on next write. This prevents silent data loss for beans imported from
        // external sources or older firmware whose IDs don't match the
        // [A-Za-z0-9_-]{1,32} alphabet. Path-traversal protection remains
        // intact at the network boundary (WebUIPlugin.cpp) and the save
        // boundary (saveBean rejects unsafe IDs at write time).
        ESP_LOGW("BeanManager", "Rescuing bean with unsafe ID; will regenerate on next save");
        bean.id = "";
    } else {
        bean.id = candidateId;
    }
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
    // A real Unix-seconds timestamp for this project must sit between
    // 2023-11-14 (1700000000, before the project existed) and "now + 1 year"
    // slack. Anything outside that window is treated as a stale millis()
    // value — which can range up to 4294967295 (~49 days of uptime) and
    // therefore overlaps the low end of the Unix-seconds range — and reset
    // to 0. The next saveBean() will backfill from NTP via the existing
    // `bean.createdAt == 0` guard. The upper bound is only applied when NTP
    // has synced; pre-NTP we keep questionable values and let the next
    // post-sync parse clean them up. This is an in-memory correction; the
    // file on disk is rewritten on next save.
    constexpr unsigned long LEGACY_TIMESTAMP_MIN = 1700000000UL;
    constexpr unsigned long ONE_YEAR_SECONDS = 31536000UL;
    const time_t nowTime = time(nullptr);
    const bool ntpValid = static_cast<unsigned long>(nowTime) >= LEGACY_TIMESTAMP_MIN;
    const unsigned long horizonMax = ntpValid ? static_cast<unsigned long>(nowTime) + ONE_YEAR_SECONDS : 0UL;

    auto isCorrupt = [&](unsigned long ts) {
        if (ts < LEGACY_TIMESTAMP_MIN) return true;
        return horizonMax != 0 && ts > horizonMax;
    };
    if (bean.createdAt != 0 && isCorrupt(bean.createdAt)) {
        bean.createdAt = 0;
    }
    if (bean.updatedAt != 0 && isCorrupt(bean.updatedAt)) {
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
