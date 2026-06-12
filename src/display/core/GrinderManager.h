#pragma once
#ifndef GRINDERMANAGER_H
#define GRINDERMANAGER_H

#include <ArduinoJson.h>
#include <FS.h>
#include <vector>

// Maximum number of grinder names retained on the device. Grinder names are
// tiny strings and the list is purely a convenience for the Shot Notes
// autocomplete, so a generous-but-bounded cap keeps the JSON file small and
// avoids unbounded growth from typos.
constexpr size_t GRINDER_LIST_MAX = 50;

// Maximum accepted length of a single grinder name (characters). Anything
// longer is rejected at the boundary so a malformed/oversized payload can't
// bloat the stored file.
constexpr size_t GRINDER_NAME_MAX_LEN = 64;

/**
 * GrinderManager persists the set of grinder names the user has entered in the
 * Shot History "Shot Notes" form so they can be offered as autocomplete
 * suggestions on later shots. Unlike beans/profiles (one file per entity), the
 * grinder list is a single JSON file holding an ordered array of name strings:
 *
 *   { "grinders": ["Niche Zero", "DF64", ...] }
 *
 * Order is most-recently-used first. Names are deduplicated case-insensitively
 * (the most recent casing wins) and the list is capped at GRINDER_LIST_MAX.
 */
class GrinderManager {
  public:
    GrinderManager(fs::FS *fs, String path);

    void setup();

    // Returns the stored grinder names, most-recently-used first.
    std::vector<String> listGrinders();

    // Records `name` as the most-recently-used grinder, deduplicating
    // case-insensitively and capping the list. Returns false if the name is
    // empty/blank or too long (in which case nothing is written).
    bool recordGrinder(const String &name);

  private:
    fs::FS *_fs;
    String _path;

    bool ensureDirectory() const;
    bool writeGrinders(const std::vector<String> &grinders);
};

#endif // GRINDERMANAGER_H
