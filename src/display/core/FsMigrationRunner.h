#ifndef FS_MIGRATION_RUNNER_H
#define FS_MIGRATION_RUNNER_H

// PRO-218 — device-side execution of the SPIFFS->LittleFS one-time migration.
//
// This is the I/O half of the shim. The pure decision logic lives in
// FsMigration.h (host-testable); this file probes the real filesystems,
// asks decideFsMigration() what to do, and carries the plan out against the
// real LittleFS / SPIFFS / SD_MMC. It is compiled into the device firmware
// only (NOT into the native test build).
//
// Call ensureDataPartitionMounted() EXACTLY ONCE, early in Controller::setup(),
// in place of the old `LittleFS.begin(true, ...)` call. On return, LittleFS is
// mounted and ready (or, on catastrophic failure, the last-resort format has
// run). It guarantees:
//   * pre-PRO-212 SPIFFS devices get /p (+ /h when an SD card is present)
//     rescued before any format;
//   * the migration runs at most once (marker file in LittleFS);
//   * fresh installs and already-migrated devices are never reformatted;
//   * the device is never bricked (fail-safe clean format as last resort).

// Mount the data partition, performing the one-time SPIFFS->LittleFS migration
// if this is the first boot of new firmware on an old SPIFFS device.
// `maxOpenFiles` mirrors the value the caller used for LittleFS asset serving.
// Returns true if LittleFS is usable on return.
bool ensureDataPartitionMounted(uint8_t maxOpenFiles);

#endif // FS_MIGRATION_RUNNER_H
