#include "FsMigrationRunner.h"

#include "FsMigration.h"
#include "FsMigrationIo.h"
#include <Arduino.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <SPIFFS.h>
#include <esp_heap_caps.h>
#include <string>
#include <vector>

// PRO-218 — device-side execution of the SPIFFS->LittleFS one-time migration.
// See FsMigrationRunner.h and FsMigration.h for the design / branch rationale.
//
// Storage-routing reality that shapes the staging strategy (single source of
// truth — verified against Controller.cpp / ShotHistoryPlugin.cpp on this fork):
//   * Per-boot, ProfileManager (/p) and ShotHistoryPlugin (/h) route writes to
//     &SD_MMC when an SD card is present, else to &LittleFS. This routing is
//     NOT persisted, so the physical location of a device's user data depends
//     only on whether an SD card happens to be inserted on a given boot.
//   * Because routing is not persisted, a device that always ran without an SD
//     card keeps its only /p,/h on the internal partition. We therefore rescue
//     /p,/h from the internal SPIFFS image whenever they are present, REGARDLESS
//     of SD state (review #6) — never reformat over the only copy.
//   * Staging is RAM-only (a single internal partition cannot hold both the old
//     SPIFFS image and a new LittleFS at once). /p is always rescued; /h is
//     rescued when it fits the RAM budget, else deferred (documented fallback).

namespace {

constexpr const char *LOG = "FsMigration";

// The data partition both filesystems live on (default partition table label).
constexpr const char *DATA_PARTITION_LABEL = "spiffs";
constexpr const char *LITTLEFS_BASE = "/littlefs";
constexpr const char *SPIFFS_BASE = "/old-spiffs";

// SPIFFS.begin() max-open-files for the read-only probe. Named (review P3): we
// only walk /p and /h sequentially, so a small handle pool is plenty.
constexpr uint8_t SPIFFS_PROBE_MAX_OPEN_FILES = 10;

// RAM staging budgets. Profiles are a few hundred bytes each; a handful is a
// few KB. Shot-history .slog files are larger. The two budgets share ONE
// remaining-heap envelope (review #7): /h may use only what is left after /p.
constexpr uint32_t MAX_RAM_PROFILE_BYTES = 64u * 1024u;
constexpr uint32_t MAX_RAM_HISTORY_BYTES = 256u * 1024u;

// Heap safety margin kept free during staging so the rest of early boot
// (framebuffer / BLE / UI buffers) does not OOM. ESP32 firmware is
// -fno-exceptions, so a failed allocation aborts rather than throwing; we must
// gate the largest contiguous free block BEFORE resizing the staging buffers.
constexpr uint32_t HEAP_SAFETY_MARGIN_BYTES = 48u * 1024u;

// Durable "migration in progress" sentinel (review #1). Written to NVS BEFORE
// the destructive format and cleared only AFTER a verified-good restore. If a
// clean LittleFS mounts on a later boot while this is still set, the partition
// contents are SUSPECT (a power cut hit the format->restore window) and we log
// loudly instead of silently blessing them.
constexpr const char *NVS_NAMESPACE = "fsmig";
constexpr const char *NVS_INPROGRESS_KEY = "inprog";

void setMigrationInProgress(bool inProgress) {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false)) {
        ESP_LOGE(LOG, "NVS open failed — migration sentinel unavailable");
        return;
    }
    if (inProgress) {
        prefs.putBool(NVS_INPROGRESS_KEY, true);
    } else {
        prefs.remove(NVS_INPROGRESS_KEY);
    }
    prefs.end();
}

bool migrationInProgressFlagged() {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, true)) {
        return false; // namespace not yet created => never started a migration
    }
    bool v = prefs.getBool(NVS_INPROGRESS_KEY, false);
    prefs.end();
    return v;
}

// ---- device adapter: drives the host-testable orchestration over real FS ----
//
// `source` is the read-only SPIFFS image; `dest` is the freshly-formatted
// LittleFS. The byte-moving logic itself (short-read / short-write / over-budget
// / verify) lives in FsMigrationIo.h and is unit-tested on the host against an
// in-memory fake; this adapter just maps that interface onto the real fs::FS.
class DeviceMigrationFs : public IMigrationFs {
  public:
    DeviceMigrationFs(fs::FS &source, fs::FS &dest) : _src(source), _dst(dest) {}

    bool dirExists(const char *dir) override { return _src.exists(dir); }

    bool listDir(const char *dir, std::vector<FsDirEntry> &out) override {
        fs::File root = _src.open(dir);
        if (!root || !root.isDirectory()) {
            return false;
        }
        for (fs::File e = root.openNextFile(); e; e = root.openNextFile()) {
            if (!e.isDirectory()) {
                FsDirEntry ent;
                ent.name = baseName(e.name());
                ent.size = static_cast<uint32_t>(e.size());
                out.push_back(ent);
            }
        }
        root.close();
        return true;
    }

