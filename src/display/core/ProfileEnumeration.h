#ifndef PROFILEENUMERATION_H
#define PROFILEENUMERATION_H

#include <vector>

// String comes from the real Arduino WString on firmware/sim builds and from
// the [env:native] host shim (test/native/Arduino.h, on the -I test/native
// include path) for host unit tests. Both expose the lastIndexOf/substring/
// endsWith subset this header uses.
#include <Arduino.h>

// PRO-354: the PURE name->stem->path logic shared by every boot-time
// ProfileManager directory scan (collectProfileIdMigrations,
// remintUnsafeProfileIds, findFilenameStemForId, listProfiles, and the
// setup-time id scan).
//
// Before this header those scans each carried their own copy of the
// filename-stem extraction (strip directory + strip a trailing ".json") and
// the directory-qualified path reconstruction (dir + "/" + stem + ".json").
// That logic is pure String arithmetic with NO fs::FS* / fs::File / Arduino-SD
// dependency, so splitting it out here (mirroring how SdReadRetryPolicy.h
// split the pure size/gate decisions out of the FS-coupled read loop) gives:
//
//   1. ONE definition of filenameStem() / the path reconstruction, so the
//      scanners can never drift apart, and
//   2. a host-linkable helper the [env:native] unit tests exercise directly
//      (round-trip of plain / dotted / directory-qualified names, the .json
//      filter, the empty-name edge) without an ESP32/SD/FS backend.
//
// The fs::FS* enumeration itself (root->openNextFile() loop) STAYS in
// ProfileManager.cpp; only the pure name math lives here. Helpers are `inline`
// so multiple translation units including this header don't violate the ODR
// (same pattern SdReadRetryPolicy.h uses with constexpr).

// Extract the addressable filename stem from a directory entry name.
//
// File::name() on the ESP32 Arduino FS backends (LittleFS/SD) returns the bare
// basename (e.g. "abc.json"), but a directory-qualified name (e.g.
// "/p/abc.json") can also appear, so strip a leading directory via the last
// '/' first, then strip a SINGLE trailing ".json" extension. Dotted names are
// preserved: "my.profile.json" -> "my.profile" (only the final ".json" is
// removed). A name without a ".json" extension is returned basename-only.
inline String filenameStem(const String &name) {
    String stem = name;
    int slash = stem.lastIndexOf('/');
    if (slash >= 0) {
        stem = stem.substring(slash + 1);
    }
    if (stem.endsWith(".json")) {
        stem = stem.substring(0, stem.length() - 5);
    }
    return stem;
}

// True if a directory entry name is a profile JSON candidate. Mirrors the
// `name.endsWith(".json")` filter every enumeration loop applies before pushing
// a name onto its candidate list.
inline bool isProfileFilename(const String &name) { return name.endsWith(".json"); }

// Reconstruct the canonical directory-qualified path for a profile entry.
//
// This is the exact inverse the scanners apply after enumerating: from a (bare
// or directory-qualified) entry `name`, rebuild `dir + "/" + filenameStem(name)
// + ".json"` so a subsequent open/remove resolves the real file in `dir` rather
// than against the FS root (File::name() drops the directory on the ESP32
// backends). PURE: no fs::FS* dependency.
inline String reconstructProfilePath(const String &dir, const String &name) { return dir + "/" + filenameStem(name) + ".json"; }

// A single enumerated candidate: its addressable stem and the reconstructed
// canonical path within the scanned directory.
struct ProfileEntry {
    String stem; // filenameStem(name)
    String path; // dir + "/" + stem + ".json"
};

// Filter + reconstruct over a list of raw directory-entry names.
//
// Takes the names a caller has ALREADY enumerated off the live fs::FS* handle
// (so the I/O stays in ProfileManager.cpp) and yields, for every ".json" entry,
// its stem and reconstructed path. This is the pure loop structure the FS-bound
// scanners share; routing a std::vector<String> through it keeps the
// name->stem->path math host-testable while leaving openNextFile() in the
// firmware. Non-".json" names are skipped, preserving the existing filter.
inline std::vector<ProfileEntry> reconstructProfileEntries(const String &dir, const std::vector<String> &names) {
    std::vector<ProfileEntry> entries;
    entries.reserve(names.size());
    for (const String &name : names) {
        if (!isProfileFilename(name)) {
            continue;
        }
        const String stem = filenameStem(name);
        entries.push_back(ProfileEntry{stem, dir + "/" + stem + ".json"});
    }
    return entries;
}

#endif // PROFILEENUMERATION_H
