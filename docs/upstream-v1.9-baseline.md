# Upstream v1.9.0 pristine baseline (PRO-653)

Reference numbers for the v1.9 integration epic (PRO-651). Every later slice on
`feature/upstream-v1.9-integration` can be compared against these figures, so a
change in size, test result or build status can be traced to either the port or
upstream.

- **Tree:** upstream `jniebuhr/gaggimate` tag `v1.9.0` = `43d2a7ba2299c196fd67c001f455985e07e57a49`,
  shallow clone, detached, **no source edits** (`git status` clean apart from ignored build outputs).
  `git describe` → `v1.9.0`, so the firmware reports `BUILD_GIT_VERSION "v1.9.0"`.
- **Measured:** 2026-09-29 on Carlos's Linux host by the `coder` profile.
- **Reproduce:** `scripts/v19_baseline.sh <tree> [logdir]` (added with this doc). See [Commands](#commands).

## Summary

| Check | Result |
|---|---|
| `display` | ✅ builds |
| `display-headless` | ✅ builds |
| `display-headless-8m` | ✅ builds |
| `controller` | ✅ builds |
| `display-sim` | ❌ **fails on this host (GCC 15)**. Missing-include errors; see [display-sim](#display-sim). ✅ builds and runs with a 3-line out-of-tree force-include. |
| `pio run -e display -t buildfs` | ✅ `littlefs.bin` 3,538,944 B (= `spiffs` partition, 3456K) |
| `pio test -e native` (plain) | ✅ **49 cases: 42 passed, 7 skipped, 0 failed** (the 7 skips are `test_ota_loadtest`, which needs `OTA_CHAOS_URL`) |
| OTA test bench (`ota-testbench.yml` replica) | ✅ **30 cases: 30 passed, 0 failed** (`test_ota_loadtest` 7/7 against `chaos_server.py`, `OTA_LOAD_ITERATIONS=60`; `test_ota_download` 23/23) |
| Web `npm ci` | ✅ 344 packages (npm audit: 13 vulnerabilities, 1 low / 1 moderate / 10 high / 1 critical) |
| Web `npm test` | ⚠️ **not applicable**: upstream `web/package.json` has no `test` script (`npm error Missing script: "test"`) and no `*.test.*`/`*.spec.*` files. 0 tests. |
| Web `npm run build` | ✅ vite 7.1.5 |
| Web `npm run lint:check` (extra) | ✅ 0 errors, 6 warnings |
| `scripts/build_webui.sh` → `embed_webui.py` | ✅ **66 assets, 516,276 B** `web_ui.bin` |
| HIL smoke + runtime heap | ⏸ **not run: needs Carlos's spare hardware.** Nothing was flashed and no serial port was opened. See [HIL](#hil-smoke--runtime-heap). |

## Per-env firmware size

"App" = the `Flash:` line from PlatformIO (image bytes counted against the `app0`
OTA slot). "Partition" = the `app0` size in the partition table actually linked
(decoded from `.pio/build/<env>/partitions.bin`). "Static DRAM" = the `RAM:` line
(`.dram0.data` + `.dram0.bss`) out of 327,680 B. This is **not** runtime free heap.

| Env | Board / partition CSV | Status | App (B) | `firmware.bin` (B) | `app0` partition (B) | Partition % | Headroom (B) | Static DRAM (B) | Embedded WebUI |
|---|---|---|---:|---:|---:|---:|---:|---:|---|
| `display` | LilyGo-T-RGB / `default_16MB.csv` | ✅ | 3,545,005 | 3,545,632 | 6,553,600 (6400K) | **54.1%** | 3,008,595 | 93,760 (28.6%) | yes, 516,276 B |
| `display-headless` | LilyGo-T-RGB / `default_16MB.csv` | ✅ | 2,639,053 | 2,639,680 | 6,553,600 (6400K) | **40.3%** | 3,914,547 | 79,768 (24.3%) | yes, 516,276 B |
| `display-headless-8m` | seeed_xiao_esp32s3 / `default_8MB.csv` | ✅ | 2,628,425 | 2,629,040 | 3,342,336 (3264K) | **78.6%** | 713,911 | 79,336 (24.2%) | yes, 516,276 B |
| `controller` | Gaggimate-Controller / `default_8MB.csv` | ✅ | 701,489 | 701,856 | 3,342,336 (3264K) | **21.0%** | 2,640,847 | 38,300 (11.7%) | n/a |
| `display-sim` | native (host) | ❌ as-is / ✅ with compat header | n/a | host ELF `program` 4,603,648 B (text 2,895,342 / data 556,992 / bss 22,520) | n/a | n/a | n/a | n/a | yes, `.incbin` |
| `native` | host test runner | ✅ (see tests) | n/a | n/a | n/a | n/a | n/a | n/a | n/a |

Embedded WebUI check: `gWebUiBlobEnd - gWebUiBlobStart` read with `xtensa-esp32s3-elf-nm` is
516,276 B in `display`, `display-headless` and `display-headless-8m`, equal to `web_ui.bin`. It is
7.9% of a 6400K slot and 15.4% of a 3264K slot.

ELF section breakdown (`xtensa-esp32s3-elf-size -A`, bytes):

| Env | `.iram0.text` | `.dram0.data` | `.dram0.bss` | `.flash.text` | `.flash.rodata` |
|---|---:|---:|---:|---:|---:|
| `display` | 90,287 | 30,872 | 62,888 | 1,977,535 | 1,445,284 |
| `display-headless` | 83,491 | 25,816 | 53,952 | 1,478,579 | 1,050,140 |
| `display-headless-8m` | 81,535 | 25,464 | 53,872 | 1,472,083 | 1,048,316 |
| `controller` | 81,407 | 20,148 | 18,152 | 462,323 | 136,584 |

Partition tables (from the built `partitions.bin`):

```
default_16MB.csv (display, display-headless)      default_8MB.csv (display-headless-8m, controller)
nvs      data nvs      0x9000   20K                nvs      data nvs      0x9000   20K
otadata  data ota      0xe000    8K                otadata  data ota      0xe000    8K
app0     app  ota_0    0x10000  6400K              app0     app  ota_0    0x10000  3264K
app1     app  ota_1    0x650000 6400K              app1     app  ota_1    0x340000 3264K
spiffs   data spiffs   0xc90000 3456K              spiffs   data spiffs   0x670000 1536K
coredump data coredump 0xff0000 64K                coredump data coredump 0x7f0000 64K
```

Observations:

- **`display-headless-8m` is the tight env: 78.6% of its slot, 697 KiB headroom.** It ships the same
  image as `display-headless` (−10.6 KB, board variant only) in half the slot. Any growth from
  porting Carlos's WebUI or plugins shows up here first.
- The full `display` image has about 2.87 MiB free in its 6400K slot.
- `display` compiles with 43 warnings: 40 are `-Wpragmas` from `src/display/ui/default/eez/eez-flow.h:961/969`
  (upstream EEZ header), and **3 are `-Woverflow` in generated `src/display/ui/default/eez/screens.c:2530/2589/2606`**:
  an `lv_opa_t` opacity of 408 is truncated to 152. That is an upstream EEZ UI bug worth noting for the EEZ slice.
  The other ESP32 envs compile with 0 warnings.

## display-sim

As-is it **fails** on this host. The first real errors, verbatim (`pio run -e display-sim`):

```
src/display/core/utils.h:14:20: error: 'runtime_error' is not a member of 'std' [-Wtemplate-body]
   14 |         throw std::runtime_error("Error during formatting.");
src/display/core/utils.h:1:1: note: 'std::runtime_error' is defined in header '<stdexcept>'; this is probably fixable by adding '#include <stdexcept>'
sim/platform/arduino/Print.cpp:51:5: error: 'va_start' was not declared in this scope
sim/platform/arduino/Print.cpp:52:5: error: 'va_copy' was not declared in this scope; did you mean 'bcopy'?
sim/platform/arduino/Print.cpp:54:5: error: 'va_end' was not declared in this scope
*** [.pio/build/display-sim/sim/platform/arduino/Print.o] Error 1
```

After fixing those two, the next one is:

```
src/display/plugins/BLEScalePlugin.h:93:10: error: 'unique_ptr' in namespace 'std' does not name a template type
```

**Likely cause:** host compiler drift, not a logic bug. This host has GCC 15.2.0 / libstdc++ 15.
libstdc++ 15 stopped pulling `<stdexcept>`/`<memory>` in through other headers, and
`Print.cpp` uses `va_*` without `<stdarg.h>`. Upstream's sim README targets macOS/clang, where
these transitive includes still resolve. The ESP32 envs use the Xtensa GCC 8.4 toolchain and are
not affected.

**Workaround used for measurement (no tracked file changed):**
`scripts/v19_sim_gcc15_compat.h` force-includes `<stdarg.h>`, `<stdexcept>` and `<memory>` through
`PLATFORMIO_BUILD_FLAGS="-include …"`. With it, `display-sim` builds (0 errors) and runs.

**Follow-up for the simulator slice:** add the three `#include`s at the use sites
(`sim/platform/arduino/Print.cpp`, `src/display/core/utils.h`, `src/display/plugins/BLEScalePlugin.h`).
Doing it now would modify upstream, which is out of scope for this baseline.

Pitfall: setting `PLATFORMIO_BUILD_FLAGS` changes PlatformIO's project checksum, and PlatformIO
then **wipes every `.pio/build/<env>`**. The script runs the compat retry last and captures
`firmware.bin` sizes first.

Sim smoke, headless (`SDL_VIDEODRIVER=offscreen`, `--screenshot … 12000`):

- Boots through controller setup: `System info: GaggiMate Sim sim-3.0 (proto=3 local=3 dm=1 ps=1 led=1 tof=1)`.
  The mock controller connects, and `[sim-web] WebUI server on http://localhost:8080/` comes up.
- `GET /` → 200, `text/html`, 774 B on the wire (gzip); the decoded page has title `Gaggimate Web UI`.
- `GET /ws` → `101 Switching Protocols`. `evt:status` frames stream continuously (10 in the script's 4 s
  window), and `req:ota-settings` → `res:ota-settings` returns `displayVersion "v1.9.0"`.
- Screenshot: 480×480 BMP written, process exit 0.
- **Sim-only heap numbers:** `heapFree 4194304 / heapLargest 2097152 / heapTotal 8388608`. These are
  **fixed constants** from `sim/platform/esp_heap_caps.h` (4 MiB / 2 MiB / 8 MiB stubs), not
  measurements. The sim has no PSRAM/internal split. Do not use them as a baseline.

## Tests

`pio test -e native` (plain, as a developer would run it):

| Suite | Result |
|---|---|
| `test_autotune_simc` | 4/4 passed |
| `test_button_handler` | 13/13 passed |
| `test_ota_download` | 23/23 passed |
| `test_puckflow_latch` | 2/2 passed |
| `test_ota_loadtest` | 7 skipped (`OTA_CHAOS_URL not set, skipping socket tests`) |
| **Total** | **49 cases: 42 passed, 7 skipped, 0 failed** |

OTA test bench, which reproduces `.github/workflows/ota-testbench.yml` (`OTA_LOAD_ITERATIONS=60 scripts/ota_testbench.sh`:
starts `test/ota_common/chaos_server.py --port 8765 --seed 1`, then `pio test -e native -f "test_ota_*"`):

| Suite | Result |
|---|---|
| `test_ota_loadtest` | 7/7 passed (clean downloads, chaos load, large asset + throttle/drops, asset swap, expired token re-resolve, stall timeout, permanent failure) |
| `test_ota_download` | 23/23 passed |
| **Total** | **30 cases: 30 passed, 0 failed** |

How faithful the replica is: CI uses `ubuntu-latest` + Python 3.11 + `pip install --upgrade platformio`.
This host has PlatformIO Core 6.1.19 (on Python 3.11.15) and runs `chaos_server.py` with system
Python 3.14.7. The command and env (`OTA_LOAD_ITERATIONS=60`) are identical. Not replicated: the
fresh runner and the Actions cache.

Web: `npm test` does not exist upstream (0 tests). The only web gate upstream is the build itself.
`build.yml`/`pr-flash.yml` run `scripts/build_webui.sh`. `npm run lint:check` (not a CI gate) reports 0 errors and 6 warnings.

Upstream CI does not gate `display-sim`, `native` (outside the OTA bench), `display-headless-8m`
or the web lint. `check.yml` runs cppcheck, which was not run here because it is not in the
issue's scope.

## HIL smoke + runtime heap

**HIL smoke + runtime heap: not run (needs Carlos's spare hardware).** Carlos's live GaggiMate is on
this network/USB, so nothing was flashed and no serial port or device IP was touched. The
free-heap column of the AC table is therefore pending: free internal DRAM, largest block and PSRAM
at idle and during a brew with a WS client connected.

Commands for Carlos (spare display + controller only; double-check the port first):

```sh
cd ~/work/gaggimate-v1.9.0-pristine          # already built (scripts/v19_baseline.sh); rebuild if .pio/build was wiped
pio device list                              # identify the SPARE board's port; do NOT use the live machine's

# Controller
pio run -e controller -t upload --upload-port /dev/ttyACM<spare-controller>
# Display (fresh install: seed-profile FS then app; WebUI is embedded in the app)
pio run -e display -t uploadfs --upload-port /dev/ttyACM<spare-display>
pio run -e display -t upload   --upload-port /dev/ttyACM<spare-display>
pio device monitor -e display  --port /dev/ttyACM<spare-display>   # keep a boot log

# Smoke: boot -> BLE pair with controller -> brew -> steam -> Settings/System "check for update" (OTA check)

# Runtime heap (internal DRAM, MALLOC_CAP_DEFAULT|INTERNAL) via the WebUI's own
# res:ota-settings reply. Take readings at idle, during a brew, and with a 2nd WS client (browser tab) open:
python3 ~/work/gaggimate-pro653/scripts/v19_ws_probe.py <spare-display-ip> 80   # script lives in Carlos's repo, not upstream
#   -> {'displayVersion': 'v1.9.0', 'heapFree': ..., 'heapLargest': ..., 'heapTotal': ...}
```

PSRAM free is **not** exposed by any v1.9.0 endpoint or log (`WebUIPlugin` reports internal heap
only; `NetworkWatchdogPlugin::logStats` logs internal heap at `ESP_LOGV`, compiled out at
`CORE_DEBUG_LEVEL=3`). Measuring it on pristine v1.9.0 needs either a serial-console read with a
temporary debug build (then it is no longer pristine; label it) or the PSRAM slice's
instrumentation. Record whichever is used.

Results go here:

| Board | Boot | BLE pair | Brew | Steam | OTA check | heapFree idle | heapLargest idle | heapFree brew+WS | heapLargest brew+WS | PSRAM free |
|---|---|---|---|---|---|---:|---:|---:|---:|---:|
| display (LilyGo T-RGB) | – | – | – | – | – | – | – | – | – | – |
| controller | – | – | – | – | – | n/a | n/a | n/a | n/a | n/a |

## Toolchain

| Tool | Version |
|---|---|
| PlatformIO Core | 6.1.19 (its Python: 3.11.15). 6.2.0 is available but was not used. |
| espressif32 platform | **6.12.0** (pinned `espressif32@6.12.0`, resolved `Espressif 32 (6.12.0)`) |
| framework-arduinoespressif32 | 3.20017.241212+sha.dcc1105b (arduino-esp32 **2.0.17**, IDF 4.4) |
| toolchain-xtensa-esp32s3 | 8.4.0+2021r2-patch5 |
| tool-esptoolpy / tool-mklittlefs | 2.40900.250804 (4.9.0) / 1.203.210628 (2.3) |
| native platform | 1.2.1; host GCC 15.2.0 (Ubuntu 15.2.0-16ubuntu1); SDL2 2.32.10 |
| Unity | 2.6.1 |
| Node / npm | v22.23.0 / 10.9.8 (upstream `.nvmrc` = `lts/jod` = Node 22; CI uses Node 22) |
| vite | 7.1.5 |
| Python (scripts, chaos server) | 3.14.7 |
| Host | Linux 7.0.0-31-generic x86_64, 4 cores, 7 GB RAM |

Resolved libraries (display): NimBLE-Arduino 1.4.3, Nanopb 0.4.92, AsyncTCP 3.5.0, ESPAsyncWebServer 3.12.0,
ArduinoJson 7.4.3, MQTT 2.5.3, esp-arduino-ble-scales (v1.0.4 → sha 7fa8589), HomeSpan 1.9.1,
Improv WiFi 0.0.4, SensorLib 0.2.3, lvgl 8.4.0, GFX Library for Arduino 1.5.9.
Controller: GaggiMateController 1.0.0, NimBLE-Arduino 1.4.3, Nanopb 0.4.92, ArduinoJson 7.4.3.

## Commands

Run as-is (sequential, `-j 3`, niced) with:

```sh
SIM_COMPAT_HEADER=scripts/v19_sim_gcc15_compat.h \
  scripts/v19_baseline.sh ~/work/gaggimate-v1.9.0-pristine ~/work/pro653-logs/final2
```

which executes, in `<tree>`:

```sh
unset NODE_ENV                                   # host exports NODE_ENV=production; npm ci would skip vite
cd web && npm ci                                 # web install
(npm test)                                       # skipped: no "test" script upstream
scripts/build_webui.sh                           # npm ci + npm run build + gzip + embed_webui.py -> src/display/webassets/
pio run -e display -t buildfs
pio run -e display -j 3
pio run -e display-headless -j 3
pio run -e display-headless-8m -j 3
pio run -e controller -j 3
pio run -e display-sim -j 3                      # fails on GCC 15 (see above)
pio test -e native
OTA_LOAD_ITERATIONS=60 scripts/ota_testbench.sh  # == ota-testbench.yml
PLATFORMIO_BUILD_FLAGS="-include scripts/v19_sim_gcc15_compat.h" pio run -e display-sim -j 3   # only if SIM_COMPAT_HEADER set
SDL_VIDEODRIVER=offscreen .pio/build/display-sim/program --screenshot sim-shot.bmp 12000 &
curl --compressed http://127.0.0.1:8080/ ; python3 scripts/v19_ws_probe.py 127.0.0.1 8080 4
```

Extras run by hand for this doc: `npm run lint:check`; `xtensa-esp32s3-elf-nm -S` (blob symbols) and
`xtensa-esp32s3-elf-size -A` on each `firmware.elf`; `gen_esp32part.py` on each `partitions.bin`.

The script writes `summary.tsv` (step, exit code, seconds), `sizes.tsv`, `toolchain.txt` and one log per step.

## Decisions / caveats

- **Build timings are not a baseline.** The host was shared with another coder run (load average
  peaked near 70 on 4 cores). Cold builds took display 322 s, display-headless 629 s,
  display-headless-8m 336 s and controller 157 s. Treat them as noise.
- `NODE_ENV=production` is exported in this host's shell. The script unsets it to match CI;
  otherwise `npm ci` drops devDependencies and the vite build fails.
- `web npm test`: the issue asked for it, but upstream has no web test runner. It is recorded as
  "not applicable" rather than as a failure.
- `display-sim` is reported as **failing as-is** because that is the honest pristine result. The compat
  header is a measurement aid only and is not proposed as an upstream change here.
- The "free heap" AC column is filled with static DRAM (link-time) for now, clearly labelled.
  Runtime heap is HIL-only (see above).
- `pio run -e display -t buildfs` runs **before** the app builds, in the same order as `build.yml`.
- cppcheck (`check.yml`) was not run because it was outside this issue's scope. The display leg
  takes ~10 min here.