    size_t readFile(const char *dir, const char *name, uint32_t size, std::vector<uint8_t> &out) override {
        std::string path = std::string(dir) + "/" + name;
        fs::File in = _src.open(path.c_str(), FILE_READ);
        if (!in) {
            out.clear();
            return 0;
        }
        out.resize(size);
        size_t got = size ? in.read(out.data(), size) : 0;
        in.close();
        out.resize(got);
        return got;
    }

    bool makeDirs(const char *path) override {
        String p(path);
        int slash = p.indexOf('/', 1);
        while (slash > 0) {
            String sub = p.substring(0, slash);
            if (!_dst.exists(sub)) {
                _dst.mkdir(sub);
            }
            slash = p.indexOf('/', slash + 1);
        }
        if (!_dst.exists(p)) {
            _dst.mkdir(p);
        }
        return true;
    }

    size_t writeFile(const char *path, const uint8_t *data, size_t len) override {
        // Remove any stale temp/target before writing so a rename can't fail on
        // an existing destination.
        if (_dst.exists(path)) {
            _dst.remove(path);
        }
        fs::File out = _dst.open(path, FILE_WRITE);
        if (!out) {
            ESP_LOGE(LOG, "restore open failed: %s", path);
            return 0;
        }
        size_t wrote = len ? out.write(data, len) : 0;
        out.flush();
        out.close();
        return wrote;
    }

    bool renameFile(const char *from, const char *to) override {
        if (_dst.exists(to)) {
            _dst.remove(to);
        }
        return _dst.rename(from, to);
    }

    bool destExists(const char *path) override { return _dst.exists(path); }

    int64_t destSize(const char *path) override {
        fs::File f = _dst.open(path, FILE_READ);
        if (!f) {
            return -1;
        }
        int64_t sz = static_cast<int64_t>(f.size());
        f.close();
        return sz;
    }

  private:
    static std::string baseName(const char *full) {
        std::string s(full ? full : "");
        size_t slash = s.find_last_of('/');
        return slash == std::string::npos ? s : s.substr(slash + 1);
    }

    fs::FS &_src;
    fs::FS &_dst;
};

// Total bytes of regular files directly under `dir` (one level; /p and /h are
// flat). Used to size-gate /p and /h before staging them.
uint32_t dirBytes(fs::FS &src, const char *dir) {
    if (!src.exists(dir)) {
        return 0;
    }
    fs::File root = src.open(dir);
    if (!root || !root.isDirectory()) {
        return 0;
    }
    uint32_t total = 0;
    for (fs::File e = root.openNextFile(); e; e = root.openNextFile()) {
        if (!e.isDirectory()) {
            total += static_cast<uint32_t>(e.size());
        }
    }
    root.close();
    return total;
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

// Largest contiguous 8-bit-capable free block, used to gate RAM staging so we
// never resize() past available heap and trip the -fno-exceptions abort().
uint32_t largestFreeBlock() { return static_cast<uint32_t>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)); }

} // namespace

