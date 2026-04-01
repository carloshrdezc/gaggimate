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
    bean.id = obj["id"] | "";
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
