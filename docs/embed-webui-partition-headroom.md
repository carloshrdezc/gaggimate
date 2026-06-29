# PRO-211 — Web-UI footprint + partition headroom; 4 MB board policy (embed-WebUI step 1)

**Status:** Measurement / decision chore. No production firmware or web code changed by this document.
**Issue:** PRO-211 (Gaggimate; labels `chore` / `firmware`). Parent migration: PRO-210
(`docs/spike-embed-webui-littlefs-migration.md`, §3.6 / §4.1 / §5 task 1).
**Branch / base:** `pro-211-embed-webui-headroom` off `dev-master`.
**Measured on:** dev-master @ `b6e0c957` (Merge PR #193 — embed-WebUI spike), Windows + git-bash,
PlatformIO Core 6.1.19, `platform = espressif32@6.12.0`, Node v24.13.0 / npm 11.6.2.

This issue gates every subsequent embed-WebUI implementation PR. It answers two questions with
**real builds**, not estimates:

1. How big is the gzipped web bundle that would be embedded into the app image?
2. Does `current app image + embedded UI` fit each board's **default** partition app slot, with
   **OTA double-bank** (app0 *and* app1) where applicable?

---

## 0. TL;DR — go/no-go + 4 MB policy

| Env | Board | Flash | App slot (each bank) | OTA banks | App used now | App + UI | Verdict |
|---|---|---|---|---|---|---|---|
| `display` | LilyGo-T-RGB | **16 MB** | 6400 KiB (6,553,600 B) | app0 **+** app1 | 3,213,425 B (49.0 %) | 3,869,256 B (**59.0 %**) | ✅ **GO** — 40.9 % free per bank |
| `display-headless-8m` | seeed_xiao_esp32s3 | 8 MB | 3264 KiB (3,342,336 B) | app0 **+** app1 | 2,061,717 B (61.7 %) | 2,717,548 B (**81.3 %**) | ✅ **GO** — 18.7 % free per bank |
| `display-headless-4m` | esp32-s3-supermini | 4 MB | 2 MiB (2,097,152 B) | **app0 only (`no_ota`)** | 2,071,441 B (**98.8 %**) | 2,727,272 B (**130 %**) | ❌ **NO-GO** — overflows by ~615 KiB |

**Measured embedded-UI blob size: 655,831 bytes ≈ 640.5 KiB ≈ 0.625 MiB** (upstream
`build_webui.sh` recipe — see §1).

**Recommended 4 MB policy: drop `display-headless-4m`, OR keep it only with
`GAGGIMATE_ENABLE_WEBUI=0` (1-byte stub).**

- Embedding the real UI on the 4 MB esp32-s3-supermini is **flatly infeasible**: its default
  partition is `no_ota.csv` (a single 2 MiB app partition, **no OTA second bank at all**), and the
  current headless app *already* fills **98.8 %** of it (only 25,711 B free) — before adding one
  byte of UI. The 656 KiB blob is ~25× the free space.
- This empirically confirms the spike's hypothesis (§3.6) for why upstream PR #764 deleted the
  env. The right fork policy is **spike §3.6 option (a) "drop it"** or **(b) "keep it with
  `GAGGIMATE_ENABLE_WEBUI=0`"**. Option (c) "keep with embedded UI" is **impossible** and should
  be struck from the spike's option list. Recommendation between (a) and (b) is a product call for
  Carlos (does any fork user run the supermini headless without a web UI?); (b) is the
  lower-regret default because it preserves the build target at nil flash cost.

Both 8 MB-class targets (the 16 MB LilyGo display and the 8 MB XIAO headless) have comfortable
headroom in **both** OTA banks. Embed-WebUI is **go** for them.

---

## 1. Web-UI bundle footprint — measured

### Method (matches upstream PR #764 exactly)

The embedded blob is **not** the same as today's SPIFFS `data/w` tree. The footprint that lands in
the app image is produced by upstream's `build_webui.sh` + `embed_webui.py` recipe:

1. `cd web && npm ci && npm run build` → Vite emits `web/dist`.
2. `gzip -f dist/assets/*.js dist/assets/*.css dist/*.html` — **only** JS, CSS and HTML are
   gzipped (served `Content-Encoding: gzip`). Everything else (fonts `.otf`, `gm.png`, `.svg`,
   `app.webmanifest`, `.png` icons) is packed **uncompressed**.
3. `embed_webui.py --src web/dist --out src/display/webassets` walks **all** of `web/dist` and
   concatenates every file into one `web_ui.bin` blob (the manifest records offset/length/
   content-type/gzip-flag per asset). The app image grows by the **total size of that blob**.

Reproduced here against a clean `npm run build`:

```
gzip -f assets/*.js assets/*.css *.html      # in web/dist copy
find . -type f -printf '%s\n' | awk '{s+=$1} END {print s}'
# => 655831
```

### Result

**Total embedded blob = 655,831 bytes = 640.46 KiB ≈ 0.625 MiB.**

This is the per-OTA-slot app-image growth on every board that embeds the UI.

### Per-file breakdown (largest first; `.gz` = gzipped in the blob, others raw)

| Bytes | File | Note |
|---|---|---|
| 150,548 | `fonts/Ndot57Caps-Regular.otf` | **raw** (fonts not gzipped) |
| 148,642 | `assets/BCA4lhpU.js.gz` | main app chunk (452 KB → 149 KB gz) |
| 75,013 | `assets/CaXTQN5I.js.gz` | (213 KB → 74 KB gz) |
| 45,208 | `fonts/NType82-Headline.otf` | **raw** |
| 44,948 | `fonts/NType82-Regular.otf` | **raw** |
| 34,427 | `assets/DUTLJwU9.css.gz` | main stylesheet |
| 24,517 | `assets/Cu0LStqA.js.gz` | |
| 14,784 | `assets/SP_Vlk-C.js.gz` | |
| 12,546 | `assets/DQWokSqS.js.gz` | |
| 12,030 | `assets/r0kQsr0L.js.gz` | |
| 11,999 | `gm.png` | **raw** |
| 9,094 | `assets/BHfnBsgq.js.gz` | |
| 8,766 | `assets/Dru-6vOW.js.gz` | |
| 8,546 | `assets/Bjxe_TzK.js.gz` | |
| 8,229 | `assets/ByxN0719.js.gz` | |
| 6,740 | `assets/BiMLITr7.png` | **raw** |
| … | ~30 smaller JS/CSS chunks + `cup.svg`, `gm.svg`, `app.webmanifest`, `index.html.gz` | |

**Observation worth flagging for later optimisation PRs:** the three `.otf` fonts total
**240,704 B (37 % of the whole blob)** and are stored **uncompressed**. `gm.png` adds another
12 KB raw. If footprint ever becomes the binding constraint on the 8 MB headless target, gzipping
fonts (or subsetting them / converting to `.woff2`) is the single biggest lever — but at today's
sizes neither 8 MB-class board needs it.

---

## 2. Partition layout per board — measured from the compiled image

The fork ships **no custom partition CSV** (`grep board_build.partitions platformio.ini` → none),
so each env uses its board JSON's default `build.arduino.partitions`. Tables below are decoded
straight from the `partitions.bin` PlatformIO actually built (`gen_esp32part.py`), not assumed:

### `display` → `LilyGo-T-RGB.json` → `default_16MB.csv` (flash 16 MB)
```
nvs,     data, nvs,      0x9000,   20K
otadata, data, ota,      0xe000,   8K
app0,    app,  ota_0,    0x10000,  6400K   (6,553,600 B)
app1,    app,  ota_1,    0x650000, 6400K   (6,553,600 B)   <- OTA second bank
spiffs,  data, spiffs,   0xc90000, 3456K
coredump,data, coredump, 0xff0000, 64K
```
> Note: the board's display name says "16M Flash 8M OPI PSRAM" — it is a **16 MB flash** part
> (`upload.flash_size: 16MB`, `maximum_size: 16777216`), not 8 MB. The "8M" refers to PSRAM. The
> task framed `display` as an 8 MB board; the real default table is 16 MB / 6400 KiB app slots.

### `display-headless-8m` → `seeed_xiao_esp32s3.json` → `default_8MB.csv` (flash 8 MB)
```
nvs,     data, nvs,      0x9000,   20K
otadata, data, ota,      0xe000,   8K
app0,    app,  ota_0,    0x10000,  3264K   (3,342,336 B)
app1,    app,  ota_1,    0x340000, 3264K   (3,342,336 B)   <- OTA second bank
spiffs,  data, spiffs,   0x670000, 1536K
coredump,data, coredump, 0x7f0000, 64K
```

### `display-headless-4m` → `esp32-s3-supermini.json` → **`no_ota.csv`** (flash 4 MB)
```
nvs,     data, nvs,      0x9000,   20K
otadata, data, ota,      0xe000,   8K
app0,    app,  ota_0,    0x10000,  2M      (2,097,152 B)   <- SINGLE app, NO app1
spiffs,  data, spiffs,   0x210000, 1920K
coredump,data, coredump, 0x3f0000, 64K
```
> **Decisive:** the 4 MB supermini's default partition is `no_ota.csv` — one 2 MiB app partition
> and **no OTA second bank**. So OTA-double-bank doesn't even apply here; there is only one slot,
> and it must hold app + UI together.

---

## 3. Current app-image sizes — measured (real `pio run`)

All three envs built clean (`[SUCCESS]`) in this environment. Flash usage is from PlatformIO's
post-link size check (= app bytes that must fit the `ota_0` slot); `firmware.bin` size is the
padded image written by esptool.

| Env | `pio run` flash used (B) | % of app slot | `firmware.bin` (B) | App slot (B) |
|---|---|---|---|---|
| `display` | 3,213,425 | 49.0 % | 3,214,048 | 6,553,600 |
| `display-headless-8m` | 2,061,717 | 61.7 % | 2,062,336 | 3,342,336 |
| `display-headless-4m` | 2,071,441 | **98.8 %** | 2,072,064 | 2,097,152 |

The headroom math below uses the **flash-used** figure (the authoritative "does it fit the slot"
number from the linker/size check); `firmware.bin` is within a few hundred bytes and tells the
same story.

---

## 4. Headroom math — app + embedded UI, with OTA double-bank

Embedded blob added to each app slot = **655,831 B**. For OTA boards, the grown image must fit
**both** `app0` and `app1` (they are equal-sized, so one check suffices). For `no_ota`, only the
single `app0`.

### `display` (LilyGo-T-RGB, 16 MB, app slot 6,553,600 B ×2)
```
app now      3,213,425
+ embed UI     655,831
= app + UI   3,869,256   (59.0 % of 6,553,600)
free/bank    2,684,344   (40.9 %)
```
Fits **both** OTA banks with ~2.56 MiB to spare in each. ✅ **GO.**

### `display-headless-8m` (seeed_xiao_esp32s3, 8 MB, app slot 3,342,336 B ×2)
```
app now      2,061,717
+ embed UI     655,831
= app + UI   2,717,548   (81.3 % of 3,342,336)
free/bank      624,788   (18.7 %)
```
Fits **both** OTA banks with ~610 KiB to spare in each. ✅ **GO** (comfortable, but the tightest of
the two viable targets — track footprint growth here over time; fonts are the obvious lever, §1).

### `display-headless-4m` (esp32-s3-supermini, 4 MB, **single** app slot 2,097,152 B)
```
app now      2,071,441   (already 98.8 % — only 25,711 B free)
+ embed UI     655,831
= app + UI   2,727,272   (130.0 % of 2,097,152)
overflow      -630,120   (image is ~615 KiB larger than the entire app partition)
```
The app **already** nearly fills its single 2 MiB partition without any UI. Adding the 656 KiB
blob overflows by ~615 KiB. There is no OTA bank to worry about because there isn't one — the
board can't OTA at all on its default table. ❌ **NO-GO.** Embedding the UI here is impossible
without a custom partition table the 4 MB flash physically cannot provide (2 MiB app + 656 KiB UI
+ FS + nvs/otadata/coredump > 4 MiB, and that's still single-bank).

---

## 5. Decisions

### 5.1 Go/no-go per board (gates the rest of PRO-210)

- **`display` (LilyGo-T-RGB, 16 MB): GO.** 40.9 % free per OTA bank after embedding.
- **`display-headless-8m` (seeed_xiao_esp32s3, 8 MB): GO.** 18.7 % free per OTA bank after embedding.
- **`display-headless-4m` (esp32-s3-supermini, 4 MB): NO-GO.** Cannot fit; no OTA bank; app already
  at 98.8 %.

Embed-WebUI is **viable for the two 8 MB-class targets** and should proceed for them (PRO-210
tasks 2/4/5/7). The 4 MB target must be handled per §5.2 before the CI-matrix PR (PRO-210 task 7)
lands, so the matrix doesn't try to build an impossible embedded-UI image for it.

### 5.2 4 MB board policy (resolves spike §3.6)

Spike §3.6 listed three options. With real numbers:

| Option | Feasible? | Notes |
|---|---|---|
| (a) Drop `display-headless-4m` (follow upstream PR #764) | ✅ | Simplest; matches upstream. Correct **if** no fork user depends on the supermini. |
| (b) Keep env, force `GAGGIMATE_ENABLE_WEBUI=0` (1-byte stub blob) | ✅ | Embedded blob is the stub (~1 B), nil flash impact; the board keeps building. App stays at ~98.8 % (UI was never on it anyway in a webless config). Preserves the target at zero cost. |
| (c) Keep env **with** embedded UI | ❌ **impossible** | Overflows the single 2 MiB app partition by ~615 KiB; no OTA bank exists. Strike from the option list. |

**Recommendation:** **(b)** as the low-regret default — keep `display-headless-4m` but pin
`GAGGIMATE_ENABLE_WEBUI=0` for it so the embed pipeline emits the stub and flash impact is nil;
fall back to **(a) drop it** if Carlos confirms no one runs the supermini. Either way, **(c) is off
the table**. The CI-matrix migration PR (PRO-210 task 7) must implement (a) or (b), not silently
inherit upstream's deletion without a decision.

> Caveat for (b): the env currently links at **98.8 %** of its 2 MiB app slot *today*, with no UI.
> That is already razor-thin — any future firmware growth (a new plugin, a library bump) will push
> a webless 4 MB build over 2 MiB and break it regardless of embed-WebUI. If we keep the target via
> (b), it needs its own size budget/CI guard, independent of this migration. This is a pre-existing
> fragility the embed work merely surfaces.

---

## Appendix — commands & evidence

```bash
# Bundle footprint (upstream build_webui.sh recipe)
cd web && npm ci && npm run build
cp -R dist /tmp/m && cd /tmp/m
gzip -f assets/*.js && gzip -f assets/*.css && gzip -f *.html
find . -type f -printf '%s\n' | awk '{s+=$1} END {print s}'      # => 655831

# App-image sizes (real builds)
pio run -e display                # Flash: 3,213,425 / 6,553,600 (49.0%)
pio run -e display-headless-8m    # Flash: 2,061,717 / 3,342,336 (61.7%)
pio run -e display-headless-4m    # Flash: 2,071,441 / 2,097,152 (98.8%)

# Partition tables actually built
GEN=$(find ~/.platformio/packages -name gen_esp32part.py | head -1)
python "$GEN" .pio/build/display/partitions.bin             # 16MB: app0/app1 = 6400K each
python "$GEN" .pio/build/display-headless-8m/partitions.bin #  8MB: app0/app1 = 3264K each
python "$GEN" .pio/build/display-headless-4m/partitions.bin #  4MB: app0 = 2M, NO app1 (no_ota)

# Board default partition tables
#   boards/LilyGo-T-RGB.json        -> default_16MB.csv  (flash_size 16MB)
#   <platform>/seeed_xiao_esp32s3   -> default_8MB.csv
#   boards/esp32-s3-supermini.json  -> no_ota.csv         (flash_size 4MB)
```

- Bundle build: `web/dist` from `npm run build`, gzipped per upstream `build_webui.sh`;
  `embed_webui.py` concatenates all of `web/dist` into the blob (PR #764).
- All `pio run` results: `[SUCCESS]`, size check from PlatformIO post-link memory usage.
- Partition CSVs: `framework-arduinoespressif32/tools/partitions/{default_16MB,default_8MB,no_ota}.csv`,
  cross-checked against each env's compiled `partitions.bin`.
