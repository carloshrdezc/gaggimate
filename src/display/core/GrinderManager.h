#pragma once
#ifndef GRINDERMANAGER_H
#define GRINDERMANAGER_H

#include <ArduinoJson.h>
#include <FS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
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
    // case-insensitively and capping the list. Returns false only on a write
    // failure; an empty/blank or oversized name is skipped and still returns
    // true (it delegates to recordGrinders, whose all-skipped input is a no-op,
    // not an error). Callers must not branch on the return value to detect a
    // rejected name.
    bool recordGrinder(const String &name);

    // Records multiple names in one locked read-modify-write. Names are
    // prepended most-recently-first (so the LAST element of `names` ends up
    // nearest the front), deduplicated case-insensitively against each other
    // and the existing list, and the result is capped. Empty/oversized names
    // are skipped. This is the authoritative merge used by the web client's
    // batch sync, so the client never has to model the device's eviction
    // policy. Returns false only on a write failure; an all-skipped input that
    // changes nothing still returns true.
    bool recordGrinders(const std::vector<String> &names);

  private:
    fs::FS *_fs;
    String _path;

    // Serializes the read-modify-write of the grinder file. recordGrinder() is
    // reachable from two concurrent FreeRTOS contexts (the AsyncWebServer/local
    // WebSocket callback and the cloud-relay task `relayLoopTask`, pinned to a
    // different core), so an unguarded read-modify-write could last-write-wins
    // drop a name or let a read observe a torn/truncated file mid-write.
    SemaphoreHandle_t _mutex = nullptr;

    bool ensureDirectory() const;
    bool writeGrinders(const std::vector<String> &grinders);

    // Reads and parses the grinder file WITHOUT taking _mutex. Callers must hold
    // _mutex (or accept being lock-free when creation failed). recordGrinder()
    // calls this directly so it can take _mutex exactly once and avoid the
    // self-deadlock that nesting locks through the public listGrinders() would
    // cause with a non-recursive mutex.
    std::vector<String> listGrindersUnlocked();
};

#endif // GRINDERMANAGER_H
