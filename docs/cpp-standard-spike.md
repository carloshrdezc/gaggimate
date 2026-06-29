# C++ standard modernization spike — CAR-340 slice 1

**Status: SUPERSEDED — C++20 is now ENABLED (see UPDATE below). Original slice-1
finding: the C++20 bump was BLOCKED on the then-pinned toolchain and the firmware
stayed on `gnu++17`.**

> **UPDATE (PRO-294, post-PRO-293):** RESOLVED. PRO-293 landed the pioarduino
> Arduino-esp32 3.x platform (release 55.03.39, xtensa gcc 14.2.0), satisfying
> the "what would unblock C++20" prerequisite below. PRO-294 (step 6 of PRO-288)
> flipped the firmware + native envs from `-std=gnu++17` to `-std=gnu++20`
> (`c++20`/`gnu++20`) and greened the full CI matrix. C++20 is now **ENABLED**.
> The blocked analysis below is preserved as the slice-1 record.

This document records the result of the toolchain spike that gates the rest of
CAR-340 ("Modernize firmware to C++20/23 + RAII", a multi-PR effort). Slice 1
asked: *confirm whether the `-std=gnu++17` build flag can move to
`gnu++20`/`c++20` — bump if clean, document if blocked.*

## What was checked

The firmware environments in `platformio.ini` (`[display_common]` and
`[env:controller]`) set the C++ standard via:

```ini
-std=c++17
-std=gnu++17   ; the later flag wins -> effective standard is gnu++17
```

The `display` board (`LilyGo-T-RGB`) is an ESP32-S3, so it builds with the
`toolchain-xtensa-esp32s3` package bundled by `platform = espressif32@6.12.0`.

| Component | Version (as pinned/installed) |
|---|---|
| PlatformIO Core | 6.1.19 |
| `platform` | `espressif32@6.12.0` |
| Arduino core (`framework-arduinoespressif32`) | `3.20017.241212+sha.dcc1105b` |
| Xtensa cross-compiler | `xtensa-esp32s3-elf-g++ 8.4.0` (crosstool-NG esp-2021r2-patch5) |

## Why it is blocked

`espressif32@6.12.0` ships the **esp-2021r2-era xtensa toolchain, gcc 8.4.0**.
GCC 8 predates C++20 and only knows the experimental pre-standardization
`*2a` dialect spelling. Empirically, against the bundled
`xtensa-esp32s3-elf-g++ 8.4.0`:

```text
-std=gnu++17   OK
-std=gnu++2a   OK   (driver accepts it, but it is only a partial C++20 stub)
-std=gnu++20   FAIL: error: unrecognized command line option '-std=gnu++20';
                     did you mean '-std=gnu++2a'?
-std=c++20     FAIL: error: unrecognized command line option '-std=c++20';
                     did you mean '-std=c++2a'?
```

`gnu++2a` is **not** a usable C++20 — the standard-library and language
support that CAR-340's later slices depend on is absent in gcc 8:

```text
#include <span>       FAIL: fatal error: span: No such file or directory
#include <ranges>     FAIL: fatal error: ranges: No such file or directory
#include <compare>    FAIL: fatal error: compare: No such file or directory  (no <=>)
concept C = true;     FAIL: 'concept' does not name a type
```

So both the canonical `gnu++20` spelling *and* the substance of C++20
(`<span>`, `<ranges>`, concepts, three-way comparison) are unavailable. Forcing
`gnu++2a` would buy nothing for the planned RAII/`std::span`/`std::optional`/
`enum class` work and would risk subtle partial-feature breakage. Per the issue
instruction ("DO NOT force it"), the flag is left at `gnu++17`.

## What would unblock C++20

The blocker is purely the toolchain version, not the codebase. To move to
C++20 the project must bump the platform pin to one whose bundled xtensa gcc is
**>= 10** (gcc 13 ships with the ESP-IDF 5.x / Arduino core 3.1.x line). The
required follow-up is a separate, riskier change because it moves the whole
ESP-IDF/Arduino core, not just a compiler flag:

1. Bump `platform = espressif32@6.12.0` to a release that bundles an xtensa
   gcc >= 10 (gcc 13 preferred), e.g. a `pioarduino`
   `platform-espressif32` 53.x release (Arduino core 3.1.x / IDF 5.3).
2. Re-validate every firmware library pin in `lib_deps` against the new core
   (NimBLE, AsyncTCP/ESPAsyncWebServer, TFT_eSPI, LVGL, GFX, HomeSpan, etc.).
3. *Then* change `-std=gnu++17` -> `-std=gnu++20` and re-run the build matrix.

That platform/core bump is out of scope for this slice and should be its own
CAR ticket (or an explicit later slice of CAR-340) because of the library
fallout it can cause.

## Decision for slice 1

- Keep `-std=gnu++17` on the firmware envs. **No flag change.**
- Record the blocker (this document) so subsequent CAR-340 slices that assume
  C++20 know the prerequisite is a platform/toolchain bump, not a one-line flag
  edit.

## Verification

`pio run -e display` builds clean on the unchanged `gnu++17` baseline (no
regression). See the PR description for the real build-output tail.
