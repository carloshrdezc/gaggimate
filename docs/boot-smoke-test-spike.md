# Spike: Pre-nightly on-device boot smoke-test (HIL)

- **Issue:** PRO-332 (`spike`, `firmware`)
- **Status:** findings + interim gate shipped — `scripts/boot_smoke_test.py` (approach B)
- **Related regressions this would have caught:** PRO-329, PRO-330, PRO-331

## 1. The gap: why CI and the simulator are both blind to boot regressions

Three platform-migration regressions all shipped to nightly during the
Arduino-esp32 3.x / `pioarduino` platform bump (PRO-293 / PRO-290), and **every
one of them builds perfectly clean**:

| Issue | Failure | When it fires |
|-------|---------|---------------|
| PRO-329 | Legacy ADC vs `driver_ng` conflict → `abort()` at C++ static-init | On real boot, before `setup()` |
| PRO-330 | NimBLE 2.x `esp_bt_controller_init` `LoadProhibited` panic | On real boot, inside `NimBLEDevice::init()` |
| PRO-331 | `Preferences::begin(readOnly=true)` races NVS init → every setting (incl. WiFi) silently resets to default each boot | On real boot, first NVS read |

None of these are compile errors. They are **runtime / boot-time / NVS-init**
faults that only manifest when the firmware actually boots on the ESP32-S3 and
exercises the IDF hardware layers.

### CI is compile-only

`.github/workflows/ci.yml` runs (and we run locally before pushing):

```
cd web && npm ci && npm run build
pio run -e display            # compiles the firmware — clean for all three bugs
pio test -e native            # host unit tests (no IDF, no hardware)
pio test -e native-sanitize   # host tests + ASan/UBSan
pio check -e display / controller   # cppcheck (static analysis)
pio run -e native -t compiledb && clang-tidy ...
```

`pio run -e display` produces a valid `.pio/build/display/firmware.bin` for all
three regressions. cppcheck/clang-tidy are static analyzers — they never run the
binary. There is **no step anywhere in CI that boots the image.**

### The simulator (`[env:display-sim]`) is structurally blind

It is tempting to assume the desktop simulator covers this. It cannot, by
construction. Verified live against `dev-master` (`platformio.ini`, the
`[env:display-sim]` block starting ~L373):

- **`platform = native`** — it is an x86 host binary, not an ESP32-S3 image.
  There is no Xtensa boot ROM, no second-stage bootloader, no `app_main`, no
  IDF startup. **PRO-329's static-init ADC/`driver_ng` abort cannot occur** —
  that conflict is between two ESP-IDF ADC drivers that don't exist on the host.
- **`build_src_filter` excludes `-<display/drivers/>` and `-<display/main.cpp>`**
  — the panel / ADC / hardware driver init code is *not even compiled*, and the
  ESP `app_main`→`setup()` entry is replaced by the sim's own `main`.
- **BLE is mocked.** `sim/comms` provides a host stub for the NimBLE link; the
  plugin list compiles `BLEScalePlugin`'s `#else` no-op branch
  (`GAGGIMATE_ENABLE_BLE_SCALE=0`). There is no real `esp_bt_controller`, so
  **PRO-330's `esp_bt_controller_init` `LoadProhibited` cannot occur.**
- **`Preferences` is a host shim** (`sim/platform`). It is an in-memory /
  file-backed key-value map with no `nvs_flash_init()` and no static-init
  ordering relative to the Arduino core. So **PRO-331's read-only-`begin()`
  races-NVS-init bug cannot occur** — there is no NVS to race.
- MQTT / HomeKit / mDNS / network-watchdog plugins are filtered out entirely.

So the sim validates UI logic and the WS/HTTP API surface on a host — which is
exactly what it is for — but it is *the wrong tool* for boot/IDF/NVS regressions.
**Only a real boot on real silicon sees these three classes of bug.**

## 2. Three approaches, scored against the three concrete bugs

| | PRO-329 (static-init abort) | PRO-330 (BLE init panic) | PRO-331 (NVS settings reset) | Infra cost | Maintenance |
|--|--|--|--|--|--|
| **(A) Self-hosted GitHub Actions HIL runner** (wired ESP32-S3 on the CI box) | ✅ | ✅ | ✅ | High (dedicated runner + wired board + USB watchdog) | High (board health, flaky-USB, runner upkeep) |
| **(B) Documented manual boot-gate script** (maintainer runs on the reference board before tagging) | ✅ | ✅ | ✅ | ~Zero (a Python script + a board the maintainer already owns) | Low (relies on human discipline to run it) |
| **(C) QEMU / Wokwi headless boot in CI** | ✅ | ⚠️ partial / ❌ | ⚠️ maybe | Medium (emulator wiring in CI) | Medium | 

Detail on each:

### (A) Self-hosted HIL runner — highest fidelity, real infrastructure

A self-hosted GitHub Actions runner on Carlos's Linux box, with the
LilyGo-T-RGB permanently wired on `/dev/ttyACM0` and the full PlatformIO/esptool
toolchain. CI flashes + boots the real image on every firmware PR. This is the
**only fully-automated option that exercises real silicon**, so it catches all
three bugs deterministically and needs no human in the loop.

Costs: a dedicated always-on runner; a physically wired board that can wedge
(USB enumeration flakiness, a brick that needs a hardware reset / BOOT-button
re-flash); board-health monitoring; and the security surface of a self-hosted
runner executing PR code. Worth automating as a **follow-up**, not the first
step.

