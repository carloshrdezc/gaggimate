# Upgrading across the SPIFFS → LittleFS boundary (profile-safe procedure)

> **⚠️ Read this before flashing the firmware release that switches the storage
> format from SPIFFS to LittleFS.** That one upgrade reformats the data
> partition. Profiles are **not** migrated automatically on the device — export
> them first, then re-import after flashing.

## Why this is needed (PRO-218 / Ref PRO-210)

Older GaggiMate firmware stores profiles (`/p`) and shot history (`/h`) on a
**SPIFFS**-formatted partition. The embed-WebUI release mounts the same
partition as **LittleFS**. SPIFFS and LittleFS are incompatible on-flash
formats, so the LittleFS mount fails and — because the firmware mounts with
`formatOnFail=true` — the partition is **clean-formatted to LittleFS on first
boot**. The device then seeds a single **Default** profile.

This is intentional and safe **only if you exported your profiles first**.

### Why not migrate in place on the device?

An in-place "mount old SPIFFS read-only → copy to RAM → format LittleFS →
restore" shim was implemented (PR #200), passed 72/72 host tests and two
reviews, and then **wiped all 7 profiles on a real-hardware upgrade test**. With
a single data partition the staged copy is volatile, the on-device SPIFFS rescue
read returned empty (unlike the host mock), and the "verified restore" gate
passed vacuously, stamping the once-only marker over wiped data. The approach is
rejected. The safe design moves the data **off-device** before any format, so a
silent wipe is structurally impossible. (See
`docs/spike-embed-webui-littlefs-migration.md` §4.2/§4.3.)

## User procedure (the safe upgrade)

1. **Export your profiles.** Open the web UI → **Profiles** → click **Export
   Profiles** (the export icon at the top of the page). This downloads a single
   `profiles.json` containing every profile. Keep this file somewhere safe.
   - The **System & Updates** (OTA) page also shows a prominent
     "Back up your profiles before updating" banner linking straight to the
     export.
   - Shot history (`/h`) is **not** included in the profile export and is **not**
     preserved across this boundary.
2. **Flash the new firmware** (the LittleFS / embed-WebUI release) via the OTA
   page as usual.
3. On first boot the device clean-formats the partition to LittleFS and shows a
   single **Default** profile. This is expected.
4. **Re-import your profiles.** Web UI → **Profiles** → **Import Profiles** →
   select the `profiles.json` you exported. All profiles are restored. Imported
   profiles whose id collides with an existing one are saved under a fresh id
   (no overwrite).
5. **Reboot** and confirm your profiles persist.

After this one boundary, all future OTAs leave `/p` (and `/h`) untouched — that
is the headline benefit of the embed-WebUI change.

## On-device verification test plan (release gate)

Host unit tests and the LVGL simulator **cannot** exercise the format boundary
(the sim fakes the filesystem with host directories). The release gate is a
real-hardware test:

> **ALWAYS take a full `esptool read_flash` backup of the device (and a
> data-partition backup) before flashing.** This is the rollback path if
> anything goes wrong.

1. Start on a real device running the **old SPIFFS** firmware with several user
   profiles (ideally including favorited / selected ones).
2. Export profiles to `profiles.json` via the web UI.
3. Flash the **LittleFS** firmware.
4. Confirm first boot **clean-formats** and shows only the **Default** profile
   (no crash, no boot loop).
5. Re-import `profiles.json`.
6. Confirm **all** profiles are restored, names/types/phases intact, and that
   they **persist across a power-cycle**.
7. Confirm a subsequent OTA does **not** wipe `/p`.

Only after this passes on real hardware can PRO-218's data-preservation
acceptance criterion be marked verified.
