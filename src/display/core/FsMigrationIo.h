#ifndef FS_MIGRATION_IO_H
#define FS_MIGRATION_IO_H

// PRO-218 — host-testable byte-moving half of the SPIFFS->LittleFS migration.
//
// WHY THIS EXISTS (review findings #1-#5): the original runner hardcoded the
// global SPIFFS / LittleFS, so the stage/restore path had ZERO host coverage —
// the silent-data-loss seam (unchecked writes, short reads, over-budget skips,
// fire-and-forget restore) shipped because nothing exercised it off-hardware.
//
// SEPARATION OF CONCERNS (mirrors FsMigration.h):
// The stage/restore/verify ORCHESTRATION here is pure logic that drives an
// abstract `IMigrationFs`. It performs no Arduino / ESP-IDF I/O directly, so it
// links and runs under `pio test -e native` against an in-memory fake FS. The
// device-side adapter (real SPIFFS/LittleFS) lives in FsMigrationRunner.cpp.
//
// The orchestration is deliberately strict: any open failure, any short read,
// any short write, any over-budget skip, or any post-restore size mismatch is
// reported as a FAILURE so the caller can refuse to stamp the once-only marker.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// One file staged in RAM during the migration, keyed by its restore-relative
// path (e.g. "p/9bar.json" or "h/12345.slog").
struct StagedFile {
    std::string relPath;
    std::vector<uint8_t> data;
    // ORIGINAL source size as reported by the directory listing, captured
    // BEFORE the read. `data.size()` may be smaller after a transient short
    // read (PRO-218 finding A); verification must compare against this original
    // size — not the possibly-truncated staged length — so a truncated restore
    // is actually detected instead of self-verifying against its own short size.
    uint32_t origSize = 0;
};

// A single regular-file entry discovered while listing a source directory.
struct FsDirEntry {
    std::string name; // base name only (e.g. "9bar.json")
    uint32_t size = 0;
};

// Minimal filesystem surface the migration needs. The device adapter wraps the
// real SPIFFS (source) and LittleFS (destination); host tests provide an
// in-memory fake. Read/write return the number of bytes actually transferred so
// the orchestration can detect short reads / short writes (review #4, #5).
struct IMigrationFs {
    virtual ~IMigrationFs() = default;

    // ---- source (read) side ----
    // True if `dir` exists and is a directory.
    virtual bool dirExists(const char *dir) = 0;
    // List the regular files directly under `dir` (one level; /p and /h are
    // flat). Returns false on an open/iterate failure.
    virtual bool listDir(const char *dir, std::vector<FsDirEntry> &out) = 0;
    // Read up to `size` bytes of "<dir>/<name>" into `out` (resized to the
    // bytes actually read). Returns the byte count read (== size on success).
    virtual size_t readFile(const char *dir, const char *name, uint32_t size, std::vector<uint8_t> &out) = 0;

    // ---- destination (write) side ----
    virtual bool makeDirs(const char *path) = 0;
    // Write `data` to `path`, truncating. Returns bytes written (== data.size()
    // on success; a short value signals ENOSPC / flash error).
    virtual size_t writeFile(const char *path, const uint8_t *data, size_t len) = 0;
    // Rename `from` to `to` (atomic-ish commit of a temp file). Returns success.
    virtual bool renameFile(const char *from, const char *to) = 0;
    // True if `path` exists on the destination.
    virtual bool destExists(const char *path) = 0;
    // Size of the destination file at `path`, or -1 if missing/unreadable.
    virtual int64_t destSize(const char *path) = 0;
};

// ---- stage result -----------------------------------------------------------

struct StageResult {
    uint32_t bytes = 0;     // total bytes staged into RAM
    uint32_t fileCount = 0; // files successfully staged
    uint32_t skipped = 0;   // files dropped (over budget) — a FAILURE signal
    bool shortRead = false; // a file read returned fewer bytes than its size
    bool ok = true;         // false if the directory could not be listed

