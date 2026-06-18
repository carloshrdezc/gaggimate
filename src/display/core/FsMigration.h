#ifndef FS_MIGRATION_H
#define FS_MIGRATION_H

// PRO-218 — SPIFFS -> LittleFS one-time data-migration decision logic.
//
// CONTEXT (see docs/spike-embed-webui-littlefs-migration.md S4.2):
// Devices flashed before PRO-212 have a *SPIFFS*-formatted data partition.
// The new firmware mounts that same partition as *LittleFS*. The on-flash
// formats are incompatible, so the LittleFS mount fails on the first boot
// after the upgrade. If LittleFS is allowed to mount with formatOnFail=true,
// it reformats the partition and WIPES /p (profiles) + /h (shot history) —
// the very upgrade that promises "OTA stops wiping your data" would wipe
// everyone once. This module decides how to rescue that data.
//
// SEPARATION OF CONCERNS:
// This header is PURE LOGIC ONLY. It performs no filesystem I/O and has no
// Arduino / ESP-IDF dependency, so it links and runs in the host
// `pio test -e native` environment. The device-side orchestration in
// Controller.cpp probes the real SPIFFS / LittleFS / SD_MMC state, packs the
// observations into `FsMigrationInputs`, asks `decideFsMigration()` what to
// do, and then carries out the resulting plan against the real filesystems.
// Keeping the branch logic here means the once-only / idempotency /
// fresh-install / fail-safe branches are unit-tested without hardware
// (the data-preservation acceptance criterion itself still requires an
// on-device upgrade test — that cannot be reproduced in CI).

#include <cstdint>

// Marker path written into LittleFS once the migration boundary is crossed.
// Shared by the device orchestration and any tooling that needs to recognize a
// migrated device. (Review #11: a top-placed inline constexpr instead of a
// trailing #define — scoped, typed, and avoids cppcoreguidelines-macro-usage.)
inline constexpr const char *FS_MIGRATION_MARKER_PATH = "/.fs_migrated";

// Observations the device gathers BEFORE deciding. Each field is filled by a
// real probe in Controller::setup() (or by a test fixture on the host).
struct FsMigrationInputs {
    // A sentinel file (FS_MIGRATION_MARKER_PATH) already exists in LittleFS,
    // meaning a previous boot already completed (or deliberately skipped) the
    // migration. When true the migration must NEVER run again (once-only).
    bool markerPresent = false;

    // LittleFS mounted cleanly WITHOUT being allowed to format on failure
    // (i.e. begin(false, ...) succeeded). True => the partition already holds
    // a valid LittleFS image: either a fresh install or a device that has
    // already been migrated. Either way there is nothing to rescue.
    bool littleFsMountedClean = false;

    // After a clean LittleFS mount failed, the same partition mounted as
    // SPIFFS read-only. True => this is a pre-PRO-212 device whose user data
    // is sitting in a SPIFFS image that LittleFS could not read.
    bool spiffsMounted = false;

    // Whether the SPIFFS image actually contains the data directories. Used
    // only to log/branch what gets staged; absence of both still proceeds to
    // a clean format so the device is never bricked.
    bool spiffsHasProfiles = false; // /p exists in SPIFFS
    bool spiffsHasHistory = false;  // /h exists in SPIFFS

    // A writable SD card is mounted. On SD-equipped devices the ProfileManager
    // (/p) and ShotHistoryPlugin (/h) route NEW writes to the SD card
    // (Controller sets `fs = &SD_MMC` when sdcard is true; ShotHistoryPlugin
    // does the same) — a *separate physical device* the internal reformat never
    // touches. HOWEVER (review #6): per-boot storage routing is NOT persisted,
    // so a device that always ran no-SD keeps its only /p,/h on the INTERNAL
    // partition; inserting an SD card across the upgrade boundary must NOT cause
    // those internal copies to be reformatted away. Therefore SD presence no
    // longer suppresses staging: whenever the internal SPIFFS image actually
    // holds /p or /h, that data is rescued regardless of sdCardAvailable. The
    // field is retained for logging / future routing decisions.
    bool sdCardAvailable = false;

    // Estimated total bytes of /h and /p in the SPIFFS image. /h is rescued to
    // RAM only if it fits ramHistoryBudgetBytes; otherwise /h is deferred (the
    // documented PRO-218 fallback) and only /p is preserved. /p must ALWAYS fit
    // ramProfileBudgetBytes — if it does not, the migration must NOT mark itself
    // done (review #3/#5: "profiles always preserved" cannot be silently
    // violated). The runner mirrors these budgets when it stages.
    uint32_t spiffsHistoryBytes = 0;
    uint32_t spiffsProfileBytes = 0;
    uint32_t ramHistoryBudgetBytes = 0;
    uint32_t ramProfileBudgetBytes = 0;
};

// What the device should do after the decision. Exactly one action.
enum class FsMigrationAction : uint8_t {
    // LittleFS is already valid (fresh install or already-migrated). Do
    // nothing destructive; just ensure the marker is written so future boots
    // short-circuit immediately. NO format, NO data movement.
    UseLittleFsAsIs,

    // The once-only marker is present. Migration already happened. Mount
    // LittleFS normally and never touch user data. (Defensive: the marker
    // lives in LittleFS, so a clean mount normally precedes this; this branch
    // exists so the decision is explicit and testable.)
    AlreadyMigrated,

    // Pre-PRO-212 SPIFFS device detected. Stage /p (+ /h if it fits the chosen
    // staging area), format the partition as LittleFS, restore the staged
    // data, then write the marker. This is the data-rescue path.
    MigrateFromSpiffs,

