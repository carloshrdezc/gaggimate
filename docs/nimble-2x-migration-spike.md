# Spike: NimBLE-Arduino 1.x → 2.x migration de-risk — PRO-289

**Status: GO. The migration is well-scoped and contained to two firmware
libraries + one external dependency. No GaggiMate application/plugin code calls
NimBLE directly. Estimated effort: medium (mechanical API renames + 3 genuine
behavior changes), gated by getting a NimBLE-2.x-compatible `esp-arduino-ble-scales`.**

Step 1 of PRO-288 (pioarduino Arduino-esp32 3.x migration). NimBLE 1.x cannot
build on Arduino-esp32 3.x / IDF 5.1+ (NimBLE-Arduino#641), so it must move to
2.x. This spike maps every call site and the concrete API delta before the
migration child (PRO-290) is dispatched.

## Call-site inventory (the entire NimBLE surface in this repo)

| Location | Role | NimBLE surface used |
|---|---|---|
| `lib/NimBLEComm/src/NimBLEServerController.{h,cpp}` | Controller-side GATT **server** (the device the display connects to) | `NimBLEDevice::init/setPower/setMTU/createServer/getAdvertising`, `NimBLEServer`, `NimBLEService`, `NimBLECharacteristic`, `NimBLEAdvertising`, `NIMBLE_PROPERTY::*`, **`NimBLEServerCallbacks`** + **`NimBLECharacteristicCallbacks`** (this class inherits both), `onConnect/onDisconnect/onWrite`, `setValue/notify/getValue/getUUID`, `NimBLEUUID` |
| `lib/NimBLEComm/src/NimBLEClientController.{h,cpp}` | Display-side GATT **client** + scanner | `NimBLEDevice::createClient/getScan`, `NimBLEClient`, `NimBLEScan`, `NimBLERemoteService`, `NimBLERemoteCharacteristic`, **`NimBLEClientCallbacks`** + **`NimBLEAdvertisedDeviceCallbacks`** (this class inherits both), `client->connect/isConnected/getService/updateConnParams`, `scanner->setAdvertisedDeviceCallbacks/setInterval/setWindow/setMaxResults/setDuplicateFilter/setActiveScan/start/stop/clearDuplicateCache/isScanning`, `char->subscribe/writeValue/readValue/canNotify/canRead`, `NimBLEAdvertisedDevice::haveServiceUUID/isAdvertisingService/getAddress/toString`, `NimBLEAddress` |
| `lib/ble_ota_dfu/src/ble_ota_dfu.{hpp,cpp}` | BLE OTA firmware-update GATT service (attached to the controller server) | Uses the **legacy `BLE*` aliases**: `BLEDevice`, `BLEServer`, `BLECharacteristic`, `BLECharacteristicCallbacks` (`onWrite`/`onNotify`), plus `NimBLEServer*` in `configure_OTA`. `getValue()` used as **`std::string`**. `NIMBLE_PROPERTY::WRITE/WRITE_ENC/WRITE_NR/NOTIFY`. |
| `src/display/plugins/BLEScalePlugin.{h,cpp}` | BLE kitchen-scale integration | **Does NOT touch NimBLE directly.** Talks to the `RemoteScales*` API (`remote_scales.h`, `RemoteScalesScanner`, `RemoteScalesFactory`, `RemoteScalesPluginRegistry`) from the external `esp-arduino-ble-scales` lib. The NimBLE-2.x burden for scales is **inside that library**, not GaggiMate. |
| `esp-arduino-ble-scales` (`lib_deps`, `#5f8f2cd`) | External: vendor scale drivers (Acaia, Bookoo, etc.) over NimBLE | Owns its own NimBLE client/scan code. **The hard external dependency** — see "Gating risk" below. |
| `sim/comms/*` (desktop simulator) | Host build, NOT firmware | Uses a hand-written `NimBLEClient.h` stub, not the real lib. Unaffected by the firmware bump; ignore for this migration. |

**Key scoping conclusion:** all first-party NimBLE code is in **`lib/NimBLEComm`
(2 classes) + `lib/ble_ota_dfu` (1 file)**. The application layer, plugins, and
UI never see NimBLE types. This is a tight, well-bounded migration.

## NimBLE 1.x → 2.x API delta (what actually changes)

NimBLE-Arduino 2.x is a breaking release. The changes that hit GaggiMate's
specific call sites, grouped by effort:

### A. Mechanical renames / signature changes (bulk of the work, low risk)

1. **Callback class method signatures gained a `NimBLEConnInfo&` parameter.**
   - `NimBLEServerCallbacks::onConnect(NimBLEServer*)` →
     `onConnect(NimBLEServer*, NimBLEConnInfo&)`; same for `onDisconnect`
     (now `onDisconnect(NimBLEServer*, NimBLEConnInfo&, int reason)`).
     → `NimBLEServerController::onConnect/onDisconnect` must add the param.
   - `NimBLECharacteristicCallbacks::onWrite(NimBLECharacteristic*)` →
     `onWrite(NimBLECharacteristic*, NimBLEConnInfo&)`.
     → `NimBLEServerController::onWrite` signature change (body unchanged).
   - `NimBLEClientCallbacks::onDisconnect(NimBLEClient*)` →
     `onDisconnect(NimBLEClient*, int reason)`.
     → `NimBLEClientController::onDisconnect` signature change.
2. **`NimBLEAdvertisedDeviceCallbacks` is removed → replaced by
   `NimBLEScanCallbacks`.** `onResult(NimBLEAdvertisedDevice*)` →
   `onResult(const NimBLEAdvertisedDevice*)`, and the scan is wired with
   **`scanner->setScanCallbacks(...)`** instead of
   `setAdvertisedDeviceCallbacks(...)`.
   → `NimBLEClientController` (inherits the callbacks, `scan()`, `onResult`).
3. **Scan-result lifetime / `getScanResults`** semantics changed; the existing
   "copy `getAddress()` by value because the advertised-device pointer may be
   freed" comment in `onResult` is *more* correct in 2.x — keep it.
4. **`getValue()` return type.** In 2.x `NimBLECharacteristic::getValue()` /
   `NimBLERemoteCharacteristic::getValue()` return a **`NimBLEAttValue`** (with
   implicit conversions). `NimBLEServerController` already uses
   `getValue().c_str()` / `getValue()[0]` which still work; `ble_ota_dfu`
   assigns `std::string value = pCharacteristic->getValue();` — verify the
   implicit conversion still compiles or switch to `.c_str()`.
5. **`subscribe()` notify-callback signature.** The
   `notify_callback` typedef in 2.x is
   `std::function<void(NimBLERemoteCharacteristic*, uint8_t*, size_t, bool)>` —
   **same shape** as the current `notifyCallback`, so the `std::bind(...)`
   subscriptions in `connectToServer()` likely need no change beyond the
   callback class rename. Confirm the exact arg constness.
6. **`NIMBLE_PROPERTY::*`** flags are unchanged in 2.x. ✅
7. **`ble_ota_dfu` legacy `BLE*` aliases:** 2.x still provides the
   `BLEDevice`/`BLEServer`/`BLECharacteristic` compatibility typedefs, but
   `BLECharacteristicCallbacks::onWrite/onNotify` get the same
   `NimBLEConnInfo&` param as above. Update those two overrides.

12. **`BLE2902` / `NimBLE2902` descriptor class is REMOVED in 2.x.** NimBLE now
    auto-creates the 0x2902 CCCD whenever a characteristic has NOTIFY/INDICATE.
    GaggiMate's server declares notify characteristics via
    `NIMBLE_PROPERTY::NOTIFY` and never manually creates a `BLE2902` (verified in
    `NimBLEServerController.cpp`), so this is a **no-op for us** — but do NOT add
    one during the port (2.x flags a manual 0x2902 as non-functional). 2.x also
    adds `NimBLECharacteristicCallbacks::onSubscribe`, an *optional* hook if the
    server ever needs to know when the display subscribes.

> **Verified against the upstream NimBLE-Arduino migration guide
> (h2zero/NimBLE-Arduino `docs/Migration_guide.md`, master):** the
> `NimBLEConnInfo&` callback-signature changes (A-1), the `NIMBLE_PROPERTY::*`
> stability (A-6), `getValue()`→`std::string` (A-4), the dual `BLE*`/`NimBLE*`
> class aliases (A-7, no renames required), and the `BLE2902` removal (B-12) are
> confirmed from the official doc, not inferred. The scan-callback rename
> (`NimBLEAdvertisedDeviceCallbacks` → `NimBLEScanCallbacks` /
> `setScanCallbacks`, A-2) is the client-side change to confirm against the 2.x
> `NimBLEScan` class docs during the port.

### B. Genuine behavior changes to verify on hardware (medium risk)

8. **Connection parameters / MTU.** `client->updateConnParams(6, 8, 0, 400)` and
   `NimBLEDevice::setMTU(128)` still exist; confirm the units/defaults didn't
   shift (2.x changed some default timeouts).
9. **Advertising API.** `advertising->setScanResponse(true)` and
   `addServiceUUID` persist, but 2.x reworked `NimBLEAdvertising` internals
   (advertisement data builder). The simple usage here should map directly —
   verify `start()/stop()/isAdvertising()` behavior under the reconnect loop in
   `NimBLEServerController::loop()`.
10. **Security/bonding.** GaggiMate uses `WRITE_ENC` on the OTA RX
    characteristic (`ble_ota_dfu`). 2.x changed the security/bonding setup API
    (`NimBLEDevice::setSecurityAuth` etc.). If pairing is required for OTA,
    re-verify the encrypted-write path pairs correctly.

### C. Gating external dependency (the real risk, not GaggiMate's code)

11. **`esp-arduino-ble-scales` (pinned `#5f8f2cd`) must build on NimBLE 2.x.**
    GaggiMate's scale support is entirely delegated to this lib. The current pin
    is a 1.x-era commit. **Before PRO-290 can finish, this dependency needs a
    NimBLE-2.x-compatible revision** — either an upstream commit/branch that
    moved to 2.x, or a GaggiMate fork carrying the port. This is the single
    biggest scheduling unknown; resolving it may itself be a sub-task. Action:
    check `github.com/gaggimate/esp-arduino-ble-scales` for a 2.x branch; if
    none, the scales port is part of PRO-290's scope (or its own issue).

## Can the migration be staged on the CURRENT 6.12.0 platform first?

**Partially — and that's the recommended de-risking.** NimBLE-Arduino 2.x is
published as an Arduino library and *can* be pinned on the existing
espressif32@6.12.0 / Arduino-2.0.17 platform (2.x supports the IDF 4.4 NimBLE
host as well as 5.x). So PRO-290 can:

1. Bump `h2zero/NimBLE-Arduino` 1.4.3 → 2.x **on the current platform**, port
   `NimBLEComm` + `ble_ota_dfu` + the scales lib, and get the BLE link + OTA +
   scale pairing green on real hardware **without** simultaneously changing the
   compiler/core. This isolates the BLE API churn from the platform churn —
   exactly the high-risk-first sequencing PRO-288 calls for.
2. Then the platform bump (PRO-293) only has to prove the *already-ported* BLE
   stack still builds under gcc 13 / core 3.x, which is a much smaller leap.

Caveat: confirm the specific NimBLE 2.x release builds against Arduino 2.0.17 /
IDF 4.4 (some 2.x point releases assume IDF 5.x). If a 6.12.0-compatible 2.x
release can't be found, fall back to doing the NimBLE port and the platform bump
together on a branch — still scoped to the same two libs.

## Files to change in PRO-290 (the migration)

- `lib/NimBLEComm/src/NimBLEServerController.h` — base-class list unchanged names but updated method signatures (`onConnect/onDisconnect/onWrite` + `NimBLEConnInfo&`).
- `lib/NimBLEComm/src/NimBLEServerController.cpp` — the 3 callback signatures; everything else (createService/createCharacteristic/setValue/notify/getValue/getUUID) is API-stable.
- `lib/NimBLEComm/src/NimBLEClientController.h` — swap `NimBLEAdvertisedDeviceCallbacks` → `NimBLEScanCallbacks`; `onDisconnect` + `reason` param; `onResult` const param.
- `lib/NimBLEComm/src/NimBLEClientController.cpp` — `scan()` uses `setScanCallbacks`; `onResult` const param; `onDisconnect(NimBLEClient*, int)`; verify `subscribe()` bind + `updateConnParams`.
- `lib/ble_ota_dfu/src/ble_ota_dfu.cpp` + `.hpp` — `BLECharacteristicCallbacks::onWrite/onNotify` signatures; verify `getValue()`→`std::string`; verify `WRITE_ENC` security path.
- `platformio.ini` — `h2zero/NimBLE-Arduino@1.4.3` → `@^2.x` in `[display_common] lib_deps_default` and `[env:controller] lib_deps`. Re-check `-DCONFIG_NIMBLE_CPP_LOG_LEVEL` / `-DCONFIG_BT_NIMBLE_PINNED_TO_CORE` flag names (NimBLE 2.x renamed some CPP config macros).
- `lib_deps` `esp-arduino-ble-scales` pin → a NimBLE-2.x-compatible revision/fork (gating — see C-11).
- `sim/comms/*` — no change (uses its own stub).

## Acceptance criteria (this spike — met)

- [x] Every first-party NimBLE call site enumerated (table above).
- [x] 1.x→2.x API delta mapped to GaggiMate's specific usage (sections A/B/C).
- [x] Staging question answered: port on 6.12.0 first if a compatible 2.x release exists, else port + platform-bump together — either way scoped to `NimBLEComm` + `ble_ota_dfu` + the scales lib.
- [x] Gating risk identified: `esp-arduino-ble-scales` needs a 2.x-compatible revision.

## Recommendation for PRO-290

1. First resolve the **`esp-arduino-ble-scales` 2.x** question (upstream branch vs. fork) — it gates scale support and is the only open unknown.
2. Pin NimBLE 2.x on the **current 6.12.0 platform**, port the two first-party libs (mechanical signature changes per section A), and re-verify on hardware: display↔controller link survives a brew cycle, BLE-OTA upload completes, a real scale pairs + streams weight.
3. Hand the *already-green* BLE stack to PRO-293 (platform bump) so the core/compiler change is validated independently.

Ref PRO-288
Ref PRO-289
