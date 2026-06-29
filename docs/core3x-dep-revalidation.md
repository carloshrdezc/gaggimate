# Core-3.x Dependency / Board / LittleFS Re-Validation — PRO-292

> **Step 4 of umbrella PRO-288** (espressif32 6.12.0 → pioarduino Arduino-esp32 3.x migration).
> This is the **pre-flip validation**. It records, per remaining dependency / custom board /
> LittleFS pipeline, whether it is core-3.x-compatible at its currently-pinned version, and what
> the PRO-293 platform flip must change in lockstep. **No platform flip and no `gnu++20` change is
> made here** — those are PRO-293 (step 5) and PRO-294 (step 6) respectively.

## Headline verdict

The third-party library stack, all three custom boards, and the LittleFS / webassets-embed
pipeline are **core-3.x-ready** — **provided two display-only graphics/HomeKit deps are bumped in
lockstep with the platform flip** (they cannot be bumped now without breaking the current 6.12.0
build), and a small set of **firmware-local `lib/OTA` source sites** are migrated to the core-3.x
networking API. None of the remaining libraries are a blocker beyond those.

NimBLE (PRO-290) and TFT_eSPI (PRO-291) were already handled on `dev-master` and are **out of scope
here** — not revisited.

## Validation method

- **Baseline (committed state, `espressif32@6.12.0` / Arduino core 2.0.17 / xtensa gcc 8.4.0):**
  full CI gate suite run locally and green (see "CI gate results" below). The committed
  `platformio.ini` keeps `platform = espressif32@6.12.0` and `-std=gnu++17` unchanged.
