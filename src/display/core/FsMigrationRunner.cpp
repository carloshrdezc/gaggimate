#include "FsMigrationRunner.h"

#include "FsMigration.h"
#include <Arduino.h>
#include <LittleFS.h>
#include <SD_MMC.h>
#include <SPIFFS.h>
#include <vector>

// PRO-218 — device-side execution of the SPIFFS->LittleFS one-time migration.
// See FsMigrationRunner.h and FsMigration.h for the design / branch rationale.
//
// Hardware reality that shapes the staging strategy (verified against
// Controller.cpp / ShotHistoryPlugin.cpp on this fork):
//   * On SD-equipped devices, ProfileManager (/p) and ShotHistoryPlugin (/h)
//     store user data on the SD card, NOT on the internal data partition. The
//     SPIFFS->LittleFS reformat only touches the internal partition, so user
//     data on SD is already safe — we just format the internal partition clean.
//   * On NO-SD devices, /p and /h live on the internal data partition (the one
//     being reformatted). This is the dangerous case: we rescue /p (always
//     small) to RAM, and /h to RAM iff it fits a budget, else defer /h
//     (documented PRO-218 fallback — /p preserved at minimum).

namespace {

constexpr const char *LOG = "FsMigration";

// The data partition both filesystems live on (default partition table label).
constexpr const char *DATA_PARTITION_LABEL = "spiffs";
constexpr const char *LITTLEFS_BASE = "/littlefs";
constexpr const char *SPIFFS_BASE = "/old-spiffs";

// RAM staging budgets. Profiles are a few hundred bytes each; a handful is a
// few KB. Shot-history .slog files are larger; only rescue /h to RAM when the
// whole directory fits comfortably in heap, otherwise defer it.
constexpr uint32_t MAX_RAM_PROFILE_BYTES = 64u * 1024u;
constexpr uint32_t MAX_RAM_HISTORY_BYTES = 256u * 1024u;

// ---- staged-file container --------------------------------------------------

struct StagedFile {
    String relPath; // e.g. "p/9bar.json" or "h/12345.slog"
    std::vector<uint8_t> data;
};

// ---- small FS helpers -------------------------------------------------------

void mkdirs(fs::FS &dst, const String &path) {
    int slash = path.indexOf('/', 1);
    while (slash > 0) {
        String sub = path.substring(0, slash);
        if (!dst.exists(sub)) {
            dst.mkdir(sub);
        }
        slash = path.indexOf('/', slash + 1);
    }
    if (!dst.exists(path)) {
        dst.mkdir(path);
    }
}

String baseName(const String &full) {
    int slash = full.lastIndexOf('/');
    return slash >= 0 ? full.substring(slash + 1) : full;
}

// Total bytes of regular files directly under dir (one level; /p and /h are
// flat). Used to decide whether /h fits the RAM budget before staging it.
uint32_t dirBytes(fs::FS &src, const char *dir) {
    if (!src.exists(dir)) {
        return 0;
    }
    fs::File root = src.open(dir);
    if (!root || !root.isDirectory()) {
        return 0;
    }
    uint32_t total = 0;
    fs::File e = root.openNextFile();
    while (e) {
        if (!e.isDirectory()) {
            total += e.size();
        }
        e = root.openNextFile();
    }
    root.close();
    return total;
}

// Read every regular file directly under srcDir from SPIFFS into `out`, keyed
// by "<dirLabel>/<filename>". Bounded by budget (skips files that would exceed
// it, logging a warning). Returns total bytes staged.
uint32_t stageDirToRam(const char *srcDir, const char *dirLabel, uint32_t budget, std::vector<StagedFile> &out) {
    if (!SPIFFS.exists(srcDir)) {
        return 0;
    }
    fs::File root = SPIFFS.open(srcDir);
    if (!root || !root.isDirectory()) {
        return 0;
    }
    uint32_t total = 0;
    fs::File entry = root.openNextFile();
    while (entry) {
        if (!entry.isDirectory()) {
            uint32_t sz = entry.size();
            if (total + sz > budget) {
                ESP_LOGW(LOG, "RAM stage budget exceeded; skipping %s (%u bytes)", entry.name(), (unsigned)sz);
            } else {
                StagedFile sf;
                sf.relPath = String(dirLabel) + "/" + baseName(String(entry.name()));
                sf.data.resize(sz);
                size_t got = entry.read(sf.data.data(), sz);
                sf.data.resize(got);
                total += got;
                out.push_back(std::move(sf));
            }
        }
        entry = root.openNextFile();
    }
    root.close();
    return total;
}

// Write staged files back into LittleFS at "/<relPath>", creating parent dirs.
void restoreToLittleFs(const std::vector<StagedFile> &staged) {
    for (const auto &sf : staged) {
        String target = "/" + sf.relPath;
        int slash = target.lastIndexOf('/');
        if (slash > 0) {
            mkdirs(LittleFS, target.substring(0, slash));
        }
        fs::File out = LittleFS.open(target, FILE_WRITE);
        if (!out) {
            ESP_LOGE(LOG, "restore open failed: %s", target.c_str());
            continue;
        }
        if (!sf.data.empty()) {
            out.write(sf.data.data(), sf.data.size());
        }
        out.close();
    }
}

void writeMarker() {
    fs::File m = LittleFS.open(FS_MIGRATION_MARKER_PATH, FILE_WRITE);
    if (m) {
        m.print("1");
        m.close();
    } else {
        ESP_LOGE(LOG, "failed to write migration marker %s", FS_MIGRATION_MARKER_PATH);
    }
}

} // namespace

