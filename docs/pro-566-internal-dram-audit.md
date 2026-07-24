# PRO-566 — Internal-DRAM consumer audit (display board, OTA 48 KiB floor)

**Type:** Spike (investigation) · **Status:** findings + recommendations, on-device
measurement deferred to Carlos · **Branch:** `pro-566-dram-audit-spike`

## TL;DR

- The device is on **Arduino-ESP 2.0.17 / ESP-IDF 4.4.7** (`platform = espressif32@6.12.0`,
  NimBLE-Arduino **1.4.3**, gcc 8.4.0 / gnu++17). The PRO-358/364 numbers were measured on
  **IDF 5.x** and do **not** transfer — re-measured/re-derived from scratch here.
- The single biggest and cheapest lever is **NimBLE host allocations → PSRAM**. On *this*
  platform (NimBLE-Arduino library path, not the IDF `esp-nimble-cpp` component) it is a
  one-line **build flag**, `-DCONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL=1` — **not** the
  `custom_sdkconfig` mechanism PRO-358 used, which **does not exist on this platform**.
- **`custom_sdkconfig` is unavailable here.** The classic `espressif32@6.12.0` Arduino build
  links prebuilt IDF libraries and never regenerates them from source, so all the PRO-358/364
  `custom_sdkconfig`-based fixes (NimBLE, WiFi buffer trims, mbedTLS caps) must be re-expressed
  as app-level `-D` build flags (which work for the NimBLE-Arduino path) or dropped.
