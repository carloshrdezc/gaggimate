# Spike: espressif32 6.12 → 7.x platform bump (C++20 enabler) — PRO-257

**Status: NO-GO on the official `platformio/espressif32` 7.x line as a C++20
enabler. The C++20 prerequisite is an Arduino-core-3.x platform (the community
`pioarduino` fork), NOT the official 7.x platform — which keeps the same
gcc 8.4.0 Arduino toolchain as 6.12.0.**

This spike investigated bumping `platform = espressif32@6.12.0` to the 7.x line
to unlock C++20, coordinating with the CAR-340 C++ standard work
(`docs/cpp-standard-spike.md`). The headline finding overturns the issue's
premise: **the official 7.x platform does not give an Arduino-framework project
a newer GCC.**

## TL;DR

| Question | Answer |
|---|---|
| Does official `espressif32@7.0.1` ship a GCC that supports `-std=gnu++20`? | **No, not for `framework = arduino`.** 7.x still bundles **Arduino core v2.0.17 (IDF v4.4.7)** → the same **xtensa gcc 8.4.0** as 6.12.0. The "IDF toolchains v15.2.0 / gcc 14+" in the 7.0.0 notes apply to the **`framework = espidf`** path only. GaggiMate is `framework = arduino`. |
| What is the 7.x release actually about? | Adding **ESP-IDF v6.0 / v6.0.1** support to the *ESP-IDF* framework. Arduino support is unchanged from the 6.x line. |
| What is the real C++20 enabler? | A platform built on **Arduino-esp32 core 3.x** (IDF 5.1+, **gcc 13**). The official platform froze Arduino at 2.0.17; the maintained route is the community fork **`pioarduino/platform-espressif32`** (currently Arduino 3.3.9 / IDF 5.5.4 / gcc 13). |
| Recommendation | **Do NOT adopt official 7.x for C++20.** If C++20 (CAR-340) is the goal, the real change is migrating to a `pioarduino` Arduino-3.x platform — a large, library-fallout-heavy change that must be its own effort. Stay on `espressif32@6.12.0` / `gnu++17` until that is scheduled. |

## What the official 7.x platform actually ships

Source: `platformio/platform-espressif32` GitHub releases.

| Release | Date | ESP-IDF framework | **Arduino framework** |
|---|---|---|---|
| 7.0.1 | May 12 | v6.0.1 | **v2.0.17 (based on IDF v4.4.7)** |
| 7.0.0 | Apr 30 | v6.0.0 (toolchains v15.2.0+20251107) | **v2.0.17 (based on IDF v4.4.7)** |
| 6.13.0 | Feb 26 | v5.5.3 | **v2.0.17 (based on IDF v4.4.7)** |
| 6.12.0 (current pin) | Jul 31, 2025 | v5.5.0 | **v2.0.17 (based on IDF v4.4.7)** |

The Arduino framework column is **identical across 6.12.0 → 7.0.1**. The xtensa
cross-compiler for the Arduino path is therefore still
`xtensa-esp32s3-elf-g++ 8.4.0` — exactly the compiler CAR-340 slice 1 already
proved rejects `-std=gnu++20` and lacks `<span>`/`<ranges>`/concepts/`<=>`.

