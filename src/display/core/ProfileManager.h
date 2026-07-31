#pragma once
#ifndef PROFILEMANAGER_H
#define PROFILEMANAGER_H
#include "PluginManager.h"
#include <FS.h>
#include <display/core/Settings.h>
#include <display/core/utils.h>
#include <display/models/profile.h>

class ProfileManager {
  public:
    ProfileManager(fs::FS *fs, String dir, Settings &settings, PluginManager *plugin_manager);

    void setup();
    std::vector<String> listProfiles();
    bool loadProfile(const String &uuid, Profile &outProfile);
    bool saveProfile(Profile &profile);
    bool deleteProfile(const String &uuid);
    bool profileExists(const String &uuid);
    void selectProfile(const String &uuid);
    Profile &getSelectedProfile();
    bool loadSelectedProfile(Profile &outProfile);
    std::vector<String> getFavoritedProfiles(bool validate = false);

    void addFavoritedProfile(String id);
    void removeFavoritedProfile(String id);

  private:
    Profile selectedProfile{};
    PluginManager *_plugin_manager;
    // PRO-608: ProfileManager is constructed with the single global Settings and is neither copied
    // nor reassigned. Switching to a pointer would only trade a compile-time non-null guarantee for
    // a runtime one. Tracked in PRO-612.
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
    Settings &_settings;
    fs::FS *_fs;
    String _dir;
    bool ensureDirectory() const;
    String profilePath(const String &uuid) const;
    void migrate();
};

#endif // PROFILEMANAGER_H
