# Desktop simulator (`display-sim`)

Runs the **real** GaggiMate display firmware (`src/display/`) natively on your
computer, with the BLE link to the controller ESP32 **mocked**. The LVGL UI
renders in an SDL window, a fake controller simulates a full brew, and this
fork's WebUI is served on `localhost`. Use it for fast UI iteration, screenshots,
and validation without any hardware.

Supported hosts: **Windows (MinGW-w64)**, **macOS**, **Linux**. (Linear: CAR-399,
building on upstream GM-107.)

## 1. Prepare your computer

### Windows (MinGW-w64)

1. **Toolchain** — a working MinGW-w64 GCC/G++ (UCRT recommended). The PlatformIO
   `native` platform just invokes `g++`/`gcc` from `PATH`, so make sure the
   `g++` first on your `PATH` is a *complete* install (it must ship `cc1plus`; a
   stub MinGW whose `cc1plus` is missing fails with
   `g++: fatal error: cannot execute 'cc1plus'`).
2. **SDL2** — download the **SDL2 MinGW development package** from
   <https://github.com/libsdl-org/SDL/releases> (`SDL2-devel-<ver>-mingw.tar.gz`),
   extract it, and point the build at the `x86_64-w64-mingw32` directory (the one
   containing `include/` and `lib/`) via the `SDL2_DIR` environment variable:

   ```sh
   export SDL2_DIR="/c/Users/<you>/SDL2"   # dir with include/ and lib/
   ```

   On this developer's machine `SDL2_DIR` defaults to `~/SDL2` if unset. Copy
   `SDL2.dll` (from the package's `bin/`) next to the built `program.exe`, or put
   it on `PATH`, so the simulator can find it at runtime.
3. **Node** (only if you want the WebUI) — Node 22+ to build the web bundle.

### macOS

```shell
xcode-select --install     # C/C++ toolchain; skip if already installed
brew install sdl2          # the windowing/graphics backend
```

### Linux (Debian/Ubuntu)

```shell
sudo apt install build-essential libsdl2-dev
```

PlatformIO's `native` platform auto-installs on the first `pio run -e display-sim`.

## 2. WebUI assets

The WebUI is **embedded into the firmware image** as a gzipped flash blob and
served from memory via `WebUIPlugin::serveWebAsset()` — the *same* code path the
device uses (GM-106 / PRO-215). It is **not** read from a filesystem directory,
so there is nothing to stage under `sim_data/`. To serve the real UI, build and
embed the bundle before (or after) building the sim:

```sh
./scripts/build_webui.sh    # npm build -> gzip -> embed_webui.py
```

This populates the git-ignored `src/display/webassets/` (the blob `web_ui.bin`,
the `web_ui_blob.S` stub that defines `gWebUiBlobStart`, and `web_ui_manifest.h`).
The sim env compiles `web_ui_blob.S` on the host (its ESP `.rodata` section is
`#if`-guarded off for native builds), so `gWebUiBlobStart` links and the
embedded assets are served exactly as on the device.

If you skip `build_webui.sh`, the `embed_webui_pre.py` pre-build hook drops an
empty **stub** bundle so the sim still **builds and links** — it just serves an
empty UI (every asset 404s) until you run the script. Re-run `build_webui.sh`
whenever the web sources change to refresh the embedded bundle.

> On Windows, if `npm ci` skips devDependencies (e.g. `vite` missing because
> `NODE_ENV=production`), run `NODE_ENV=development npm ci --include=dev` before
> `build_webui.sh` (which calls `npm ci` itself).

## 3. Build & run

```sh
export SDL2_DIR="/c/Users/<you>/SDL2"   # Windows only
pio run -e display-sim                  # build
pio run -e display-sim -t run           # build + launch
#                                       # (also: .pio/build/display-sim/program[.exe])
```

`-t run` is a custom PlatformIO target (see `scripts/sim_run.py`). In a PlatformIO
IDE (CLion/VSCode) it shows up under the `display-sim` environment as the
**Run Simulator** task.

- **Interact** with the mouse (it acts as the touchscreen).
- **WebUI**: open <http://localhost:8080/> while it runs (port 80 is remapped to
  8080 so it needs no elevated privileges). Live status streams over the
  WebSocket just like on the device.
- **Screenshot mode** (renders briefly, writes a BMP, exits — handy for
  validation/CI):

  ```sh
  .pio/build/display-sim/program --screenshot shot.bmp 4000   # 4000 = delay ms
  ```

- **State** (settings, profiles, shot history) persists under `sim_data/`
  (git-ignored). Delete it to start fresh.

## 4. How it works

Everything simulator-only lives here in `sim/`; the firmware in `src/` is built
unchanged except for a few small `#ifndef GAGGIMATE_SIM` guards.

| Folder | Role |
|---|---|
| `sim/platform/` | Host shims for the Arduino/ESP32 APIs the firmware uses — Arduino core (vendored `String`/`Print`/`Stream`), FreeRTOS, `FS`/`LittleFS`/`SPIFFS`/`SD_MMC`, `Preferences` (NVS), `WiFi`, and the `esp_*` headers. `xTaskCreate*` is a no-op so the sim drives the firmware's loop methods cooperatively on the main thread. |
| `sim/comms/` | A mock `NimBLEClientController` matching this fork's client API, plus a `MockController` thermal/hydraulic model that reacts to the boiler/pump/relay commands the display sends and emits sensor telemetry (temperature, pressure, flow, scale weight). `GaggiMateComm.h`/`NimBLEComm.h` carry the plain protocol vocabulary so the sim has no NimBLE dependency. |
| `sim/driver/` | `SdlDriver` — an SDL2 window wired into LVGL as the display + mouse-as-touch input, plus a screenshot helper. |
| `sim/web/` | Host shim of `ESPAsyncWebServer`/`AsyncWebSocket`/`DNSServer` over a tiny non-blocking HTTP/1.1 + WebSocket server (pumped from the main loop, so handlers never race the firmware). Sockets use BSD sockets on macOS/Linux and Winsock2 on Windows. A no-op `WebSocketsClient` stands in for the cloud-relay client; OTA / BLE-scale endpoints are stubbed. |
| `sim/main.cpp` | Entry point: builds the `Controller`, then runs one cooperative loop (controller + UI + web server + SDL) on the main thread. |

The PlatformIO env is `[env:display-sim]` (`platform = native`) in `platformio.ini`.
`scripts/sim_sdl_flags.py` injects the SDL + socket link flags per host
(`sdl2-config` on macOS/Linux; `SDL2_DIR` + `-lws2_32` Winsock on Windows).

## Caveats

- **Compiled out** for the desktop (via `GAGGIMATE_ENABLE_*` flags + sim guards):
  MQTT/HomeAssistant, HomeKit, mDNS, the WiFi watchdogs, BLE scales, the cloud
  relay, and firmware OTA. The core machine + brew/steam/grind + profiles +
  settings + WebUI all run.
- The mock controller is a plausible model, not a physical one — temperatures and
  pressures are illustrative, not calibrated.
- SDL must own the main thread (macOS requirement), which is why the whole sim is
  a single cooperative loop.
