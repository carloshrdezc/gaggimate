# Spike: NimBLE-Arduino 1.4.3 → 2.x migration findings (PRO-255)

## Summary / Recommendation (go / no-go)

**Conditional GO — but NOT as a standalone dependency bump. NimBLE 2.x is
coupled to the Arduino-esp32 core 2.x → 3.x platform migration and must be
slated as the first slice of that effort, not shipped on its own against the
current `espressif32@6.12.0` platform.**

The BLE API churn itself is *well-bounded and low-to-medium risk*: every
first-party NimBLE call site lives in three firmware libs (`lib/NimBLEComm`,
`lib/ble_ota_dfu`, `lib/OTA`), the application/plugin/UI layers never call
NimBLE directly, and the changes are mostly mechanical signature edits plus a
handful of renames. The single open gating unknown from the prior spike — *does
the vendored scales lib have a NimBLE-2.x-compatible revision?* — is now
**RESOLVED**: `gaggimate/esp-arduino-ble-scales` already ships a `v2.0.0` tag
and a `main` branch fully ported to NimBLE 2.5.0 (see §3). So the scales lib is
no longer a blocker; it is a pin bump to an existing upstream revision.

The reason this is *conditional* rather than an unqualified GO: NimBLE-Arduino
1.x is incompatible with Arduino-esp32 core 3.x / IDF 5.1 (upstream
[NimBLE-Arduino#641]), and the maintainers' own v2 scales line is built for the
**pioarduino / IDF-managed-component** world (it deliberately drops the bundled
`NimBLE-Arduino` dep and pulls headers from the `esp-nimble-cpp` IDF component —
scales-lib commit `130942f`). The firmware is currently on
`platform = espressif32@6.12.0` (Arduino core **2.0.17**, xtensa **gcc 8.4.0** —
`platformio.ini:12`, and the standing comment at `platformio.ini:26-29`). NimBLE
2.x is the *enabling* first step of the platform bump (`docs/espressif32-7x-platform-bump-spike.md`
calls it "the largest single risk"), and the platform bump is what NimBLE 2.x
is *for*. Doing them as one tracked effort (NimBLE 2.x first, in isolation on a
branch, then the core bump) is the right sequencing.

**This doc supersedes the two earlier spikes for the go/no-go decision** by (a)
closing their one open unknown and (b) correcting one staging claim in
`docs/nimble-2x-migration-spike.md` (PRO-289) — see §6. It does not re-derive the
full call-site inventory those docs already contain; it cross-references them and
adds only the new, verified evidence.

### Prior art (read these too)
- `docs/nimble-2x-migration-spike.md` (PRO-289, PR #269) — exhaustive first-party
  call-site inventory + the A/B/C API-delta breakdown. Verified accurate against
  the current tree while writing this doc.
- `docs/espressif32-7x-platform-bump-spike.md` (PRO-257, PR #268) — establishes
  that the real C++20 / core-3.x enabler is the **pioarduino** Arduino-3.x
  platform, and lists NimBLE 1.x→2.x as blocker #1 of that migration.

---

## 1. Breaking changes that affect us (v1 API → v2 API → our call sites)

Scope rule: only API surface GaggiMate **actually uses** is listed. Source for
the v2 column: upstream `h2zero/NimBLE-Arduino` migration guide
(`docs/Migration_guide.md`, master) + the 2.x `NimBLEScan`/callback class docs,
cross-checked against the already-completed port in the scales lib's `v2.0.0`
tag (`98d45b8`), which is the most concrete proof of the exact deltas.

| v1 API (1.4.3) | v2 API (2.5.0) | Our call site(s) |
|---|---|---|
| `class : public NimBLEAdvertisedDeviceCallbacks` | **`NimBLEScanCallbacks`** (old class removed) | `NimBLEClientController.h:7` (inherits it) |
| `onResult(NimBLEAdvertisedDevice*)` | **`onResult(const NimBLEAdvertisedDevice*)`** | `NimBLEClientController.cpp:367`, decl `.h:83` |
| `scanner->setAdvertisedDeviceCallbacks(this, true)` | **`scanner->setScanCallbacks(this, true)`** | `NimBLEClientController.cpp:34` |
| `scanner->start(0, nullptr, false)` (3-arg: duration, cb, is_continue) | **`start(0, false)`** (2-arg: duration ms, `bool restart`) | `NimBLEClientController.cpp:40` |
| `scanner->clearDuplicateCache()` | **removed** (call `stop()`/`start(restart=true)` instead) | `NimBLEClientController.cpp:33` — must drop/replace |
| `NimBLEClientCallbacks::onDisconnect(NimBLEClient*)` | **`onDisconnect(NimBLEClient*, int reason)`** | `NimBLEClientController.cpp:384`, decl `.h:86` |
| `client->setClientCallbacks(this)` | unchanged (2nd `deleteCallbacks` arg still optional) | `NimBLEClientController.cpp:24` ✅ |
| `NimBLEServerCallbacks::onConnect(NimBLEServer*)` | **`onConnect(NimBLEServer*, NimBLEConnInfo&)`** | `NimBLEServerController.cpp:258`, decl `.h:69` |
| `NimBLEServerCallbacks::onDisconnect(NimBLEServer*)` | **`onDisconnect(NimBLEServer*, NimBLEConnInfo&, int reason)`** | `NimBLEServerController.cpp:264`, decl `.h:70` |
| `NimBLECharacteristicCallbacks::onWrite(NimBLECharacteristic*)` | **`onWrite(NimBLECharacteristic*, NimBLEConnInfo&)`** | `NimBLEServerController.cpp:270`, decl `.h:73` |
| `pServer->startAdvertising()` in `onDisconnect` | works, **but advertising no longer auto-restarts** in 2.x — the manual restart at `:267` becomes load-bearing (keep it) OR call `NimBLEServer::advertiseOnDisconnect(true)` | `NimBLEServerController.cpp:267` |
| notify subscribe callback `(NimBLERemoteCharacteristic*, uint8_t*, size_t, bool)` | **same 4-arg shape** in 2.x `notify_callback` typedef — `std::bind(...)` subscriptions need no body change beyond the const-correctness of args | `NimBLEClientController.cpp:136-175` (7 subscribe sites), `notifyCallback` decl `.h:89` |
| `NIMBLE_PROPERTY::WRITE/NOTIFY/READ` | **unchanged** ✅ | `NimBLEServerController.cpp:26-75` (all `createCharacteristic`) |
| `char->setValue(buf,len)` / `->notify()` / `->writeValue(buf,len,false)` | **unchanged** ✅ (`notify()` just no longer takes the `bool is_notification` arg, which we don't pass) | server `sendXxx` (`.cpp:114-221`), client `sendXxx` (`.cpp:55-359`) |
| `pCharacteristic->getValue()` → `NimBLEAttValue` | unchanged shape; `.data()/.size()/[0]` all still work | `NimBLEServerController.cpp:276,311,325,...` (`onWrite` decode) ✅ |
| `NimBLEUUID(...)`, `.equals(...)`, `getUUID()` | unchanged ✅ | throughout both wrappers |
| `NimBLEDevice::init/setMTU/createClient/getScan/createServer/getAdvertising` | unchanged ✅ | `initClient` (`.cpp:15-19`), `initServer` (`.cpp:14-82`) |
| `NimBLEDevice::setPower(ESP_PWR_LVL_P9)` | **`setPower(int dBm)`** — `esp_power_level_t` enum no longer accepted; pass integer dBm (`9`) | `NimBLEClientController.cpp:16`, `NimBLEServerController.cpp:15` |
| `client->updateConnParams(6,8,0,400)` / `setMTU(128)` | exist; **verify default-timeout shifts** in 2.x (behavior, not signature) | `NimBLEClientController.cpp:108,17` |
| `BLE2902` / `NimBLE2902` descriptor | **removed** — 2.x auto-creates 0x2902 CCCD for NOTIFY/INDICATE chars | **no-op for us** — we never create one (verified); do NOT add one during the port |

Out of this repo's scope but on the same migration (covered by PRO-289):
`lib/ble_ota_dfu` (legacy `BLE*` aliases, `onWrite`/`onNotify` + `NimBLEConnInfo&`,
`WRITE_ENC` security path) and `lib/OTA/ControllerOTA`/`GitHubOTA` (client-side
notify callback, same shape). The desktop sim (`sim/comms/*`) uses a hand-written
stub and is unaffected.

---

## 2. Effort: mechanical vs. behavioral

- **Mechanical (low risk, compiler-caught):** the callback-signature edits
  (`NimBLEConnInfo&` / `int reason` params), the `NimBLEAdvertisedDeviceCallbacks`
  → `NimBLEScanCallbacks` swap, `setAdvertisedDeviceCallbacks` →
  `setScanCallbacks`, `start()` arg change, `clearDuplicateCache()` removal,
  `setPower(int)`. These fail to compile if missed — no silent runtime hazard.
- **Behavioral (medium risk, hardware-verify):** advertising no-auto-restart on
  disconnect (`NimBLEServerController.cpp:264-267` reconnect path); connection
  param / MTU default shifts; the `WRITE_ENC` OTA security path (`ble_ota_dfu`).
  These compile fine but can perturb the link — must be smoke-tested on real
  hardware (display↔controller link across a brew cycle, BLE-OTA upload, real
  scale pairs + streams weight).

---

## 3. Scales-lib (`esp-arduino-ble-scales`) compatibility verdict — RESOLVED

**Verdict: a NimBLE-2.x-compatible revision already exists upstream; this is a
pin bump, not a fork-and-patch.** The prior spike (PRO-289 §C-11) left this as
"the single biggest scheduling unknown." It is now closed.

`git ls-remote https://github.com/gaggimate/esp-arduino-ble-scales` shows:
- current pin `#5f8f2cd` == tag **`v1`** (the 1.4.x-era line we're on).
- tag **`v2.0.0` = `98d45b8`**, and **`main` = `180070f`**, both ported to NimBLE
  **2.5.0** (commit `135572a` "upgrade NimBLE-Arduino from 1.4.x to 2.5.0").
- branch `v1.x` (`ea3ecf5`) preserves the 1.x line.

The exact v1→v2 deltas inside the lib (confirms §1's API column with real code):

| File:line (at `5f8f2cd`, v1) | v1 | v2 (`98d45b8`) |
|---|---|---|
| `src/remote_scales.h:134` | `class RemoteScalesScanner : public NimBLEAdvertisedDeviceCallbacks` | `: public NimBLEScanCallbacks` |
| `src/remote_scales.h:140` | `void onResult(NimBLEAdvertisedDevice*)` | `void onResult(const NimBLEAdvertisedDevice*)` |
| `src/remote_scales.h:11` | `DiscoveredDevice(NimBLEAdvertisedDevice*)` | `DiscoveredDevice(const NimBLEAdvertisedDevice*)` |
| `src/remote_scales.cpp:100` | `getScan()->setAdvertisedDeviceCallbacks(this, true)` | `getScan()->setScanCallbacks(this, true)` |
| `src/remote_scales.cpp:106` | `getScan()->start(0, nullptr, false)` | `getScan()->start(0, false)` |
| `src/remote_scales.cpp:124` | `advertisedDevice->getAddress().getNative()` | `...getAddress().getVal()` (`getNative` removed → `getBase`/`getVal`) |

Per-driver scale files (`acaia`, `bookoo`, `dot`, `timemore`, …) use the
`subscribe(bool, callback)` notify API whose 4-arg callback shape is unchanged in
2.x, so they ported with no callback-body changes.

**Caveat (the catch):** the v2 scales line is built for **pioarduino + the
`esp-nimble-cpp` IDF managed component**, not the PlatformIO-library NimBLE we use
today. Scales-lib commit `130942f` ("remove NimBLE-Arduino dep; headers from
esp-nimble-cpp IDF component") states: *"Under dual-framework pioarduino builds,
h2zero/esp-nimble-cpp is declared as an IDF managed component
(`src/idf_component.yml` in gaggimate). … Declaring NimBLE-Arduino here would
duplicate C++ class symbols in the binary."* The firmware tree has **no
`idf_component.yml` today** (verified — only `.pio` build artifacts), so adopting
the scales-lib v2 line implies (a) moving to pioarduino and (b) adding the
`esp-nimble-cpp` IDF component declaration. That is the platform-coupling that
makes this a platform-migration slice, not an isolated bump.

---

## 4. NimBLEComm wrapper impact

Fully inventoried in §1. Net: `NimBLEClientController` takes the bigger hit
(scan-callback class swap + `clearDuplicateCache` removal + client `onDisconnect`
reason param + 7 subscribe sites to recompile), `NimBLEServerController` takes 3
callback-signature edits (`onConnect`/`onDisconnect`/`onWrite`). All the nanopb
encode/decode bodies (PRO-241..245) are NimBLE-agnostic and unaffected — they
operate on `uint8_t*`/`NimBLEAttValue` buffers whose API is stable.

**Volumetric / grace-period interaction (dispatch AC bullet 3):** the
source-switching + `BLUETOOTH_GRACE_PERIOD_MS` (1500ms, `Controller.h:267`) logic
lives entirely in `src/display/core/Controller.cpp` (`isActiveVolumetricSourceLive()`
at `:494`, source latch at `:1042-1145`, grace check at `:1447`) and consumes
weight via `Controller::onVolumetricMeasurement()` / the `RemoteScales` weight
callback — **none of it calls NimBLE directly**. A NimBLE bump cannot perturb the
*timing logic* itself; the only risk vector is upstream of it — if the ported
scale driver delivers notifications at a different cadence or with a connection
hiccup during the port, the grace window / source latch would see it as a normal
BLE dropout. So: no code change needed in the volumetric path, but it is on the
hardware smoke-test list (brew a shot on a real BLE scale, confirm yield is
authoritative and the source doesn't flap).

---

## 5. Config-macro deltas

Current flags (`platformio.ini:47-48`, in `[display_common] build_flags`, applied
to both display and controller via `[env:controller]`):
```
-DCONFIG_NIMBLE_CPP_LOG_LEVEL=2
-DCONFIG_BT_NIMBLE_PINNED_TO_CORE=0
```
- `CONFIG_NIMBLE_CPP_LOG_LEVEL` — **still present in 2.x** (the CPP wrapper's log
  level macro persists). Keep, re-verify the value maps to the same verbosity.
- `CONFIG_BT_NIMBLE_PINNED_TO_CORE` — this is an **IDF NimBLE host** Kconfig, not
  a NimBLE-Arduino-CPP macro. Under core 3.x / IDF 5.x it still exists but its
  effective default and the surrounding `CONFIG_BT_NIMBLE_*` set changed across
  the IDF 4.4 → 5.x jump. **Re-audit the full `CONFIG_BT_NIMBLE_*` block under
  pioarduino** when the platform moves — some macros were renamed/relocated and a
  stale `-D` silently no-ops rather than erroring.
- Action item for the migration PR: after the platform bump, diff the resolved
  `sdkconfig`/NimBLE Kconfig defaults against these two `-D`s and drop or rename
  any that the new core ignores.

---

## 6. Flash / RAM / build notes — and a correction to PRO-289

**No trial compile was performed for this spike.** A meaningful flash/RAM
measurement requires actually installing the pioarduino platform and resolving
the *other* core-3.x blocker (`TFT_eSPI`, broken on Arduino 3.x per PRO-257), so a
bare `pio run -e display` with only the NimBLE pin bumped would not link on the
current platform and would not represent the real migrated binary. Measuring the
delta is correctly the first task of the migration child, not this research
spike. Qualitatively, upstream advertises 2.x as *lower* RAM/flash than 1.x
("greatly reduced resource use"), so the BLE-stack delta is expected to be
neutral-to-favorable; the platform/core jump dominates any size change.

**Correction to `docs/nimble-2x-migration-spike.md` (PRO-289 §"Can the migration
be staged on the CURRENT 6.12.0 platform first?"):** PRO-289 recommends pinning
NimBLE 2.x on the existing `espressif32@6.12.0` / Arduino-2.0.17 / IDF-4.4
platform to isolate the BLE churn from the platform churn. This spike's evidence
makes that path **doubtful, not recommended**:
- The maintainers' own v2 scales line (the dependency we'd consume) is explicitly
  built for pioarduino + the `esp-nimble-cpp` IDF managed component (commit
  `130942f`), and *removed* the PlatformIO-library NimBLE dep to avoid duplicate
  symbols. Consuming scales-lib v2 on the old PlatformIO-library NimBLE path is
  not a configuration the upstream tests.
- NimBLE-Arduino 2.x point releases increasingly assume IDF 5.x; pairing a 2.x
  release that still builds on IDF 4.4 with a scales-lib v2 that expects the IDF
  component is fragile.

Net recommendation: **do NimBLE 2.x + the pioarduino core bump together on one
branch** (NimBLE ported first within that branch, before flipping other deps),
rather than betting on a 6.12.0-compatible 2.x staging step. If a quick spike
*can* prove a specific NimBLE 2.x release links on 6.12.0 with the v1-style PIO
library layout, the isolation step is a bonus — but don't plan the slicing around
it.

---

## 7. Effort + risk estimate

| Dimension | Estimate |
|---|---|
| BLE API port (3 first-party libs) in isolation | **Medium-low.** Mostly compiler-caught mechanical edits; ~6 signature changes + 4 renames. 1-2 focused days incl. hardware smoke test. |
| Scales-lib | **Low (now).** Pin bump `#5f8f2cd` → `v2.0.0`/`main`; upstream already ported. |
| Coupled platform bump (pioarduino core 3.x) | **Large / high blast radius** (PRO-257): `TFT_eSPI` is an unsolved core-3.x blocker, plus full `lib_deps` + SPIFFS-pipeline + 3-board re-validation, then the `gnu++17`→`gnu++20` flip. |
| **Overall** | **GO on NimBLE 2.x as the first slice of the platform migration; the migration as a whole is a Large tracked effort with the full CI matrix, not a standalone PR.** |

Risk register: (1) `TFT_eSPI` on core 3.x is the worst unknown — independent of
NimBLE but blocks the display env. (2) Hardware re-test of the BLE link + OTA +
scale streaming is mandatory (cannot be proven by CI alone). (3) `CONFIG_BT_NIMBLE_*`
macro drift under IDF 5.x (§5).

---

## 8. Proposed slicing plan / follow-up issues

The decomposition PRO-289/PRO-257 implies already exists as PRO-288 (pioarduino
migration umbrella) → PRO-289 (this NimBLE de-risk, done) → PRO-290 (the NimBLE
port) → PRO-293 (platform bump). This spike's recommendation refines that chain:

1. **`feature` — NimBLE 1.x→2.x port of `NimBLEComm` + `ble_ota_dfu` + `OTA`
   client side** (≈ PRO-290). Mechanical edits per §1; bump scales-lib pin to
   `v2.0.0`/`main`; do it on the pioarduino branch (per §6 correction), NimBLE
   landing before the other dep flips. Verify: matrix builds + hardware smoke
   test (link across a brew, BLE-OTA, real scale weight + volumetric authority).
2. **`feature` — resolve `TFT_eSPI` on Arduino core 3.x** (patched fork or driver
   swap) for the `display` env. Independent of NimBLE; can run in parallel.
3. **`feature` — pioarduino platform switch + `lib_deps`/SPIFFS/board
   re-validation** (≈ PRO-293): flip `platform =`, add `src/idf_component.yml`
   declaring `esp-nimble-cpp`, get `display`/`display-headless`/`controller`
   green, measure flash/RAM delta, re-audit `CONFIG_BT_NIMBLE_*` (§5).
4. **`chore` — flip `-std=gnu++17` → `gnu++20`** (the CAR-340 payoff) once 1-3 are
   green; re-run the matrix.
5. **`feature` (regression) — volumetric / grace-period hardware regression
   pass** on the ported stack (§4): brew on a real BLE scale, confirm yield
   authority and no source flapping across the `BLUETOOTH_GRACE_PERIOD_MS` window.

Ref PRO-255
Ref PRO-288
Ref PRO-289
Ref PRO-257

[NimBLE-Arduino#641]: https://github.com/h2zero/NimBLE-Arduino/issues/641