    // Neither LittleFS nor SPIFFS mounted (truly fresh / corrupt / blank
    // partition). Fail safe: format a clean LittleFS, seed default profiles,
    // write the marker. Never brick the device.
    FreshFormat,
};

// Where to stage rescued data during a MigrateFromSpiffs run.
enum class FsStagingTarget : uint8_t {
    None,                  // not migrating, or SD device (user data already safe on SD)
    RamProfilesOnly,       // no SD, /h too big for RAM: stage /p only; /h deferred
    RamProfilesAndHistory, // no SD, /h fits RAM budget: stage both /p and /h
};

struct FsMigrationPlan {
    FsMigrationAction action = FsMigrationAction::FreshFormat;
    FsStagingTarget staging = FsStagingTarget::None;
    // True only on the MigrateFromSpiffs+RamProfiles branch: /h could not be
    // safely staged (too big for the RAM budget) and is intentionally NOT
    // preserved. The caller logs a prominent warning; profiles (/p) are always
    // preserved.
    bool historyDeferred = false;
    // True whenever the run must end by writing the once-only marker so the
    // migration never re-runs. (Every terminal action writes it; AlreadyMigrated
    // does not need to rewrite an existing marker.)
    bool writeMarker = true;
    // Review #3/#5/#6 — abort signal. True when the SPIFFS image holds profiles
    // (/p) that cannot be staged safely (too large for the profile RAM budget).
    // The runner MUST NOT reformat the partition in this case: doing so would
    // erase the only copy of the user's profiles. Instead it leaves SPIFFS
    // intact and retries on the next boot. "Profiles always preserved" is a hard
    // guarantee, so an over-budget /p is a refuse-to-format condition, never a
    // silent drop.
    bool abortFormatToPreserveProfiles = false;
};

// Pure decision function. No I/O. Given the probed inputs, return the plan.
//
// Branch order matters:
//  1. markerPresent            -> AlreadyMigrated  (once-only guard, highest priority)
//  2. littleFsMountedClean     -> UseLittleFsAsIs  (fresh install / already-migrated, no marker yet)
//  3. spiffsMounted            -> MigrateFromSpiffs (the rescue path)
//  4. otherwise                -> FreshFormat       (fail safe, never brick)
inline FsMigrationPlan decideFsMigration(const FsMigrationInputs &in) {
    FsMigrationPlan plan;

    // (1) Once-only guard. If the marker exists the migration has already run
    // (or been deliberately skipped). Never run it again, never move data.
    if (in.markerPresent) {
        plan.action = FsMigrationAction::AlreadyMigrated;
        plan.staging = FsStagingTarget::None;
        plan.historyDeferred = false;
        plan.writeMarker = false; // already present
        return plan;
    }

    // (2) Fresh install or already-migrated-but-marker-missing: LittleFS is
    // valid. Do NOT format, do NOT migrate. Just write the marker so future
    // boots take branch (1) immediately. This is what protects a brand-new
    // device (blank partition that begin(false) happened to format-and-mount
    // is handled in (4); a genuinely valid LittleFS lands here) from a
    // spurious migration.
    if (in.littleFsMountedClean) {
        plan.action = FsMigrationAction::UseLittleFsAsIs;
        plan.staging = FsStagingTarget::None;
        plan.historyDeferred = false;
        plan.writeMarker = true;
        return plan;
    }

    // (3) Pre-PRO-212 SPIFFS device. Rescue the data.
    //
    // Review #6: SD presence does NOT suppress staging. Per-boot storage routing
    // is never persisted, so a device that always ran no-SD has its only /p,/h
    // on the internal partition even if an SD card happens to be inserted across
    // the upgrade. Reformatting over unstaged internal profiles would erase the
    // only copy. So: whenever the internal SPIFFS image holds /p or /h, rescue
    // it; the cost on a genuine SD device (whose live data is on SD) is merely
    // restoring a stale internal copy that runtime routing never reads — strictly
    // safer than the previous unconditional wipe.
    if (in.spiffsMounted) {
        plan.action = FsMigrationAction::MigrateFromSpiffs;
        plan.writeMarker = true;

        // Review #3/#5: "profiles always preserved" is a hard guarantee. If /p
        // exists but is too large for the profile RAM budget, we must NOT
        // reformat — that would erase the only copy. Signal an abort so the
        // runner leaves SPIFFS intact and retries next boot.
        if (in.spiffsHasProfiles && in.spiffsProfileBytes > in.ramProfileBudgetBytes) {
            plan.staging = FsStagingTarget::RamProfilesOnly;
            plan.abortFormatToPreserveProfiles = true;
            plan.writeMarker = false; // never seal a run that cannot preserve /p
            return plan;
        }

        if (in.spiffsHasHistory && in.spiffsHistoryBytes > in.ramHistoryBudgetBytes) {
            // /h present but too large to hold safely in RAM: preserve /p
            // (always small), defer /h (documented PRO-218 fallback).
            plan.staging = FsStagingTarget::RamProfilesOnly;
            plan.historyDeferred = true;
        } else {
            // /h absent or small enough to fit the RAM budget: preserve both.
            plan.staging = FsStagingTarget::RamProfilesAndHistory;
            plan.historyDeferred = false;
        }
        return plan;
    }

    // (4) Fail safe: nothing mounted. Truly fresh / blank / corrupt partition.
    // Format clean LittleFS, seed defaults, mark done. Never brick.
    plan.action = FsMigrationAction::FreshFormat;
    plan.staging = FsStagingTarget::None;
    plan.historyDeferred = false;
    plan.writeMarker = true;
    return plan;
}

#endif // FS_MIGRATION_H