- **3.x target (pioarduino `platform-espressif32` `55.03.39` = Arduino core 3.3.9 / IDF 5.5.4 /
  xtensa gcc 14.2.0):** a **throwaway `[env:display-core3x-probe]`** mirroring `[env:display]` was
  added locally, built with the pioarduino fork of PlatformIO-core (stock `platformio==6.1.19`
  cannot install the core-3.x `framework-arduinoespressif32` package — `FRAMEWORK_DIR=None`), then
  **deleted before commit**. The committed tree contains only this doc.
  - HomeSpan and GFX were bumped to core-3.x-compatible versions **inside the probe only** to let
    the rest of the stack compile far enough to be validated.
  - Reproduction notes (so PRO-293 doesn't rediscover them) are in the appendix.

## Per-dependency findings

| Dependency | Pinned (6.12.0) | Builds on core 2.x (6.12.0)? | Builds on core 3.x? | PRO-293 action |
|---|---|---|---|---|
| `homespan/HomeSpan` | `1.9.1` | ✅ yes (committed) | ❌ **no** — 1.x won't compile on core 3.x | **Bump to `2.x` (latest `2.1.8`) in lockstep.** HomeSpan 2.0.0+ requires Arduino-esp32 core ≥ 3.0.2, and conversely 2.x will NOT build on core 2.x. **Platform-exclusive — cannot bump now.** |
| `moononournation/GFX Library for Arduino` | `1.5.9` | ✅ yes (committed) | ❌ **no** — `spiFrequencyToClockDiv()` signature changed in core 3.x (`(uint32_t)` → `(spi_t*, uint32_t)`); 1.5.9's `Arduino_ESP32SPI.cpp` / `Arduino_ESP32SPIDMA.cpp` fail to compile | **Bump to `≥1.6.5` (latest `1.6.6`) in lockstep.** v1.6.5 explicitly "Fixed build error with ESP32 Arduino core 3.x". Conversely ≥1.6.x `#include "esp32-hal-periman.h"` which **does not exist on core 2.x**, so it will NOT build on 6.12.0. **Platform-exclusive — cannot bump now.** ⚠️ v1.6.3 introduced breaking renames (`RGB565_GREEN`→`RGB565_LIME`; u8g2 `_t_`→`_h_` fonts) — **GaggiMate source uses neither** (verified by grep), so no firmware change needed for the bump. |
| `lewisxhe/SensorLib` | `0.2.3` | ✅ yes (committed) | ✅ **yes** — compiled clean on core 3.x at the pinned 0.2.3 (22 objects) | **No change.** (Newer 0.4.x exists but is not required for core 3.x.) |
| `https://github.com/ESP32Async/AsyncTCP.git` | `#v3.4.9` | ✅ yes | ✅ **yes** — ESP32Async forks explicitly support core 3.x | No change. |
| `https://github.com/ESP32Async/ESPAsyncWebServer.git` | `#v3.9.1` | ✅ yes | ✅ **yes** — compiled clean (13 objects) | No change. |
| `bblanchon/ArduinoJson` | `7.4.3` | ✅ yes | ✅ **yes** — header-only, core-agnostic | No change. |
| `256dpi/MQTT` | `2.5.3` | ✅ yes | ✅ **yes** — compiled clean (5 objects) | No change. |
| `links2004/WebSockets` | `2.7.3` | ✅ yes | ✅ **yes** — compiled clean (7 objects) | No change. |
| `ble_ota_dfu` (local lib) | — | ✅ yes | ✅ **yes** — no core-3.x-sensitive API used | No change. |
| `https://github.com/gaggimate/esp-arduino-ble-scales` | `#135572a` | ✅ yes (committed) | ✅ **yes** — the NimBLE-2.x port (PRO-290) compiled all scale drivers clean on core 3.x | No change. (This pin was already chosen for the NimBLE-2.x line in PRO-290.) |
| `h2zero/NimBLE-Arduino` | `2.2.3` | ✅ yes (PRO-290, committed) | ✅ **yes** — compiled clean (177 objects) | No change (handled in PRO-290). |
| `lvgl/lvgl` | `8.4.0` | ✅ yes | ✅ **yes** — compiled clean (192 objects); core-agnostic C | No change. |
| `nanopb/Nanopb` | `^0.4.9` | ✅ yes | ✅ **yes** — runtime + codegen ran clean on core 3.x | No change. (Codegen needs `protobuf`/`grpcio-tools` in the PIO penv — same as today.) |

### Firmware-local source that PRO-293 must migrate (NOT lib_deps — out of scope for PRO-292)

Once GFX is bumped, the probe build surfaced core-3.x networking-API changes in the firmware's own
`lib/OTA` sources (these compile fine on 6.12.0 today; they are PRO-293 work, listed here so the
flip is low-risk):

1. **`lib/OTA/src/GitHubOTA.cpp:26`** — `NetworkClientSecure::setCACertBundle()` now requires a
   second `size_t size` argument on core 3.x:
   `setCACertBundle(x509_crt_imported_bundle_bin_start)` → `setCACertBundle(bundle, <size>)`.
2. **`lib/OTA/src/ControllerOTA.cpp:73`** — `WiFiClient *tcp = http.getStreamPtr();` — `WiFiClient`
   is not in scope on core 3.x (the network stack was reorganized under `NetworkClient`); needs the
   correct include / type (`NetworkClient`) on the flip.

These are **firmware code changes for PRO-293**, deliberately not made in this slice (PRO-292 scope
is lib_deps / boards / LittleFS validation only).

## Custom boards (`boards/*.json`)

All three boards are **local, self-contained JSON** with **no platform/core version pin**, so they
resolve under any Arduino-3.x platform. Verified that the build-time keys parse and are honored
under core 3.x (the probe resolved `LilyGo-T-RGB` as "LilyGo T-RGB (16M Flash 8M OPI PSRAM)" under
platform `espressif32@55.3.39`, with PSRAM detected):

| Board | `memory_type` | `psram_type` | `partitions` | core-3.x verdict |
|---|---|---|---|---|
| `LilyGo-T-RGB` | `qio_opi` | `opi` | `default_16MB.csv` | ✅ resolves & honored |
| `Gaggimate-Controller` | `qio_opi` | `opi` | `default_8MB.csv` | ✅ resolves (keys identical shape) |
| `esp32-s3-supermini` | `qio_qspi` | `qio` | `no_ota.csv` | ✅ resolves (keys identical shape) |

`memory_type` / `psram_type` / `partitions` are all keys the Arduino-3.x platform still honors — no
board JSON change is required for the flip. (The `no_ota` single-app partition *scheme* for
`esp32-s3-supermini` is selected by this repo's own board definition,
`boards/esp32-s3-supermini.json:5` → `"partitions": "no_ota.csv"`; only the `no_ota.csv` byte
layout — the actual partition sub-table sizes — ships with the Espressif/Arduino (pioarduino core
3.x) platform package and is not vendored here, so a platform bump could change those sizes without
any repo edit. That is why the `display-headless-4m` CI leg gates flash overflow against it.)

## LittleFS / webassets-embed pipeline (GM-90, GM-106)

- `board_build.filesystem = littlefs` (GM-90): the LittleFS API is **stable across the core bump**.
  On the core-3.x probe both `LittleFS.cpp.o` and LVGL's `lv_fs_littlefs.c.o` compiled clean.
- The `data/` → webassets embed pipeline (`scripts/build_webui.sh` → `scripts/embed_webui.py`,
  `scripts/embed_webui_pre.py`) is **platform-independent** (npm build → gzip → pack into a flash
  blob `.S` + manifest header). Re-tested: `build_webui.sh` packs 50 assets (≈652 KB) into
  `src/display/webassets/` and the `embed_webui_pre.py` empty-stub fallback works. No change needed
  for the flip.

## Core-version-sensitive build flags (re-check)

| Flag (in `[display_common].build_flags`) | core-3.x note |
|---|---|
| `-DARDUINO_USB_CDC_ON_BOOT=1` | Still honored on core 3.x (USB-CDC-on-boot). No change. |
| `-DCONFIG_ASYNC_TCP_RUNNING_CORE=0`, `-DCONFIG_ASYNC_TCP_MAX_ACK_TIME`, `-DCONFIG_ASYNC_TCP_PRIORITY`, `-DCONFIG_ASYNC_TCP_QUEUE_SIZE`, `-DCONFIG_ASYNC_TCP_STACK_SIZE` | Consumed by the **ESP32Async AsyncTCP** fork, which honors the same `CONFIG_ASYNC_TCP_*` knobs on core 3.x. No meaning change observed. No change needed for the flip. |
| `-DCORE_DEBUG_LEVEL=3` | Same semantics on core 3.x (Arduino log verbosity). No change. |
| `-DCONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_CMN`, `-DCONFIG_NIMBLE_CPP_LOG_LEVEL`, `-DCONFIG_BT_NIMBLE_PINNED_TO_CORE` | Consumed by mbedTLS / NimBLE-2.x; unchanged meaning. No change. |

No build flag changes meaning or is removed on core 3.x for this project's usage. (The mbedTLS cert
bundle is still present; the API churn is the `setCACertBundle(bundle, size)` signature noted under
`lib/OTA` above, not the flag.)

