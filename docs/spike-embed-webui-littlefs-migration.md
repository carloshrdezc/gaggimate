# Spike (PRO-210): Migration plan + risk assessment — port upstream embed-WebUI (PR #764) incl. SPIFFS→LittleFS

**Status:** Spike / planning only. No production firmware or web code is changed by this document.
**Issue:** PRO-210 (Gaggimate, labels `spike` / `firmware` / `web-ui`).
**Branch / base:** `pro-210-embed-webui-spike` off `dev-master`; PR `Ref PRO-210` (not `Fixes`).
**Repo state at spike time:** fork tip `dev-master @ 17abab78` ("Merge PR #190 — gate WebUI flag"), **140 commits behind `upstream/master`** (`591abe74`). Fork still on **SPIFFS** and **NimBLEComm**; the entire nanopb GM-82 rewrite is absent.

This is the authoritative reference for the actual implementation work. It answers the five required questions, citing this fork's files and the relevant upstream commits.

---

## 0. TL;DR / recommendation

**Recommended strategy: (B) Surgical backport — phased.**

1. Backport just the **SPIFFS→LittleFS** filesystem swap (the mechanical part of `85c0c38a`), *without* the nanopb-coupled WiFi-coex / BT-latency hunks (which can't even apply — see §2).
2. Port **PR #764's embed mechanism** (`embed_webui.py`, `embed_webui_pre.py`, `build_webui.sh`, `serveWebAsset`, manifest header, OTA simplification) on top.
3. Adapt every fork-specific item (WEBUI/BLE-scale gating, extra routes, **the Windows `display-sim` path**, the CAR-385 CI matrix, the `display-headless-4m` board) by hand.

**Why not (A) full catch-up:** rebasing/merging ~140 upstream commits drags in the entire **nanopb GM-82 comms rewrite (PR #726)**, which replaces the fork's `NimBLEComm` library wholesale. That is a multi-week, high-risk migration completely unrelated to embedding the web UI, and it would bury the (genuinely valuable, low-risk) embed change under a comms re-architecture. Full catch-up is the right *eventual* destination for the fork, but it should be its own large initiative, not a prerequisite for this win.

The embed-WebUI benefit (OTA stops wiping user data) is achievable in days, not weeks, via the surgical path — and the highest-risk item (Windows sim) turns out to be **largely already solved** by the existing sim shim (§3.4).

| Strategy | Effort | Risk | Verdict |
|---|---|---|---|
| (A) Full catch-up (rebase ~140 commits + nanopb) | **3–6 weeks**, comms re-test on real hardware | **High** — pulls in GM-82 BLE rewrite, driver churn, every fork patch re-applied at once | Reject as the path to embed-WebUI; track separately as a fork-modernization initiative |
| **(B) Surgical backport (recommended)** | **3–5 days** across 4–5 reviewable PRs | **Low–Medium** — FS swap is mechanical; embed mechanism is self-contained; sim mostly pre-wired | **Adopt** |
| (C) Hybrid/phased | n/a | n/a | (B) *is* the phased plan; a separate "catch up to upstream" initiative can follow later |

---

## 1. Migration-strategy decision (detail)

### (A) Full fork catch-up — rebase/merge ~140 upstream commits to `dev-master`
- **What it entails:** integrate `upstream/master` (`591abe74`) into `dev-master`, re-applying every fork patch (WEBUI/BLE-scale gating, CAR-385 CI matrix, Windows sim, the LVGL/UI fork work in `car-*` branches). Embed-WebUI (PR #764) then lands "naturally" because it's already in upstream history.
- **The killer dependency:** between us and PR #764 sits **GM-82 (PR #726, commit `ccfe792b`)**, the nanopb framed-comms rewrite that *replaced* `NimBLEComm`. Confirmed: this fork ships `lib/NimBLEComm` and has **no `lib/NanoPbComm`** at all (`ls lib/` → `ble_ota_dfu GaggiMateController NayrodPID NimBLEComm OTA README`). Catching up forces adopting the entire new BLE/comms stack — `Endpoint`, `GaggiMateClient`, the `.proto`/nanopb build integration, ControllerOTA changes, and the display-driver SD_MMC changes — plus re-validating BLE control timing on real hardware.
- **Effort:** 3–6 weeks. **Risk:** High. Conflicts across `Controller.cpp`, every driver, the plugins, and the comms layer; the fork's `car-*` UI branches would all need rebasing too.
- **Recommendation:** **Not** for this objective. Worth doing as a *separate* "modernize fork to upstream" initiative, but coupling it to embed-WebUI defeats the purpose (a low-risk OTA-safety win held hostage to a comms rewrite).

### (B) Surgical backport — **RECOMMENDED**
- **What it entails:** decouple the FS swap from the nanopb/coex hunks in `85c0c38a`, backport only the FS swap, then port PR #764's embed mechanism on top, adapting fork specifics.
- **Why it works:** the SPIFFS→LittleFS change is *mechanical and separable* (§2), the embed mechanism is *self-contained* (new scripts + one new method + a generated header + OTA simplification — see PR #764's 13-file diff, no comms coupling), and the fork's web-server library *already has* the `beginResponse(int, const String&, const uint8_t*, size_t)` overload the embed path needs (`.pio/libdeps/display/ESPAsyncWebServer/src/ESPAsyncWebServer.h:506`).
- **Effort:** 3–5 days, 4–5 small PRs. **Risk:** Low–Medium (medium only because of partition headroom — §4 — and the sim path, which is mostly pre-wired — §3.4).
- **Recommendation:** **Adopt.**

### (C) Hybrid/phased
- Strategy (B) is already the phased plan (§5 breaks it into sequential reviewable PRs). A full catch-up (A) can be scheduled afterward as its own initiative once embed-WebUI has shipped and de-risked the OTA story. No separate "C" is needed.

---

## 2. Dependency analysis — is SPIFFS→LittleFS separable from nanopb GM-82?

**Yes, completely. The LittleFS migration does NOT require nanopb. They were merely co-committed in `85c0c38a`.**

`85c0c38a` ("Replace SPIFFS with LittleFS, add WiFi coex, BT latency") is a **grab-bag commit** mixing three orthogonal concerns. Partitioned from `git show 85c0c38a`:

### 2a. The actual FS swap — mechanical, no comms dependency
Every filesystem hunk is a literal token substitution:

- `platformio.ini`: add `LittleFS` to `lib_deps_default` (after `SPIFFS`); add `board_build.filesystem = littlefs` to `[env:display]` (GM-90 comment).
- `src/display/core/Controller.cpp`: `#include <SPIFFS.h>` → `#include <LittleFS.h>`; `SPIFFS.begin(true)` → `LittleFS.begin(true, "/littlefs", 16)`; `FS *fs = &SPIFFS;` → `&LittleFS;`.
- `src/display/plugins/WebUIPlugin.cpp`: `#include` swap; `FS *fs = &SPIFFS;` → `&LittleFS;`; the three `serveStatic`/`onNotFound` calls switch `SPIFFS`→`LittleFS`.
- `src/display/plugins/ShotHistoryPlugin.{cpp,h}`: `#include` swap; `FS *fs = &SPIFFS;` → `&LittleFS;`; `SPIFFS.totalBytes()/usedBytes()` → `LittleFS.*`.
- `lib/OTA/src/ControllerOTA.cpp`: `#include` swap; `SPIFFS.exists/remove/open` → `LittleFS.*` for `/board-firmware.bin`.

None of these touch comms. `LittleFS` is a standard ESP-IDF/Arduino library; no nanopb symbol is referenced.

### 2b. Hunks that are NOT the FS swap (do not backport in the FS PR)
- **NanoPbComm RTT/latency telemetry** — `lib/NanoPbComm/src/Endpoint.{cpp,h}` (Karn's-rule RTT sampling, EWMA `_smoothedRttMs`) and `GaggiMateClient.h` (`getLatencyMs()`/`hasLatency()`). **These cannot apply to our fork — `lib/NanoPbComm` doesn't exist here** (we're on `NimBLEComm`). They are a BT-latency *feature*, unrelated to the FS change.
- **WiFi/BLE coexistence** — `Controller.cpp` `esp_coex_preference_set(...)` in `setupBluetooth()`/`applyConnectionPriority()` + `#include "esp_coexist.h"`. A radio-arbitration *feature* (GM-90), independent of the FS swap.
- **Driver `maxOpenFiles` 5→10** — `Amoled_DisplayPanel.cpp`, `LilyGo_RGBPanel.cpp`, `WavesharePanel.cpp`: `SD_MMC.begin("/sdcard", true, false)` → `... BOARD_MAX_SDMMC_FREQ, 10)`. SD-card hardening for concurrent `.slog` serving; *related to GM-90 load*, not to LittleFS. **Optional**: worth backporting for robustness, but in a *separate* commit, and only if `BOARD_MAX_SDMMC_FREQ` is defined for each board in our fork (verify before applying).
- **The `lat` status field + web cosmetics** — `WebUIPlugin.cpp` `statusDoc["lat"]`, plus Prettier reformatting in `Navigation.jsx`, `Home/index.jsx`, `OTA/index.jsx`, `ApiService.js`. Tied to the latency feature; **skip** (the web `lat` readout has no backend without the NanoPbComm latency API).

**Conclusion:** backport **only §2a**. The FS swap is ~6 files of pure `SPIFFS`→`LittleFS` substitution with zero comms coupling. The "depends on nanopb" impression is an artifact of upstream bundling three features in one commit message.

> **One real cross-cutting concern (not a code dependency):** SPIFFS and LittleFS are *different on-flash formats*. A device flashed with a SPIFFS image cannot be mounted as LittleFS — `LittleFS.begin(true, ...)` will **format** the partition on mount failure, wiping `/p` and `/h`. This is a *data-migration* risk, handled in §4, not a build dependency.

---

## 3. Fork-patch preservation plan

For each fork-specific item, how it is preserved under strategy (B).

### 3.1 `GAGGIMATE_ENABLE_WEBUI` gating (PR #190, commits `d4db7d4c`, `cc8fb70d`)
- **Where it lives now:** `src/display/config/features.h:42-43` (`#ifndef GAGGIMATE_ENABLE_WEBUI` / `#define ... 1`); guards in `src/display/core/Controller.cpp:29,98` and `src/display/plugins/mDNSPlugin.cpp:25,28` (mDNS HTTP advertisement only when WebUI is on).
- **Interaction with embed:** the embed path adds a *new include* `#include <display/webassets/web_ui_manifest.h>` and a `serveWebAsset()` method to `WebUIPlugin.cpp`. Since the whole `WebUIPlugin` already only runs when `GAGGIMATE_ENABLE_WEBUI`, the embed code is implicitly gated. **Action:** ensure `WebUIPlugin.cpp`'s new `#include <display/webassets/web_ui_manifest.h>` and `serveWebAsset()` are inside the existing WEBUI-gated compilation; the `embed_webui_pre.py` stub guarantees the header exists even for `GAGGIMATE_ENABLE_WEBUI=0` builds so non-WebUI envs still compile. Re-verify the `mDNSPlugin` advertisement path is untouched by the embed change (it is — embed only changes *how* assets are served, not *whether* port 80 is advertised).

### 3.2 `GAGGIMATE_ENABLE_BLE_SCALE` gating (PR #188, commit `669c45c8`)
- **Where:** `WebUIPlugin.cpp:339,447,1412,1468` (`#if GAGGIMATE_ENABLE_BLE_SCALE` ... `#endif`).
- **Interaction with embed:** none — the embed change touches `setupServer()`'s static-asset wiring and adds `serveWebAsset()`, not the BLE-scale handlers. **Action:** preserve the four `#if/#endif` blocks verbatim during the WebUIPlugin merge; they are far from the edited `serveStatic`/`onNotFound` region (lines 453–485).

### 3.3 Extra HTTP routes not in upstream (must survive the `serveStatic`→`serveWebAsset` swap)
Our `setupServer()` (lines 453–485) registers routes upstream does not:
- `server.serveStatic("/api/history/", *fs, "/h/")` (line 457) + `/api/history/index.bin` handler (458–466) — **shot history**, served from `/h` on the filesystem.
- explicit `/favicon.ico`, `/apple-touch-icon.png`, `/apple-touch-icon-precomposed.png` → `SPIFFS, "/w/gm.png"` (lines 474–476).
- `server.serveStatic("/fonts/", SPIFFS, "/w/fonts/")` (line 480).
- our `onNotFound` is deliberately registered **before** `serveStatic` (comment line 481) and falls back to `/w/index.html`.

**Preservation actions:**
- **`/api/history/` stays on the filesystem** — it serves `/h`, which *remains in LittleFS* (only `/w` leaves the filesystem). Just swap `SPIFFS`→`LittleFS` for `*fs` here; do **not** route it through `serveWebAsset`.
- **`/favicon.ico` + `/apple-touch-icon*`**: two options — (a) keep them as explicit `server.on(...)` handlers but have them call `serveWebAsset` with the embedded `/gm.png` path (preferred — keeps icons out of the filesystem entirely), or (b) drop them and let the `serveWebAsset` catch-all + manifest serve them if the build emits them. Recommend (a) to preserve exact current behavior; ensure `gm.png` is in `web/dist` so `embed_webui.py` packs it.
- **`/fonts/`**: fonts move into the embedded blob automatically (`embed_webui.py` walks all of `web/dist` and assigns content types incl. `.woff2`). Remove the explicit `/fonts/` `serveStatic`; the `serveWebAsset` catch-all handles `/fonts/*`. **Verify** fonts land in `web/dist/fonts/` (or wherever Vite emits them) so they're packed.
- **`onNotFound` ordering**: upstream's final form is `server.onNotFound([this](req){ serveWebAsset(req); })` — a single catch-all that internally falls back to `index.html` for SPA routes (PR #764 `serveWebAsset` does this for non-`/assets/` misses). This *replaces* our before-serveStatic ordering trick; the SPA fallback semantics are preserved by `serveWebAsset`'s own logic. **Action:** delete the `/`/`/assets/` `serveStatic` calls and the standalone `onNotFound`→index handler; register the single `onNotFound`→`serveWebAsset`. Keep `/api/history/` and the icon `server.on` handlers registered (explicit routes win over `onNotFound`).

### 3.4 Windows desktop simulator (`display-sim`, PR #191, commit `4a2380b4`) — **highest-risk item, but largely pre-solved**
The task flags this as highest risk because PR #764's generated `web_ui_blob.S` only guards `#if !defined(__APPLE__) && !defined(__linux__)` — **Windows/MinGW is not in that guard**, so the `.section .rodata.embedded` directive would be emitted on Windows and the `.incbin` of an absolute blob path would have to assemble under MinGW. The sim also can't use ESP flash memory-mapping.

**Investigation result — the sim is already wired for an embedded blob:**
- The sim's web shim **already implements the exact embed overload**: `sim/web/ESPAsyncWebServer.h:85` declares `beginResponse(int code, const String &contentType, const uint8_t *content, size_t len)`, backed by a **zero-copy `_blob` body** (`ESPAsyncWebServer.h:50-51`, "optional zero-copy body (embedded assets)") and implemented at `sim/web/ESPAsyncWebServer.cpp:319-326` (`r->_blob = content; r->_blobLen = len;`) with the sender preferring `_blob` over `_body` (`ESPAsyncWebServer.cpp:306-307`). So `serveWebAsset()`'s `beginResponse(200, ct, gWebUiBlobStart + offset, len)` call **compiles and runs as-is in the sim**.
- The sim **already has a LittleFS shim** (`sim/platform/LittleFS.h`, `LittleFSFS::begin(...)` backed by a host directory) *and* a SPIFFS shim — so the FS swap (§2a) is a no-op for the sim build.
- The sim **already excludes `display/webassets/`** from its `build_src_filter` (`platformio.ini:[env:display-sim]` → `-<display/webassets/>`), and today serves the UI from `sim_data/spiffs/w/` via the host `serveStatic`.

**What's actually missing for the sim under embed (the real work):**
1. `gWebUiBlobStart` / `gWebUiBlobEnd` symbols: with `display/webassets/` excluded and no `.S` assembled on the host, these are **undefined** → link error. **Fix:** provide a *host backing* for the blob. Two options:
   - **(a) Sim host blob source (recommended):** add a tiny `sim/web/web_blob_host.cpp` that either `#include`s a generated `web_ui.bin` via a host-friendly mechanism (e.g. `xxd`/CMake-style byte array, or `std::ifstream` loading `sim_data/spiffs/w` at startup and exposing a synthesized manifest), defining `gWebUiBlobStart`/`gWebUiBlobEnd`. This lets the sim exercise the *real* `serveWebAsset` code path.
   - **(b) Keep sim on filesystem serving:** under `#ifdef GAGGIMATE_SIM`, retain the current `serveStatic("/", LittleFS, "/w")` path and skip `serveWebAsset`. Lower-fidelity (sim diverges from device serving) but minimal effort.
   - Recommend **(a)** for parity (the sim's whole point per PR #191 is "WebUI parity"), with (b) as a fallback if (a) proves fiddly.
2. The generated `.S` guard: **must be extended** to also no-op on Windows/MinGW. Patch `embed_webui.py`'s assembly template from `#if !defined(__APPLE__) && !defined(__linux__)` to also exclude `_WIN32`/`__MINGW32__` (or, cleaner, gate the whole device `.S` out of the sim build — which the existing `-<display/webassets/>` filter already does, so the device `.S` is never compiled in the sim; the only requirement is that the **host** provides the symbols per item 1). **This is the single mandatory `embed_webui.py` change for sim compatibility.**
3. `embed_webui_pre.py` runs only for device envs (it's in `[env:display]` `extra_scripts`); the sim env has its own `extra_scripts` (`sim_sdl_flags.py`, `sim_run.py`) and won't invoke it. The sim's blob source must be produced by its own path (item 1a) or the sim stays on filesystem serving (1b).

**Net:** the sim risk is **medium, not high** — the hard part (a web-server shim that can serve a raw `(ptr,len)` blob) already exists. The implementation issue is "give the sim a host definition of `gWebUiBlobStart` (or `#ifdef GAGGIMATE_SIM` keep filesystem serving) and widen the `.S` guard to exclude Windows."

### 3.5 CAR-385 CI build matrix (PR #189)
- **Where:** `.github/workflows/build.yml` runs firmware targets as a **parallel matrix** (`controller`, `display`, `display-headless`) and builds the **SPIFFS filesystem image in its own job** (`build.yml:112` `./scripts/build_spiffs.sh` → `display-headless-filesystem.bin`). `build-nightly.yml` and `pr-flash.yml` also call `build_spiffs.sh`.
- **PR #764's CI change** rewrites these to call `build_webui.sh` instead of `build_spiffs.sh` and deletes `build_spiffs.sh`.
- **Conflict:** PR #764 edits a *different* CI shape than ours (upstream has a single Build-Web step; we have a matrix + a dedicated filesystem job). A blind apply will not merge.
- **Preservation actions:**
  - Add `scripts/build_webui.sh`, `scripts/embed_webui.py`, `scripts/embed_webui_pre.py` (new files, no conflict).
  - In **our** `build.yml`: the web UI now embeds into the firmware app image, so the **per-target firmware build must run `build_webui.sh` before `pio run -e <target>`** for the display targets (so `src/display/webassets/` exists with the real bundle, not the stub). Restructure: either (a) run `build_webui.sh` once and share `src/display/webassets/` across matrix legs via cache/artifact, or (b) run it within each display leg. The dedicated **filesystem-image job changes meaning**: it now only stages `/p` (seed profiles) for fresh USB installs — `build_webui.sh` still produces a `data/p` filesystem image, but `/w` is gone from it. Decide whether we still ship a `*-filesystem.bin` artifact at all (yes, for fresh installs / seed profiles; see §4).
  - Update `build-nightly.yml` and `pr-flash.yml` the same way (`build_spiffs.sh` → `build_webui.sh`).
  - `.gitignore`: add `src/display/webassets/` (PR #764 does this) — the embedded bundle is generated, not committed.
  - Keep the **gating CI** (`ci.yml`, `check.yml` — CAR-341 cppcheck/clang-tidy/ASan) intact; verify `embed_webui_pre.py`'s stub lets `pio run -e display` link in `ci.yml` *without* a prior web build (it's designed to — `ci.yml:62` runs `pio run -e display` and the pre-hook writes an empty stub if the manifest is missing).

### 3.6 `display-headless-4m` board (esp32-s3-supermini) — **PR #764 deletes it; we use it**
- **Where:** `platformio.ini:99-101` `[env:display-headless-4m] extends = env:display-headless / board = esp32-s3-supermini`. **PR #764 deletes this env** (its platformio.ini diff removes the 4m block).
- **Why upstream deleted it:** the 4 MB esp32-s3-supermini almost certainly **lacks flash headroom** for an app image that now also carries the embedded web UI (see §4). Upstream chose to drop the 4 MB target rather than fit the UI into it.
- **Decision required (fork policy):** do we keep supporting 4 MB? Options:
  - **(a) Follow upstream — drop `display-headless-4m`.** Simplest; matches upstream. Acceptable *only if* no fork user depends on the supermini board. **Action item: confirm with Carlos whether the 4 MB board is still a supported target.**
  - **(b) Keep it but exclude the embedded UI on 4 MB.** Headless builds already serve no UI in many configs; if the 4 MB target runs with `GAGGIMATE_ENABLE_WEBUI=0`, the embedded blob is just the stub (1 byte) and flash impact is nil. Keep the env, set `GAGGIMATE_ENABLE_WEBUI=0` for it.
  - **(c) Keep it with embedded UI** only if the partition math (§4) shows the gzipped bundle fits in 4 MB — likely tight/infeasible.
  - **Recommend (b)** if we still ship the supermini, else (a). Do **not** silently inherit upstream's deletion without deciding.

---

## 4. Risk assessment

### 4.1 Partition / flash-size impact — **the primary technical risk**
- **No custom partition CSV in the repo** — the fork uses each board's **default** PlatformIO partition table (confirmed: `grep board_build.partitions platformio.ini` → none; the only `partitions-4MB.csv` files found are inside `.pio/libdeps/.../ESPAsyncWebServer/`, i.e. a vendored example, not ours). So app-partition size is whatever the board default provides.
- Embedding the (gzipped) web UI moves it from the filesystem partition into the **app partition** (`.rodata.embedded` → DROM). The app image grows by the **total gzipped bundle size**. **Action: measure it** — run `scripts/build_spiffs.sh` (current) and `du -bc data/w` to get today's gzipped UI footprint; that's the app-image growth. (At spike time the bundle wasn't built in this workspace, so I can't quote a number — **measuring this is task 1 of the implementation, and it gates the 4 MB decision in §3.6.**)
- **8 MB targets (`LilyGo-T-RGB` display, `seeed_xiao_esp32s3` display-headless-8m):** almost certainly fine — these were upstream's kept targets. Verify the default 8 MB partition app slot has room for `app + embedded UI` with OTA double-bank.
- **4 MB target (`esp32-s3-supermini`):** the headroom risk that drove upstream's deletion. See §3.6.
- **OTA double-bank:** embedding the UI grows *each* of the two OTA app slots. Confirm the default partition layout's `app0`/`app1` slots each fit the grown image. This is the make-or-break check for the whole approach on a given board.

### 4.2 OTA compatibility for already-deployed devices (SPIFFS layout → new)
- Today, an OTA flashes firmware **and** a filesystem image; PR #764 removes the filesystem OTA phase (`GitHubOTA.cpp` drops `PHASE_DISPLAY_FS`/`update_filesystem`). After migration, OTA flashes only the app image (now containing the UI) and **never touches the data partition**.
- **The hazard:** existing devices have a **SPIFFS-formatted** data partition. New firmware mounts it as **LittleFS** (`LittleFS.begin(true, ...)`). SPIFFS and LittleFS are incompatible on-flash formats, so the mount **fails**, and because `formatOnFail=true`, LittleFS **reformats the partition — wiping `/p` (profiles) and `/h` (shot history)** on first boot after the upgrade.
- **This is the single most dangerous user-facing risk.** Mitigations (pick one, design in the OTA/migration PR):
  - **(a) One-time migration filesystem image:** the upgrade that introduces LittleFS *also* ships a LittleFS filesystem image (with seed `/p`) flashed via the legacy filesystem-OTA path **one last time**, so the partition is LittleFS-formatted with at least seed data. Loses *user* profiles/shots unless combined with (b).
  - **(b) Read-SPIFFS-then-reformat-LittleFS migration shim:** on first LittleFS-mount failure, attempt a SPIFFS mount of the same partition, copy `/p` + `/h` to RAM/SD, format LittleFS, write them back. Most user-friendly, most code. Feasible because both libs can be linked simultaneously (the fork already lists `SPIFFS` in `lib_deps`; add `LittleFS`).
  - **(c) Accept data loss with loud warning + export-first UX:** document that this specific upgrade resets profiles/history; prompt users to export first. Cheapest, worst UX.
  - **Recommend (b)** if effort allows (it's the "OTA stops wiping user data" promise made real even across the one migration boundary); **(a)** as the pragmatic fallback. Decide explicitly — this must not be discovered in the field.
- **Note:** *after* the LittleFS migration boundary, the embed change delivers its headline benefit — subsequent OTAs leave `/p` and `/h` untouched.

### 4.3 Data-migration story (profiles `/p` + shot history `/h`)
- `/p` and `/h` **stay on the filesystem** (now LittleFS) — only `/w` (web UI) leaves it. `ShotHistoryPlugin` (`/h`) and `ProfileManager` (`/p`) keep using the filesystem `FS*`; the §2a swap repoints them from SPIFFS to LittleFS transparently.
- Survival across the upgrade = the §4.2 mitigation. After that boundary, they persist across all future OTAs (the win).
- Seed profiles (`data/p`) are still staged into the fresh-install filesystem image by `build_webui.sh` (it keeps `mkdir -p data/p`), so USB fresh-installs still ship default profiles.

### 4.4 CI / build-time impact
- New mandatory pre-firmware step for display targets: `build_webui.sh` (npm ci + build + gzip + `embed_webui.py`). Adds the web build to the firmware-build critical path (previously the web build was a separate filesystem job). Mitigate by caching `src/display/webassets/` across matrix legs (build once, reuse).
- `embed_webui_pre.py` stub keeps `ci.yml`'s `pio run -e display` working without a web build — **good**, the gating CI doesn't need npm. Verify after porting.
- `pio test -e native` / `native-sanitize` (host tests): unaffected — they don't compile `display/webassets/`. The **sim** (`display-sim`) needs the §3.4 host-blob handling or it won't link; if the sim is in any CI leg, add the host blob step.
- Net build-time delta: +one npm build on display firmware legs (cacheable), -one separate filesystem job's web build (folded in). Roughly neutral once cached.

---

## 5. Follow-up implementation issue breakdown (sequential coder→reviewer)

Proposed sequence; one reviewable unit per issue. Each is small enough to coder→reviewer per Carlos's workflow. **Do not create these now** — listed for planning.

1. **PRO-21x — Measure & decide: web-UI footprint + partition headroom + 4 MB policy** *(spike-tail / chore)*
   Build the current bundle, measure gzipped size, compute app-image growth, check default partition app-slot headroom on `display` (8 MB), `display-headless-8m`, and `display-headless-4m` (4 MB). Output: the 4 MB decision (§3.6) and a go/no-go per board. *Gates everything below.*

2. **PRO-21x — SPIFFS→LittleFS filesystem swap (FS only, no embed)** *(firmware)*
   Apply only §2a (the mechanical `SPIFFS`→`LittleFS` substitution in `Controller.cpp`, `WebUIPlugin.cpp`, `ShotHistoryPlugin.{cpp,h}`, `ControllerOTA.cpp`, `platformio.ini`). Keep `/w` on the filesystem for now (still `serveStatic` from LittleFS). Verify device boots, mounts, serves UI, profiles/history work. **Exclude** the nanopb/coex/`lat`/driver-maxOpenFiles hunks. Preserves WEBUI/BLE-scale gating and all extra routes (just FS token swap). Sim: no-op (LittleFS shim exists).

3. **PRO-21x — (optional) SD_MMC maxOpenFiles 5→10 hardening** *(firmware)*
   Backport the driver `maxOpenFiles` bump (§2b) **iff** `BOARD_MAX_SDMMC_FREQ` is defined for each board. Independent robustness fix; can be skipped or deferred.

4. **PRO-21x — Embed web UI build pipeline** *(firmware + infrastructure)*
   Add `scripts/embed_webui.py` (with the **Windows/MinGW-aware `.S` guard**, §3.4 item 2), `embed_webui_pre.py`, `build_webui.sh`; wire `pre:scripts/embed_webui_pre.py` into `[env:display]`; `.gitignore` `src/display/webassets/`. No serving change yet — just generate the blob/manifest and prove `pio run -e display` links with both the stub and a real bundle.

5. **PRO-21x — Serve UI from embedded blob + simplify OTA** *(firmware)*
   Add `serveWebAsset()` + `#include <display/webassets/web_ui_manifest.h>`; replace `/`+`/assets/` `serveStatic` and the standalone `onNotFound` with the single `onNotFound`→`serveWebAsset` (§3.3); keep `/api/history/` on LittleFS, convert favicon/apple-touch-icon to embedded serving, drop the explicit `/fonts/` route. Apply PR #764's `GitHubOTA.{cpp,h}` simplification (remove `update_filesystem`/`PHASE_DISPLAY_FS`). Preserve all `#if GAGGIMATE_ENABLE_*` blocks.

6. **PRO-21x — Windows sim parity for embedded UI** *(firmware / sim)*
   Implement §3.4: define `gWebUiBlobStart`/`gWebUiBlobEnd` for the host (option a: `sim/web/web_blob_host.cpp` synthesizing blob+manifest from `sim_data/spiffs/w`, exercising the real `serveWebAsset`; or option b: `#ifdef GAGGIMATE_SIM` keep filesystem serving). Confirm `display-sim` builds & runs on Windows/MinGW and the web UI renders.

7. **PRO-21x — CI matrix migration to embed pipeline** *(infrastructure)*
   Update `build.yml` (matrix) + `build-nightly.yml` + `pr-flash.yml`: run `build_webui.sh` before display firmware builds (cache `src/display/webassets/`), redefine the filesystem-image job to stage only `/p` for fresh installs, drop/retain `display-headless-4m` per task 1's decision. Keep gating CI (`ci.yml`/`check.yml`) green.

8. **PRO-21x — OTA migration & data-preservation across the SPIFFS→LittleFS boundary** *(firmware — highest user-facing risk)*
   Implement the §4.2 mitigation (recommend (b): SPIFFS-read → LittleFS-reformat → restore `/p`+`/h`; fallback (a): one-time migration filesystem image). Field-test an upgrade from a real SPIFFS device. **Ship last, gate the release on it** — this is the difference between "OTA stops wiping data" and "the upgrade wipes everyone's data once."

**Ordering rationale:** 1 gates the rest (headroom). 2 isolates the risky-but-mechanical FS swap so it can be validated alone. 4→5 layer the embed mechanism. 6 handles the sim. 7 lands CI once the build works. 8 is the data-safety capstone, tested against real hardware before release.

---

## Appendix — evidence index (fork-specific citations)

- Fork tip / behind count: `dev-master @ 17abab78`, `git rev-list --count dev-master..upstream/master` = **140**.
- Fork on SPIFFS: `src/display/plugins/WebUIPlugin.cpp:3,453,474-485` (SPIFFS includes/serveStatic/onNotFound); `:1491-1494` (SPIFFS usage metrics).
- Fork extra routes: `WebUIPlugin.cpp:457` `/api/history/`, `:458-466` `/api/history/index.bin`, `:474-476` favicon/apple-touch-icon, `:480` `/fonts/`, `:481-485` onNotFound-before-serveStatic.
- WEBUI gating: `src/display/config/features.h:42-43`; `Controller.cpp:29,98`; `mDNSPlugin.cpp:25,28`.
- BLE-scale gating: `WebUIPlugin.cpp:339,447,1412,1468`.
- Fork lacks nanopb: `ls lib/` → `ble_ota_dfu GaggiMateController NayrodPID NimBLEComm OTA README` (no `NanoPbComm`).
- Fork web server already supports the embed overload: `.pio/libdeps/display/ESPAsyncWebServer/src/ESPAsyncWebServer.h:506` `beginResponse(int, const String&, const uint8_t*, size_t, ...)`.
- `display-headless-4m` present in fork: `platformio.ini:99-101` (`esp32-s3-supermini`); deleted by PR #764's platformio.ini diff.
- No custom partition CSV: `grep board_build.partitions platformio.ini` → none.
- CAR-385 CI matrix: `.github/workflows/build.yml:11-13,21-35,112,117`; `build-nightly.yml:45,72`; `pr-flash.yml` (build_spiffs.sh).
- Sim (PR #191 / `4a2380b4`, on branch `car-windows-sim-port`): `platformio.ini:[env:display-sim]` (`-<display/webassets/>`, `-DGAGGIMATE_SIM`); `sim/platform/LittleFS.h` + `sim/platform/SPIFFS.h` shims; `sim/web/ESPAsyncWebServer.h:50-51,85` (`_blob` zero-copy + embed overload), `sim/web/ESPAsyncWebServer.cpp:306-307,319-326` (blob send path).
- PR #764 (`3bc04041`, merged 2026-06-13): `scripts/embed_webui.py` (`.S` guard `#if !defined(__APPLE__) && !defined(__linux__)` at the generated-asm template), `embed_webui_pre.py`, `build_webui.sh`; `WebUIPlugin.cpp` `serveWebAsset`; `GitHubOTA.{cpp,h}` filesystem-OTA removal; `platformio.ini` deletes `display-headless-4m` + adds `embed_webui_pre.py`.
- `85c0c38a` (SPIFFS→LittleFS + WiFi coex + BT latency, 2026-06-03): FS hunks in `Controller.cpp`, `WebUIPlugin.cpp`, `ShotHistoryPlugin.{cpp,h}`, `ControllerOTA.cpp`, `platformio.ini`; **separable** nanopb hunks in `lib/NanoPbComm/src/Endpoint.{cpp,h}` + `GaggiMateClient.h`; coex hunks in `Controller.cpp`; driver `maxOpenFiles` in the three panel drivers.