bool ensureDataPartitionMounted(uint8_t maxOpenFiles) {
    // STEP 1: try LittleFS WITHOUT formatOnFail. This is the critical change:
    // begin(false) must NOT reformat the partition, so an old SPIFFS image
    // survives long enough to be rescued.
    if (LittleFS.begin(false, LITTLEFS_BASE, maxOpenFiles, DATA_PARTITION_LABEL)) {
        // A valid LittleFS already mounted: fresh install or already migrated.
        FsMigrationInputs in;
        in.markerPresent = LittleFS.exists(FS_MIGRATION_MARKER_PATH);
        in.littleFsMountedClean = true;
        FsMigrationPlan plan = decideFsMigration(in);
        if (plan.action == FsMigrationAction::UseLittleFsAsIs && plan.writeMarker) {
            writeMarker(); // stamp so future boots short-circuit immediately
        }
        ESP_LOGI(LOG, "LittleFS mounted clean (marker=%d) — no migration needed", (int)in.markerPresent);
        return true;
    }

    ESP_LOGW(LOG, "LittleFS clean mount failed — probing for a pre-PRO-212 SPIFFS image");

    // STEP 2: the LittleFS mount failed. Before letting LittleFS format the
    // partition, mount the same partition as SPIFFS read-only and inspect it.
    bool spiffsMounted = SPIFFS.begin(false, SPIFFS_BASE, 10, DATA_PARTITION_LABEL);

    FsMigrationInputs in;
    in.markerPresent = false; // marker lives in LittleFS, which didn't mount
    in.littleFsMountedClean = false;
    in.spiffsMounted = spiffsMounted;
    in.spiffsHasProfiles = spiffsMounted && SPIFFS.exists("/p");
    in.spiffsHasHistory = spiffsMounted && SPIFFS.exists("/h");
    // SD card: detected/mounted by the driver earlier in setup() on display
    // builds. On SD devices the user's /p and /h live on SD (safe). cardType()
    // returns 0 (CARD_NONE on device / the sim's no-card stub) when absent; a
    // literal-0 compare is portable across the device enum and the sim's int.
    in.sdCardAvailable = (SD_MMC.cardType() != 0);
    in.ramBudgetBytes = MAX_RAM_HISTORY_BYTES;
    in.spiffsHistoryBytes = spiffsMounted ? dirBytes(SPIFFS, "/h") : 0;

    FsMigrationPlan plan = decideFsMigration(in);

    std::vector<StagedFile> staged;
    bool stagedHistory = false;
    if (plan.action == FsMigrationAction::MigrateFromSpiffs) {
        ESP_LOGW(LOG, "SPIFFS image found (p=%d h=%d sd=%d hbytes=%u) — migrating to LittleFS", (int)in.spiffsHasProfiles,
                 (int)in.spiffsHasHistory, (int)in.sdCardAvailable, (unsigned)in.spiffsHistoryBytes);

        // Stage from the internal SPIFFS to RAM only on no-SD devices; on SD
        // devices the user data is on SD and the internal SPIFFS holds at most
        // stale seed data, so staging=None and we just format clean.
        if (plan.staging == FsStagingTarget::RamProfilesOnly || plan.staging == FsStagingTarget::RamProfilesAndHistory) {
            stageDirToRam("/p", "p", MAX_RAM_PROFILE_BYTES, staged);
        }
        if (plan.staging == FsStagingTarget::RamProfilesAndHistory) {
            uint32_t h = stageDirToRam("/h", "h", MAX_RAM_HISTORY_BYTES, staged);
            stagedHistory = h > 0 || !in.spiffsHasHistory;
        }
    }

    // Release SPIFFS so LittleFS can take ownership of the same partition.
    if (spiffsMounted) {
        SPIFFS.end();
    }

    // STEP 3: format the partition as LittleFS (formatOnFail=true forces a
    // format since the partition is still SPIFFS-formatted or blank).
    if (!LittleFS.begin(true, LITTLEFS_BASE, maxOpenFiles, DATA_PARTITION_LABEL)) {
        ESP_LOGE(LOG, "LittleFS format/mount FAILED — partition unusable");
        return false;
    }

    // STEP 4: restore staged data (no-op when staging was None).
    if (!staged.empty()) {
        restoreToLittleFs(staged);
    }

    // STEP 5: stamp the once-only marker so the migration never re-runs.
    if (plan.writeMarker) {
        writeMarker();
    }

    if (plan.action == FsMigrationAction::MigrateFromSpiffs) {
        if (plan.historyDeferred) {
            ESP_LOGW(LOG,
                     "shot history (/h, %u bytes) too large to stage in RAM without an SD card "
                     "— NOT preserved; profiles (/p) were preserved",
                     (unsigned)in.spiffsHistoryBytes);
        }
        ESP_LOGI(LOG, "SPIFFS->LittleFS migration complete (history_preserved=%d)", (int)stagedHistory);
    } else {
        ESP_LOGI(LOG, "fresh LittleFS ready (no recoverable SPIFFS data)");
    }
    return true;
}