bool ensureDataPartitionMounted(uint8_t maxOpenFiles, bool sdCardAvailable) {
    // STEP 1: try LittleFS WITHOUT formatOnFail. begin(false) must NOT reformat
    // the partition, so an old SPIFFS image survives long enough to be rescued.
    if (LittleFS.begin(false, LITTLEFS_BASE, maxOpenFiles, DATA_PARTITION_LABEL)) {
        // Review #1: a clean LittleFS mount while the in-progress sentinel is
        // still set means a power cut hit the previous run's format->restore
        // window. The contents are suspect — log loudly. We still clear the
        // sentinel and proceed (the next-boot path is non-destructive and the
        // device must stay usable), but this surfaces the data-loss event.
        if (migrationInProgressFlagged()) {
            ESP_LOGE(LOG, "LittleFS mounted clean but migration sentinel was still set — a previous migration was "
                          "INTERRUPTED mid-restore; LittleFS contents may be incomplete (possible data loss)");
            setMigrationInProgress(false);
        }
        FsMigrationInputs in;
        in.markerPresent = LittleFS.exists(FS_MIGRATION_MARKER_PATH);
        in.littleFsMountedClean = true;
        FsMigrationPlan plan = decideFsMigration(in);
        if (plan.action == FsMigrationAction::UseLittleFsAsIs && plan.writeMarker) {
            writeMarker(); // stamp so future boots short-circuit immediately
        }
        ESP_LOGI(LOG, "LittleFS mounted clean (marker=%d) — no migration needed", static_cast<int>(in.markerPresent));
        return true;
    }

    ESP_LOGW(LOG, "LittleFS clean mount failed — probing for a pre-PRO-212 SPIFFS image");

    // STEP 2: the LittleFS mount failed. Before letting LittleFS format the
    // partition, mount the same partition as SPIFFS read-only and inspect it.
    bool spiffsMounted = SPIFFS.begin(false, SPIFFS_BASE, SPIFFS_PROBE_MAX_OPEN_FILES, DATA_PARTITION_LABEL);

    FsMigrationInputs in;
    in.markerPresent = false; // marker lives in LittleFS, which didn't mount
    in.littleFsMountedClean = false;
    in.spiffsMounted = spiffsMounted;
    in.spiffsHasProfiles = spiffsMounted && SPIFFS.exists("/p");
    in.spiffsHasHistory = spiffsMounted && SPIFFS.exists("/h");
    // SD state is provided authoritatively by the caller (review #3): Controller
    // already owns the SD detect result; re-probing SD_MMC.cardType() here
    // depends on undocumented driver behavior when SD was never begun.
    in.sdCardAvailable = sdCardAvailable;
    in.ramHistoryBudgetBytes = MAX_RAM_HISTORY_BYTES;
    in.ramProfileBudgetBytes = MAX_RAM_PROFILE_BYTES;
    in.spiffsProfileBytes = spiffsMounted ? dirBytes(SPIFFS, "/p") : 0;
    in.spiffsHistoryBytes = spiffsMounted ? dirBytes(SPIFFS, "/h") : 0;

    FsMigrationPlan plan = decideFsMigration(in);

    // Review #3/#5/#6: profiles are too large to stage safely. Refuse to
    // reformat — that would erase the only copy of the user's profiles. Leave
    // SPIFFS intact and retry on the next boot. The marker is NOT written.
    if (plan.abortFormatToPreserveProfiles) {
        ESP_LOGE(LOG,
                 "ABORTING migration: /p (%u bytes) exceeds the %u-byte profile RAM budget; refusing to reformat "
                 "over the only copy of profiles. Leaving SPIFFS intact; will retry next boot.",
                 static_cast<unsigned>(in.spiffsProfileBytes), static_cast<unsigned>(MAX_RAM_PROFILE_BYTES));
        if (spiffsMounted) {
            SPIFFS.end();
        }
        return false;
    }

    // Stage the rescuable data to RAM BEFORE the destructive format. The two
    // budgets share one remaining-heap envelope (review #7): /h gets only the
    // heap left after /p, and each stage is gated on the largest free block so
    // we never trip the -fno-exceptions abort() mid-migration.
    std::vector<StagedFile> staged;
    StageResult profileStage;
    StageResult historyStage;
    bool stageFailedOnHeap = false;

    if (plan.action == FsMigrationAction::MigrateFromSpiffs) {
        ESP_LOGW(LOG, "SPIFFS image found (p=%d h=%d sd=%d pbytes=%u hbytes=%u) — migrating to LittleFS",
                 static_cast<int>(in.spiffsHasProfiles), static_cast<int>(in.spiffsHasHistory),
                 static_cast<int>(in.sdCardAvailable), static_cast<unsigned>(in.spiffsProfileBytes),
                 static_cast<unsigned>(in.spiffsHistoryBytes));

        DeviceMigrationFs devFs(SPIFFS, LittleFS); // dest unused until after format; only source read here

        // Heap guard for /p (review #7): /p must always fit, but never abort.
        if (plan.staging == FsStagingTarget::RamProfilesOnly || plan.staging == FsStagingTarget::RamProfilesAndHistory) {
            uint32_t free = largestFreeBlock();
            if (in.spiffsProfileBytes + HEAP_SAFETY_MARGIN_BYTES > free) {
                ESP_LOGE(LOG, "insufficient heap to stage /p (need %u + %u margin, have %u) — aborting, SPIFFS left intact",
                         static_cast<unsigned>(in.spiffsProfileBytes), static_cast<unsigned>(HEAP_SAFETY_MARGIN_BYTES),
                         static_cast<unsigned>(free));
                stageFailedOnHeap = true;
            } else {
                profileStage = stageDir(devFs, "/p", "p", MAX_RAM_PROFILE_BYTES, staged);
            }
        }

        if (!stageFailedOnHeap && plan.staging == FsStagingTarget::RamProfilesAndHistory) {
            // Shared envelope: /h may use only what is left after /p + margin.
            uint32_t free = largestFreeBlock();
            uint32_t remaining =
                free > (profileStage.bytes + HEAP_SAFETY_MARGIN_BYTES) ? free - profileStage.bytes - HEAP_SAFETY_MARGIN_BYTES : 0;
            uint32_t historyBudget = remaining < MAX_RAM_HISTORY_BYTES ? remaining : MAX_RAM_HISTORY_BYTES;
            if (in.spiffsHistoryBytes > historyBudget) {
                // Not enough heap right now: defer /h rather than abort. /p is
                // already staged and will be preserved.
                ESP_LOGW(LOG, "deferring /h: %u bytes exceeds available history budget %u",
                         static_cast<unsigned>(in.spiffsHistoryBytes), static_cast<unsigned>(historyBudget));
                plan.historyDeferred = true;
            } else {
                historyStage = stageDir(devFs, "/h", "h", historyBudget, staged);
            }
        }
    }

    // If staging /p failed on heap pressure, do NOT proceed to the format —
    // SPIFFS still holds the only copy. Bail; retry next boot (review #7).
    if (stageFailedOnHeap) {
        if (spiffsMounted) {
            SPIFFS.end();
        }
        return false;
    }

    // Review #5: never report a partial stage as a full preservation. A skip or
    // a short read on /p or /h means we did NOT preserve everything we found.
    bool profilesComplete = profileStage.complete();
    bool historyComplete = historyStage.complete();
    bool stagedHistory = historyComplete && !plan.historyDeferred;

    // If profiles exist but we could not stage all of them in full, refuse to
    // format — the same hard guarantee as the over-budget /p abort above.
    if (plan.action == FsMigrationAction::MigrateFromSpiffs && in.spiffsHasProfiles && !profilesComplete) {
        ESP_LOGE(LOG,
                 "ABORTING migration: /p stage incomplete (skipped=%u shortRead=%d) — refusing to reformat over "
                 "the only copy of profiles. Leaving SPIFFS intact; will retry next boot.",
                 static_cast<unsigned>(profileStage.skipped), static_cast<int>(profileStage.shortRead));
        if (spiffsMounted) {
            SPIFFS.end();
        }
        return false;
    }

    // ---- POINT OF NO RETURN: from here the SPIFFS image is destroyed. --------
    // Set the durable in-progress sentinel BEFORE releasing/formatting so an
    // interrupted run is detectable on the next boot (review #1).
    bool destructiveRun =
        (plan.action == FsMigrationAction::MigrateFromSpiffs) || (plan.action == FsMigrationAction::FreshFormat);
    if (destructiveRun) {
        setMigrationInProgress(true);
    }

    // Release SPIFFS so LittleFS can take ownership of the same partition.
    if (spiffsMounted) {
        SPIFFS.end();
    }

    // STEP 3: format the partition as LittleFS (formatOnFail=true forces a
    // format since the partition is still SPIFFS-formatted or blank).
    if (!LittleFS.begin(true, LITTLEFS_BASE, maxOpenFiles, DATA_PARTITION_LABEL)) {
        ESP_LOGE(LOG, "LittleFS format/mount FAILED — partition unusable");
        // Leave the sentinel set: the next boot will see it and know the prior
        // run was interrupted.
        return false;
    }

    // STEP 4: restore staged data and VERIFY it (review #1/#2/#3/#4). The
    // once-only marker is written ONLY when the restore is verified complete.
    bool restoreVerified = true;
    if (!staged.empty()) {
        DeviceMigrationFs devFs(SPIFFS, LittleFS);
        uint32_t restored = restoreStaged(devFs, staged);
        uint32_t verified = verifyRestored(devFs, staged);
        restoreVerified = (restored == staged.size()) && (verified == staged.size());
        if (!restoreVerified) {
            ESP_LOGE(LOG,
                     "restore verification FAILED: staged=%u restored=%u verified=%u — NOT stamping marker; "
                     "device will re-attempt the clean-LittleFS path next boot",
                     static_cast<unsigned>(staged.size()), static_cast<unsigned>(restored), static_cast<unsigned>(verified));
        }
    }

    // STEP 5: stamp the once-only marker ONLY on a verified-good run (review
    // #2). On a failed/partial restore we deliberately leave the marker absent:
    // the now-valid LittleFS will mount clean next boot and the UseLittleFsAsIs
    // path re-stamps the marker WITHOUT reformatting (self-heal preserved). The
    // sentinel we set above also flags the interruption on that next boot.
    if (plan.writeMarker && restoreVerified) {
        writeMarker();
        if (destructiveRun) {
            setMigrationInProgress(false); // verified done — clear the sentinel
        }
    }

    if (plan.action == FsMigrationAction::MigrateFromSpiffs) {
        if (plan.historyDeferred) {
            ESP_LOGW(LOG,
                     "shot history (/h, %u bytes) too large to stage in RAM — NOT preserved; profiles (/p) "
                     "were preserved",
                     static_cast<unsigned>(in.spiffsHistoryBytes));
        }
        ESP_LOGI(LOG, "SPIFFS->LittleFS migration complete (history_preserved=%d verified=%d)", static_cast<int>(stagedHistory),
                 static_cast<int>(restoreVerified));
    } else {
        ESP_LOGI(LOG, "fresh LittleFS ready (no recoverable SPIFFS data)");
    }
    return true;
}