**Why the confusion:** the 7.0.0 notes ("ESP-IDF v6.0", "IDF toolchains to
v15.2.0") describe the **ESP-IDF** framework toolchain, which is modern. But
`platformio/platform-espressif32` deliberately stopped advancing the **Arduino**
framework past 2.0.17 (it predates the upstream split documented in
platform-espressif32#1225). For a `framework = arduino` project, bumping to 7.x
buys **zero** C++ language progress and only risks IDF-side build-script
behavior changes.

## The real C++20 path: Arduino-esp32 3.x via pioarduino

C++20 (`<ranges>`, `<span>`, concepts) becomes available with **Arduino-esp32
v3.0.0+**, which is built on **IDF v5.1.x and ships gcc 13** (espressif
crosstool-NG#48 confirms ranges land in the 3.0.0 / IDF 5.1 line). Since the
official PlatformIO platform froze Arduino at 2.0.17, the maintained way to get
Arduino 3.x under PlatformIO is the community fork:

```ini
; pioarduino fork — Arduino core 3.3.9 / IDF 5.5.4 / xtensa gcc 13
platform = https://github.com/pioarduino/platform-espressif32/releases/download/55.03.39/platform-espressif32.zip
```

(`pioarduino/platform-espressif32`, maintainer Jason2866, GPG-signed; tag
`55.03.39` = Arduino 3.3.9, IDF 5.5.4, latest as of this spike.)

This matches what `docs/cpp-standard-spike.md` already predicted: the unblock is
"a platform whose bundled xtensa gcc is >= 10 (gcc 13 ships with the ESP-IDF
5.x / Arduino core 3.1.x line)… e.g. a pioarduino release."

## Blast radius: library + board re-validation (the hard part)

Moving to Arduino-esp32 3.x is a **major-core migration**, not a flag flip. The
`lib_deps` audit surfaces two confirmed blockers and several to-verify items:

| Dependency (current pin) | Verdict on Arduino-esp32 3.x | Notes |
|---|---|---|
| **`h2zero/NimBLE-Arduino@^1.4.0`** | ❌ **BLOCKER — must move to NimBLE-Arduino 2.x** | NimBLE-Arduino 1.x is incompatible with core 3.x / IDF 5.1 (NimBLE-Arduino#641, breaking API changes). 2.x has a different API surface → GaggiMate's entire BLE stack (scale comms, controller link via `NimBLEComm`, `esp-arduino-ble-scales`) needs migration + re-test. **Largest single risk.** |
| **`bodmer/TFT_eSPI @ 2.5.43`** | ❌ **BLOCKER (display env) — broken on core 3.x** | Documented `REG_SPI_BASE` boot crash on ESP32-S3 with Arduino 3.x; community guidance is "TFT_eSPI does not work with Espressif Arduino 3.x yet" (TFT_eSPI#3329). Affects the `display`/`LilyGo-T-RGB` build's panel driver. Needs a patched TFT_eSPI or driver swap. |
| `lvgl/lvgl @ 8.4.0` | ⚠️ Verify | LVGL 8.x is core-agnostic C; main risk is the TFT_eSPI binding above, not LVGL itself. |
| `esp-arduino-ble-scales` (`#5f8f2cd`) | ⚠️ Verify | Wraps NimBLE — its compatibility is gated by the NimBLE 1.x→2.x migration. |
| `homespan/HomeSpan@1.9.1` | ⚠️ Verify | HomeSpan historically tracks core changes; confirm a 3.x-compatible release. |
| AsyncTCP `#v3.4.9` / ESPAsyncWebServer `#v3.9.1` (ESP32Async) | ✅ Likely OK | The ESP32Async forks explicitly support core 3.x. |
| `bblanchon/ArduinoJson@^7.2.1`, `256dpi/MQTT`, `links2004/WebSockets`, `FS`/`SPIFFS`/`Wire`/`SPI` | ✅ Likely OK | Header-only / framework libs; low risk. |

**Boards:** the three custom boards (`LilyGo-T-RGB`, `Gaggimate-Controller`,
`esp32-s3-supermini`) are **local self-contained JSON** under `boards/`. They
pin no core version — they only require the platform to provide the `esp32`
Arduino core. They should resolve under any Arduino-3.x platform; low risk
(re-verify `memory_type`/`psram_type` keys, which 3.x still honors).

**Build flags / filesystem:** GaggiMate uses **SPIFFS** (`lib_deps` + `data/w/`
SPIFFS image via `scripts/build_spiffs.sh`). Core 3.x still ships SPIFFS but
upstream nudges toward LittleFS; the SPIFFS partition + build pipeline should
keep working but must be re-tested. Several `-DCONFIG_*` async-TCP/NimBLE flags
and `-DARDUINO_USB_CDC_ON_BOOT` are core-version-sensitive and need a re-check
under 3.x.

**Size delta:** not measured — measuring it requires actually installing the
pioarduino platform and resolving the (currently broken) NimBLE/TFT_eSPI deps,
which is out of scope for a research spike and would be the first task of the
real migration.

## Coordination with CAR-340 (C++20 effort)

- CAR-340 slice 1 (`docs/cpp-standard-spike.md`) correctly identified the
  prerequisite as a platform/core bump to gcc ≥ 10. **This spike confirms that
  prerequisite is the pioarduino Arduino-3.x platform, and disproves the
  assumption that the official 7.x line satisfies it.** The `platformio.ini`
  comments referencing "a platform/core bump first" remain accurate.
- The C++20 flag flip (`gnu++17` → `gnu++20`) must be a **stacked follow-up**
  that lands in the SAME effort as, but logically AFTER, the platform migration
  + library fallout is green. Doing the flag change in the platform-bump PR is
  fine *only once* NimBLE 2.x and TFT_eSPI are resolved and the matrix builds.

## Recommendation: go / no-go + sequencing

1. **NO-GO on official `espressif32@7.x` as a C++20 enabler.** It does not
   advance the Arduino toolchain; adopting it for C++20 is based on a false
   premise. (A bump to 6.13.0 for IDF security/bugfixes is a separate, low-value
   question — still gcc 8.4.0 for Arduino — and not what this issue is about.)
2. **The real change is a pioarduino Arduino-3.x platform migration.** Treat it
   as its own multi-step effort (own issue / CAR-340 later slice), sequenced:
   1. Migrate **NimBLE-Arduino 1.x → 2.x** (API rewrite + BLE re-test) — biggest
      risk, do it first / in isolation.
   2. Resolve **TFT_eSPI on core 3.x** (patched fork or driver swap) for the
      display env.
   3. Re-validate remaining `lib_deps` + the SPIFFS pipeline + custom boards.
   4. Switch `platform =` to the pioarduino release; get the full env matrix
      (`display`, `display-headless`, `controller`) green; measure flash/RAM
      delta.
   5. **Then** flip `-std=gnu++17` → `-std=gnu++20` (the CAR-340 payoff) and
      re-run the matrix.
3. **Until that is scheduled, stay on `espressif32@6.12.0` / `gnu++17`.** No
   change to `platformio.ini` from this spike.

**Effort / risk estimate:** Large. The NimBLE 1.x→2.x migration alone is a
meaningful sub-project (touches all BLE comms), and TFT_eSPI on core 3.x is an
unsolved-upstream pain point. This is a high-blast-radius change that should be
its own tracked branch with the full CI matrix, not folded into unrelated work.

## References

- `platformio/platform-espressif32` releases (7.0.0 / 7.0.1 ship Arduino 2.0.17).
- platform-espressif32#1225 — official platform discontinued advancing Arduino past 2.x.
- `pioarduino/platform-espressif32` releases — Arduino 3.3.9 / IDF 5.5.4 / gcc 13 (tag 55.03.39).
- espressif/crosstool-NG#48 — C++20 `<ranges>` available from Arduino-esp32 3.0.0 / IDF 5.1.
- h2zero/NimBLE-Arduino#641 — NimBLE-Arduino 1.x incompatible with core 3.x / IDF 5.1.
- Bodmer/TFT_eSPI#3329 — TFT_eSPI broken on Arduino-esp32 > 2.0.14 / 3.x (S3 boot crash).
- `docs/cpp-standard-spike.md` — CAR-340 slice 1, the upstream blocker analysis.

Ref CAR-340