## CI gate results (committed 6.12.0 state — all green)

Run locally on the committed tree (`platform = espressif32@6.12.0`, `gnu++17`):

| Gate | Result |
|---|---|
| `cd web && npm ci && npm run build` | ✅ |
| `cd web && npm test` | ✅ 225/225 |
| `pio run -e display` | ✅ |
| `pio run -e display-flags-off` | ✅ |
| `pio run -e display-headless-4m` (empty-bundle stub, as CI runs it) | ✅ |
| `pio run -e display-sim` (real embedded bundle + SDL2) | ✅ |
| `pio test -e native` | ✅ 116/116 |
| `pio test -e native-sanitize` | ✅ 116/116 |
| `pio check -e display` (cppcheck, gating) | ✅ 0 HIGH / 0 MEDIUM |
| `pio check -e controller` (cppcheck, gating) | ✅ 0 HIGH / 0 MEDIUM |
| `pio run -e native -t compiledb` + `clang-tidy` | ✅ exit 0 |

## What PRO-293 (the platform flip) must do, in one place

1. Flip `platform = espressif32@6.12.0` → the pioarduino `55.03.39` (or later) zip.
2. **In the same commit**, bump the two platform-exclusive deps:
   - `homespan/HomeSpan@1.9.1` → `@2.x` (e.g. `2.1.8`).
   - `moononournation/GFX Library for Arduino @ 1.5.9` → `@≥1.6.5` (e.g. `1.6.6`).
   - (Both in `[env:display]` and any other env that lists them.)
3. Migrate the firmware-local `lib/OTA` networking sites to the core-3.x API
   (`setCACertBundle(bundle, size)`, `NetworkClient`).
4. Re-run the full CI matrix on core 3.x. Everything else in `lib_deps`, all three boards, and the
   LittleFS/embed pipeline was validated core-3.x-ready by this spike and needs no further change.

## On-hardware verification (deferred)

LittleFS mount + all three boards actually flashing/booting on real hardware is **Carlos's manual
post-merge acceptance step**. This spike validated compile/link + key resolution against core 3.x;
it does not flash a device.

## Appendix — reproducing the core-3.x probe build

```sh
# pioarduino fork of PlatformIO-core (stock platformio can't install core-3.x framework pkg):
uv venv /path/to/.pio-pioarduino
uv pip install --python /path/to/.pio-pioarduino/bin/python \
  "pioarduino-core @ https://github.com/pioarduino/platformio-core/archive/refs/tags/v6.1.19.zip"
# (the `develop` branch reports version 6.1.19a2, which the platform's ">=6.1.19" check REJECTS;
#  use the tagged v6.1.19 which reports a clean 6.1.19)

# nanopb codegen needs protobuf in the pioarduino core's penv (NOT the venv above):
uv pip install --python $HOME/.platformio-pioarduino/penv/bin/python protobuf grpcio-tools

# the pioarduino-core VCS handler does `git fetch --depth=1 origin <sha>`, which GitHub rejects for
# a bare commit SHA — pre-clone the scales fork and point lib_deps at a local file:// path, OR pin a
# tag/branch instead of the 135572a SHA, for the probe only.

PLATFORMIO_CORE_DIR=$HOME/.platformio-pioarduino \
  /path/to/.pio-pioarduino/bin/pio run -e display-core3x-probe
```