    // True only when every regular file under the directory was staged in full.
    // Used to compute "history_preserved" honestly (review #5): a skip or a
    // short read must NOT be reported as a complete preservation.
    bool complete() const { return ok && skipped == 0 && !shortRead; }
};

// Stage every regular file directly under `srcDir` into `out`, keyed by
// "<dirLabel>/<filename>". `budget` caps the CUMULATIVE staged bytes; a file
// that would exceed it is recorded as `skipped` (not silently dropped). A read
// that returns fewer bytes than the file size sets `shortRead`. Appends to
// `out`; returns the per-directory result.
inline StageResult stageDir(IMigrationFs &fs, const char *srcDir, const char *dirLabel, uint32_t budget,
                            std::vector<StagedFile> &out) {
    StageResult r;
    if (!fs.dirExists(srcDir)) {
        return r; // nothing to stage; ok=true, complete()=true
    }
    std::vector<FsDirEntry> entries;
    if (!fs.listDir(srcDir, entries)) {
        r.ok = false;
        return r;
    }
    for (const auto &e : entries) {
        if (r.bytes + e.size > budget) {
            // Over budget: record the skip so the caller can refuse the marker.
            r.skipped++;
            continue;
        }
        StagedFile sf;
        sf.relPath = std::string(dirLabel) + "/" + e.name;
        sf.origSize = e.size; // capture the source size BEFORE the read (finding A)
        size_t got = fs.readFile(srcDir, e.name.c_str(), e.size, sf.data);
        if (got != e.size) {
            r.shortRead = true; // truncated read — treat as failure, not success
        }
        r.bytes += static_cast<uint32_t>(got);
        r.fileCount++;
        out.push_back(std::move(sf));
    }
    return r;
}

// Restore staged files into the destination. Each file is written to a temp
// path then renamed onto its real name, so a truncated/failed write never sits
// under the real name (review #4). A failed open, a short write, or a failed
// rename is counted as a failure. Returns the number of files FULLY restored;
// the caller compares it against staged.size() before stamping the marker.
inline uint32_t restoreStaged(IMigrationFs &fs, const std::vector<StagedFile> &staged) {
    uint32_t restored = 0;
    for (const auto &sf : staged) {
        std::string target = "/" + sf.relPath;
        std::string tmp = target + ".mig.tmp";
        size_t slash = target.find_last_of('/');
        if (slash != std::string::npos && slash > 0) {
            fs.makeDirs(target.substr(0, slash).c_str());
        }
        size_t wrote = fs.writeFile(tmp.c_str(), sf.data.empty() ? nullptr : sf.data.data(), sf.data.size());
        if (wrote != sf.data.size()) {
            continue; // short/failed write — leave the temp, do NOT count it
        }
        if (!fs.renameFile(tmp.c_str(), target.c_str())) {
            continue; // commit failed — do NOT count it
        }
        restored++;
    }
    return restored;
}

// Read-back verification (review #3, PRO-218 finding A): re-open each expected
// target, confirm it exists and its size matches the ORIGINAL source byte count
// (`origSize`), NOT the possibly-truncated staged `data.size()`. Comparing
// against the staged length would let a transient short read self-verify (the
// truncated bytes written == the truncated bytes staged) and seal corrupt data.
// Comparing against `origSize` means a truncated file fails verification, so the
// caller refuses the marker. Returns the number of files that verified good. The
// caller requires verified == staged.size().
inline uint32_t verifyRestored(IMigrationFs &fs, const std::vector<StagedFile> &staged) {
    uint32_t verified = 0;
    for (const auto &sf : staged) {
        std::string target = "/" + sf.relPath;
        if (!fs.destExists(target.c_str())) {
            continue;
        }
        int64_t sz = fs.destSize(target.c_str());
        if (sz < 0 || static_cast<uint64_t>(sz) != sf.origSize) {
            continue;
        }
        verified++;
    }
    return verified;
}

#endif // FS_MIGRATION_IO_H