- Instrumentation to *measure* the per-subsystem trajectory on hardware is committed on this
  branch behind `-DGM_HEAP_DIAG_ENABLED=1` (`[env:display-heapdiag]`), zero-cost in production.
  **The absolute KB numbers below are ESTIMATED from static code + sdkconfig reads and must be
  confirmed by flashing `display-heapdiag` and capturing serial** (Carlos's manual step).

---

## 1. Platform reconciliation (acceptance criterion 6)

Per `git-ancestry-vs-platform-rollback-pitfall.md`, ancestry ≠ still-in-effect. Verified against
the *actual current tree*, not commit history:

| Fact | Source of truth | Value |
|---|---|---|
| Platform | `platformio.ini` L12 | `espressif32@6.12.0` |
| Arduino core | `framework-arduinoespressif32/package.json` | `3.20017…` = **2.0.17** |
| ESP-IDF | `.../tools/sdk/esp32s3/include/.../esp_idf_version.h` | **4.4.7** |
| NimBLE | `platformio.ini` L62 / installed `library.properties` | NimBLE-Arduino **1.4.3** |
| C++ std | `[display_common]` | gnu++17 (gcc 8.4.0) |
| `custom_sdkconfig` in tree | `grep custom_sdkconfig platformio.ini` | **none** (reverted with `ea232239`) |

The `ea232239` rollback reverted the entire IDF-5.x/pioarduino stack (and PRO-358's
`custom_sdkconfig` NimBLE→PSRAM fix + `scripts/nanopb_idf_include.py`). Boot serial the issue
cites (`2.0.15-396-ga8fe7c67`) matches this 2.x lineage. **IDF 4.4 has a materially different
internal-DRAM budget than IDF 5.x**, and — importantly — its prebuilt esp32s3 sdkconfig sets
`CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y` and `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096`, so a lot
of WiFi/lwIP already lands in PSRAM automatically. This likely makes WiFi/lwIP a *smaller*
internal consumer than the ~58 KB PRO-358 saw on IDF 5.x — re-measure before trusting old numbers.

Static internal-DRAM baseline from the linker (baseline `[env:display]` build):
`RAM: 24.2% (used 79396 bytes from 327680 bytes)` — i.e. ~79.4 KB of .data/.bss is statically
committed; the remaining ~248 KB internal DRAM is the runtime heap that WiFi + BLE controller +
FreeRTOS task stacks + mbedTLS + lwIP draw from. The 48 KiB floor is a *largest-contiguous-block*
gate on that runtime heap, so fragmentation — not just total free — is what matters.

---

## 2. Consumer inventory (acceptance criteria 2 & 3)

Legend: **Steady** = permanently held while the feature is up. **Transient** = allocated then
freed. **PSRAM-routable** = the allocation does **not** need DMA-capable internal DRAM and can be
moved to the 8 MB PSRAM. All KB figures are **ESTIMATED (static analysis), pending on-device
verification** unless marked *measured*.

FreeRTOS note: on ESP-IDF `xTaskCreate` stack depth is in **bytes** (not words like vanilla
FreeRTOS). Task stacks always come from **internal** DRAM. `configMINIMAL_STACK_SIZE = 768 +
configSTACK_OVERHEAD_TOTAL` — the overhead depends on build options (stack-check / apptrace /
watchpoint), so the `MIN×N` sums below are lower-bounded at 768 B/unit and flagged for on-device
confirmation.

| # | Consumer | Est. internal DRAM | Steady/Transient | PSRAM-routable? | Notes |
|---|---|---|---|---|---|
| 1 | **NimBLE host mempools (msys mbufs, ACL bufs, GATT)** via `NimBLEDevice::init()` | ~30–70 KB (measure) | Steady | **YES** (build flag) | `esp_nimble_mem.c`: `_INTERNAL` → `MALLOC_CAP_INTERNAL`; `_EXTERNAL` → `MALLOC_CAP_SPIRAM`. Selected by app `#define` in NimBLE-Arduino 1.4.3 (`nimconfig.h` L177). PRO-358 measured ~68 KB on IDF 5.x — re-measure. Single BLE stack (`NimBLEDevice`) shared by client scan + the peripheral GATT service. |
| 2 | **BT controller (`libbt.a` link-layer)** | ~20–40 KB (measure) | Steady | **NO** (prebuilt lib, needs `custom_sdkconfig` which is unavailable) | Distinct from the host mempools above. Cannot be moved without the from-source rebuild lever this platform lacks. Effectively a floor we can't easily lower. |
| 3 | **DiagnosticLogPlugin** (`DiagLogUDP` task + queue) | **~32.7 KB** *(near-exact from code)* | Steady *(while enabled)* | **YES** (both) | Queue `64 × sizeof(DiagLogLine≈260 B)` ≈ 16.6 KB + drain-task stack `16384 B`. **Default OFF** but was observed **ON** on Carlos's device ("UDP log tee armed"). No free/uninstall path once armed. Neither the queue storage nor the drain-task stack needs DMA memory. |
| 4 | **WiFi + lwIP** (`setupWifi()`) | ~10–40 KB (measure — likely << IDF-5.x's 58 KB) | Steady | Partly already PSRAM | IDF 4.4 sdkconfig: `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y`, `SPIRAM_MALLOC_ALWAYSINTERNAL=4096` → allocs ≥4 KB already prefer PSRAM. Buffer-count trims are *unavailable* here (no `custom_sdkconfig`), and PRO-364 proved count-trims don't grow the contiguous block anyway. |
| 5 | **mbedTLS SSL I/O buffers** (OTA-check TLS handshake) | 2 × 16.4 KB **transient peak** | Transient (per handshake) | contiguous internal required | `CONFIG_MBEDTLS_SSL_MAX_CONTENT_LEN=16384`, `CONFIG_MBEDTLS_ASYMMETRIC_CONTENT_LEN` **not set** in the prebuilt esp32s3 sdkconfig → in/out record buffers are 16.4 KB each, exactly PRO-364's finding. **This is the allocation the 48 KiB floor exists to protect.** The `-DCONFIG_MBEDTLS_SSL_IN/OUT_CONTENT_LEN=4096` caps used elsewhere are INERT on the prebuilt lib (no `custom_sdkconfig` to make them live). |
| 6 | **WebUIRelay task** (`WebUIPlugin`) | 16 KB | Steady | YES (stack) | `xTaskCreatePinnedToCore(relayLoopTask,"WebUIRelay",16384,…)`. Flat 16 KB stack. |
| 7 | **async_tcp task** (AsyncTCP) | 8 KB + queue | Steady | stack YES | `CONFIG_ASYNC_TCP_STACK_SIZE=8192`, `CONFIG_ASYNC_TCP_QUEUE_SIZE=64`. Per-WS-client buffers (`DEFAULT_MAX_WS_CLIENTS=4`) add up under multi-tab load. |
| 8 | **Firmware task stacks** (steady) | ~25–35 KB aggregate (measure) | Steady | stacks are internal-only | Controller loopControl `MIN×6`, DefaultUI loop `MIN×6` + loopProfiles `MIN×4`, Settings loop `MIN×6`, ShotHistory loop `MIN×6`, NimBLE client loop `MIN×4`, NimBLE server loop `MIN×4`. These are load-bearing; not a refactor target. |
| 9 | **HomeSpan / HomeKit** (`homeSpan.begin()`) | ~0 unless runtime-ON | Steady *(if on)* | n/a | **Double-gated**: compile `GAGGIMATE_ENABLE_HOMEKIT=1` AND runtime `settings.isHomekit()` (default false). `HomekitPlugin` is only *registered* when `isHomekit()` is true (else `mDNSPlugin`). PRO-364 measured near-zero when runtime-off. **Confirm Carlos's device state before treating as a suspect** (Trap 6). |
| 10 | **MQTT** (`MQTTPlugin`, 256dpi/MQTT) | ~1–2 KB | Steady *(if HA on)* | small | `MQTTClient` default lwmqtt buffers (~128 B r/w, `setBufferSize` not called). Registered only if `settings.isHomeAssistant()`. Negligible. |
| — | Transient (not steady-state): ShotHistory rebuild task `MIN×8` (self-deletes), `OtaResolve` task 8192 B (per channel-switch click), ShotHistory file-op task | varies | Transient | — | Not steady-state floor contributors; only spike during their operation. |

### What NOT to touch (acceptance criterion 4)

- **LVGL / display framebuffers — already PSRAM, CONFIRMED.** `LV_Helper.cpp` L114/L118:
  both `buf` and `buf1` via `ps_malloc`. `lv_conf.h` uses the LVGL custom allocator. Not an
  internal-DRAM consumer. Do not spend budget here.
- **Do NOT disable HomeKit or MQTT as a "fix"** without first confirming via the bracketed
  measurement that their heavy init actually runs on this device — both are runtime-gated and
  likely consuming ~nothing (Trap 6). Disabling a feature that isn't running frees nothing.
- **BT controller `libbt.a`** — cannot be moved without `custom_sdkconfig` (unavailable); don't
  chase it on this platform.
- **WiFi/lwIP buffer-count trims** — the lever (`custom_sdkconfig`) is unavailable, AND PRO-364
  proved count-trims move total-free but not the contiguous block. Dead end here.

---

## 3. Top-3 refactor recommendations (acceptance criterion 3)

### R1 — Route NimBLE host allocations to PSRAM (build flag) — **highest value, lowest risk**

- **How:** add `-DCONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL=1` to `[display_common].build_flags`
  (applies to `display` + all derived envs). Do **not** also define `_INTERNAL` — nimconfig.h
  defaults `_INTERNAL=1` only when `_EXTERNAL` is undefined, and `esp_nimble_mem.c` checks
  `_INTERNAL` first, so setting only `_EXTERNAL` correctly flips the msys/mbuf pools to
  `MALLOC_CAP_SPIRAM`.
- **Why it's cheap here (vs PRO-358):** this is the **NimBLE-Arduino library** path — the sources
  are compiled *with* the app's `nimconfig.h`, so the app `-D` is honored. This is NOT the
  IDF-5.x/`esp-nimble-cpp`-managed-component path where the flag was a no-op and `custom_sdkconfig`
  was required. None of the PRO-358 `custom_sdkconfig` cascade traps (nanopb break, esp-modbus
  static-assert, Component-Registry hang, from-source 15-min build) apply.
- **Est. recovered:** likely tens of KB of the largest-block (PRO-358 got 3 KB→21 KB on IDF 5.x by
  moving ~68 KB). **Must be measured on IDF 4.4** — the NimBLE host mempool footprint differs.
- **Risk/effort:** LOW / ~1 line. Verified in PRO-358 that BLE runs fine on the PSRAM allocator
  (scale scanning worked, no crash). PSRAM is not DMA-capable but NimBLE host mbufs don't need DMA.
  On-device gate: BLE still scans + connects to the controller, no panic.

### R2 — Move DiagnosticLogPlugin queue + drain-task stack to PSRAM — **~32 KB, low risk**

- **How:** allocate the queue with `xQueueCreateStatic` backed by a `heap_caps_malloc(…,
  MALLOC_CAP_SPIRAM)` storage buffer, and create the drain task with `xTaskCreateStaticPinnedToCore`
  using a PSRAM-backed stack buffer (FreeRTOS static creation lets you place both in PSRAM; neither
  needs DMA memory). Alternatively, and simpler: this is a **debug-only** feature Carlos doesn't need
  steady-state — ensure it stays **default OFF** and is turned off on his device, which frees the
  full ~32.7 KB immediately with zero code change.
- **Est. recovered:** ~32.7 KB internal when enabled → PSRAM (or freed entirely if left OFF).
- **Risk/effort:** LOW-MEDIUM / small. Static FreeRTOS creation is well-trodden; the only care is
  the drain task does UDP/SD I/O from a PSRAM stack (fine — no ISR context, no DMA on the stack).

### R3 — Make the mbedTLS 4 KB SSL content-length caps actually live — **directly unblocks the OTA floor**

- **Problem:** the OTA check's TLS handshake needs a **contiguous** internal block for two 16.4 KB
  record buffers (`CONFIG_MBEDTLS_SSL_MAX_CONTENT_LEN=16384`, `ASYMMETRIC_CONTENT_LEN` unset). This
  is *the* allocation the 48 KiB floor guards. Capping each to 4 KB (PRO-364's fix) is the most
  surgical unlock.
- **Blocker on this platform:** the caps live in the **prebuilt** mbedtls lib; app `-D` flags are
  inert and `custom_sdkconfig` (the PRO-364 lever) is **unavailable** on classic espressif32.
  Options to evaluate (needs a spike of its own): (a) a project `sdkconfig.defaults` +
  a `build_type`/menuconfig path if this platform honors one; (b) route esp-tls/mbedtls working
  buffers to PSRAM via a `mbedtls_platform_set_calloc_free` hook installed at boot; (c) reduce the
  *transient* contiguous need by pre-allocating/reusing a single TLS buffer. **Do not assume the
  4 KB cap is reachable the same way as on IDF 5.x** — measure the failing alloc first (PRO-364
  `-DGM_TLS_ALLOC_PROBE` recipe) on this platform.
- **Est. recovered:** each buffer 16.4 KB → 4.4 KB if the cap can be made live; would very likely
  clear the 48 KiB floor once R1 frees the contiguous space. **Feasibility unconfirmed on this
  platform** — that's why it's R3, gated behind measurement.

**Recommended order:** R1 (flag) → measure → R2 (or just keep DiagLog off) → measure → only then
R3 if the floor still doesn't clear. Do not blind-lower `kOtaResolveInternalDramFloorBytes` — the
floor protects a real contiguous TLS allocation (PRO-364 proved lowering it just moves the OOM into
mbedtls).

---

## 4. Instrumentation delivered (acceptance criterion 1)

Committed on `pro-566-dram-audit-spike`, all behind `-DGM_HEAP_DIAG_ENABLED=1`
(`[env:display-heapdiag]`; NOT built by CI; default `[env:display]` unchanged, +8 B static):

- `src/display/core/GmHeapDiag.h` — `GM_HEAP_DIAG(tag)` + `GM_HEAP_DIAG_INFO()` macros.
- Boot brackets in `Controller::setup()` / `connect()` / `setupBluetooth()`: `setup() begin`,
  `after setupPanel`, `after pluginManager->setup`, `after ui->init`, `setup() end`,
  `connect() begin`, `after setupWifi`, `before/after clientController.initClient`,
  `after setupBluetooth`, `connect() end`.
- `mDNSPlugin`: `before/after MDNS.begin`.
- `DiagnosticLogPlugin`: `before/after DiagLog install (queue+task)`.
- `WebUIPlugin`: `before/after webserver+relay start`.
- `Controller::loop()`: 5 s steady-state internal-DRAM sampler + one-shot
  `heap_caps_print_heap_info` (exhaustion vs fragmentation).

### Carlos's manual measurement step (deferred — no hardware access in this env)

```bash
cd /home/carlos/gaggimate-pro566
export NODE_ENV=development
# (web bundle already packed; rebuild if stale: bash scripts/build_webui.sh)
pio run -e display-heapdiag -t upload --upload-port /dev/ttyACM0
# capture ~30 s of boot serial (DTR/RTS reset recipe from the flashing skill), then:
#   grep GmHeapDiag /tmp/boot.log
```

Read the per-tag `largest_block` trajectory to confirm which subsystem collapses the contiguous
block below 49152, and the `steady-state loop` line for the resting floor. Compare against the
estimates in §2. Then apply R1, rebuild `display-heapdiag`, re-capture, and confirm the largest
block after `after clientController.initClient` no longer collapses.

---

## 5. Follow-ups filed (acceptance criterion 5)

Filed as separate PRO issues (each `Ref PRO-566`):

- **PRO-567** — R1: Route NimBLE host allocations to PSRAM via `CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL` build flag.
- **PRO-568** — R2: Move DiagnosticLogPlugin queue + drain-task stack off internal DRAM (PSRAM) or keep default OFF.
- **PRO-569** — R3: Investigate making mbedTLS SSL content-length caps live on classic espressif32 (no `custom_sdkconfig`).

This spike delivers findings + instrumentation only; the fixes are separate PRs.