### (B) Documented manual boot-gate — RECOMMENDED interim

A committed, ready-to-run script (`scripts/boot_smoke_test.py`) that a maintainer
runs on the reference board immediately **before tagging a nightly** (and on any
firmware / platform / lib-pin PR). It:

1. Flashes bootloader + partitions + `boot_app0` + firmware via **esptool** and
   asserts every region reports `Hash of data verified`.
2. Captures ~20 s of boot serial via **pyserial** and asserts the boot is healthy
   (see §3 for the exact assertion→bug mapping).

Near-zero infrastructure: it is one stdlib-plus-`pyserial`/`esptool` Python file
and a board the maintainer already has on the bench. Its only weakness is that it
relies on **human discipline** to actually run it before tagging — which §4
(CONTRIBUTING note) addresses by documenting *when* it is mandatory. This is the
right first move: it closes the gap today with no new infrastructure, and the
same script is the payload a future approach-(A) runner would invoke.

### (C) QEMU / Wokwi headless boot in CI — partial, no hardware

Emulated boot (Espressif's QEMU fork, or Wokwi's CI action) runs the real image
headless in CI with no physical board.

- **PRO-329 (static-init abort):** ✅ likely caught — the abort happens in the
  firmware's own static-init / driver setup, which the emulator runs.
- **PRO-330 (BLE init panic):** ❌ **not caught.** QEMU/Wokwi do **not** emulate
  a real `esp_bt_controller` / Bluetooth radio. The very call that panicked
  (`esp_bt_controller_init`) is exactly what the emulator stubs or cannot model,
  so the LoadProhibited would not reproduce. This is the decisive gap.
- **PRO-331 (NVS reset):** ⚠️ maybe — depends on whether the emulated NVS flash
  partition reproduces the Arduino-core-vs-static-init ordering race. Not
  reliable.

Verdict: a useful **complement** to (B) for the static-init class, but it cannot
replace a real boot because it misses the BLE-controller class entirely. Track
as a possible future CI add-on, not the primary gate.

## 3. Recommendation + exact assertion → bug mapping

**Ship (B) now** as the committed `scripts/boot_smoke_test.py`; **evaluate (A)**
(self-hosted HIL automation) as a follow-up (filed as a separate PRO issue,
referenced from this spike). (C) is optional future complement for the
static-init class only.

The script's assertions map to the three bugs as follows. Every marker below is
a **real** string the current firmware emits (verified in source on
`dev-master`):

| Assertion | Catches | Real source marker / mechanism |
|-----------|---------|--------------------------------|
| No `abort()` / `Guru Meditation` / `assert failed` in the boot window | **PRO-329** | A `driver_ng` static-init abort prints a Guru Meditation / `abort()` panic and never reaches `setup()`. |
| Reaches the BLE-init marker **and continues past it without a panic** | **PRO-330** | `NimBLEClientController::initClient()` logs `Pre-BLE-init heap: free=...` immediately before `NimBLEDevice::init()` (the call that LoadProhibited'd). Seeing that line **followed by** steady-state markers (not a `LoadProhibited` backtrace) proves BLE init survived. |
| No `LoadProhibited` / `Backtrace:` in the boot window | **PRO-330** | The panic signature of the NimBLE 2.x crash. |
| Reaches steady-state markers: `Started webserver` (+ BLE scan alive) | PRO-329/330 liveness | `WebUIPlugin` logs `Started webserver` (`WebUIPlugin.cpp`). A board that aborted early never gets here. |
| **Settings round-trip:** write a known value via `POST /api/settings`, reboot, read it back via `GET /api/settings`, assert `read == written` | **PRO-331** | PRO-331 made every NVS read silently return the default each boot. A value that does **not** survive a reboot reproduces the bug exactly. `/api/settings` is the real GET/POST endpoint (`WebUIPlugin.cpp` L615). |
| **No boot loop:** count the ROM reset banner `ESP-ROM:esp32s3` in the window — must be exactly `1` | PRO-329 (loop form) | A static-init abort reboots in a loop; >1 ROM banner in 20 s = boot loop. |

Any assertion miss → non-zero exit. The full captured serial is written to a
file as the run artifact for post-mortem.

## 4. On-hardware verification is a manual acceptance step

The script is **pure offline tooling**: it imports cleanly, lints cleanly, and
`python3 scripts/boot_smoke_test.py --help` works with **no board attached**, so
it is safe in CI (CI never flashes hardware). The actual on-board boot run —
executing `boot_smoke_test.py` against the physical LilyGo-T-RGB on
`/dev/ttyACM0` — is the maintainer's manual pre-nightly acceptance step,
documented in `CONTRIBUTING.md` ("Pre-nightly on-device boot smoke-test").

## 5. Follow-ups

- **Approach (A):** automate the boot-gate on a self-hosted GitHub Actions HIL
  runner with the wired ESP32-S3. Filed as a separate PRO issue (see the PR body
  / `Ref` link). The runner would invoke this same `boot_smoke_test.py`.
- **Approach (C):** evaluate QEMU/Wokwi headless boot in CI as a complement that
  catches the static-init class (PRO-329) automatically, accepting that it cannot
  cover the BLE-controller class (PRO-330).
