# PRO-655: migrate display↔controller comms from NimBLEComm to upstream NanoPbComm

Status: design + increment 1 (vendor + codec tests) landed; increments 2-6 open. Ref PRO-655, epic PRO-651.
Base: `feature/upstream-v1.9-integration` (Carlos dev-master + v1.9 docs).
Upstream reference: tag `v1.9.0` (pristine tree), commits `ccfe792b` (#726, framed
nanopb protocol) and `94f8a5e` (#730, incompatible-controller OTA recovery).

## 1. Summary / key finding

**Carlos's display↔controller wire vocabulary is a strict subset of upstream
v1.9.0's.** No Carlos-only data crosses the display↔controller link. So the
"add Carlos fields to `gaggimate.proto`" acceptance criterion is **empty**:
there are no new proto fields to add, and `PROTOCOL_VERSION` stays upstream's.
Evidence is in §2.3. This means the migration is a transport/API swap plus
link-behaviour re-validation, not a protocol extension, and it does not fork
the protocol.

## 2. Current Carlos comms surface

### 2.1 Library

`lib/NimBLEComm/src/`: `NimBLEComm.h` (UUIDs, error codes, callback typedefs),
`NimBLEClientController.{h,cpp}` (display side), `NimBLEServerController.{h,cpp}`
(controller side). There is one GATT characteristic per message. Payloads are
nanopb-encoded per characteristic (PRO-240..245, `lib/NimBLEComm/proto/comms.proto`,
package `gaggimate`). There is no framing, acks or queue; each write/notify is
one message.

Changes since merge base `0172f1f6` (29 commits on `lib/NimBLEComm`):
- Wire encoding moved from text to nanopb (PRO-241/242/243/244/245, PRO-306 rename).
- Link behaviour: CAR-249 (disconnect/timeout fixes), CAR-116 (`serverAddress`
  cached by value, `e63bcbff`), CAR-106 / `dbafc60b` (optional error char null
  guard), PRO-303/304 (heap headroom log), PRO-305 (LOG_TAG).
- Bond/security (CAR-94/225/231/257 encrypted writes, bond recovery) was
  **reverted** at the tip (`df7ccc89`, `ea232239`), so there is nothing to port.
- NimBLE 2.x migration (PRO-290, `c150d3d3`) was rolled back by `ea232239`, so
  the tip is on NimBLE-Arduino **1.4.3**.
- Public header API: unchanged apart from `serverDevice*` becoming
  `NimBLEAddress serverAddress`, removal of `float_to_string` and removal of
  `_lastOutputControl`.

### 2.2 Call sites (all of them)

The only consumers are `src/display/core/Controller.{h,cpp}` (display, 21 call
sites) and `lib/GaggiMateController/src/GaggiMateController.{h,cpp}` (controller,
19 call sites). `src/controller/**` has none. It only instantiates
`GaggiMateController`.

| Direction | NimBLEComm API (Carlos) | Upstream NanoPbComm equivalent |
|---|---|---|
| D→C | `sendOutputControl(valve, pumpSetpoint, boilerSetpoint)` | `sendRelayControl(0,valve)` + `sendPumpControl(0,POWER,…)` + `sendBoilerControl(0,TEMPERATURE,…)` (batched) |
| D→C | `sendAdvancedOutputControl(valve, boiler, pressureTarget, p, f)` (×3) | `sendRelayControl` + `sendPumpControl(0, PRESSURE\|FLOW, …)` + `sendBoilerControl` |
| D→C | `sendAltControl(active)` | `sendRelayControl(1, active)` |
| D→C | `sendPidSettings(kp,ki,kd,kf)` | `sendPidSettings` |
| D→C | `sendPumpModelCoeffs(a,b,c,d)` (×2) | `sendPumpSettings(a,b,c,d, …gains)` |
| D→C | `sendAutotune(testTime, samples)` | `sendAutotune(testTime, samples, heaterWattage)` |
| D→C | `setPressureScale(scale)` | `sendPressureScale` |
| D→C | `tare()` / LED control | `tare()` / `sendLedControl(channels, n)` |
| D→C | ping (keep-alive) | `sendPing()` (Endpoint also acks and measures latency) |
| C→D | `sendSensorData(temp, p, puckFlow, pumpFlow, puckR)` (×2) | `sendSensorData(…, pumpPower, heaterPower, waterPumped)` |
| C→D | `sendBrewBtnState` / `sendSteamBtnState` | `sendButtonState(0\|1, pressed)` |
| C→D | `sendAutotuneResult`, `sendVolumetricMeasurement`, `sendTofMeasurement`, `sendError` | same names |
| C→D | SystemInfo/Capabilities on the info char | `setSystemInfo(hw, ver, caps)` (+ `protocol_version`) |
| C | `initServer`, `register*Callback` (15) | `init(name, hw, ver, caps)`, `on*` (10 typed callbacks) |
| D | `initClient`, `isReadyForConnection`, `connectToServer`, `isConnected`, `registerDisconnectCallback`, `register*Callback` (7) | `init`, same three calls, `onConnectionChanged`, `onIncompatibleController`, `on*` |
| D | `getPid`, `getPumpModelCoeffs` (×2), `getPressureScaling` | these are local Settings reads (not wire), no change |

Full grep output: produced during discovery. Regenerate with
`grep -rnE "(clientController|serverController)\.[a-zA-Z]+\(" src lib/GaggiMateController`.

### 2.3 Field-by-field mapping (evidence for "no new proto fields")

Every field of every message in Carlos's `comms.proto`, and where it lands in
upstream v1.9.0's `gaggimate.proto`:

| Carlos `comms.proto` | Upstream `gaggimate.proto` (v1.9.0) |
|---|---|
| `SensorData{temperature,pressure}` | `SensorData.boilers[0].{temperature,pressure}` |
| `SensorData{puck_flow,pump_flow,puck_resistance}` | `SensorData.{puck_flow,pump_flow,puck_resistance}` |
| `Error{code}` (int32) | `Error{code}` (`ErrorCode` enum with the same 0..6 values) |
| `BrewButton{pressed}` / `SteamButton{pressed}` | `ButtonState{index=0\|1, pressed}` |
| `AutotuneResult{kp,ki,kd,kf}` | `AutotuneResult{kp,ki,kd,kf}` |
| `VolumetricMeasurement{value}` | `VolumetricMeasurement{volume}` |
| `TofMeasurement{distance_mm}` | `TofMeasurement{distance}` |
| `Capabilities{dimming,pressure,led_control,tof}` | same field numbers 1..4 (+ `addons`) |
| `SystemInfo{hardware,version,capabilities}` | same (+ `protocol_version`) |
| `SimpleOutput{valve,pump_setpoint,boiler_setpoint}` | `RelayControl`+`PumpControl(POWER)`+`BoilerControl` |
| `AdvancedOutput{valve,boiler_setpoint,pressure_target,pump_pressure,pump_flow}` | `RelayControl`+`PumpControl(PRESSURE\|FLOW)`+`BoilerControl` |
| `AltControl{active}` | `RelayControl{index=1}` |
| `Ping{}` / `Tare{}` / `PressureScale{scale}` | identical |
| `PidSettings{kp,ki,kd,kf}` | identical |
| `PumpModelCoeffs{a,b,c,d}` | `PumpSettings{a,b,c,d,…}` (superset) |
| `AutotuneRequest{test_time,samples}` | `AutotuneRequest{test_time,samples,heater_wattage}` |
| `LedControl{channel,brightness}` | `LedControl{repeated LedChannel}` |

Nothing in `lib/NimBLEComm/src/*.h` adds a characteristic or callback beyond
this list. The candidate Carlos-only features the matrix flagged do **not**
touch the wire:
- **PRO-603 (grind setting), PRO-629/F18 (per-profile brew temp override)**: the
  commits `9cbe1b22`, `927c850b`, `f39cb9db`, `980f18a2` touch no `lib/` or
  `proto` file. The override is resolved on the display, and the controller only
  ever sees the resulting boiler setpoint (`BoilerControl.setpoint`).
- **BoilerFillPlugin / volumetric overrides / BLE scale**: display-side only.
- `lib/GaggiMateController` diff since the merge base is `utilities.h` only.

**Conclusion**: there are zero Carlos-only proto fields. We vendor
`gaggimate.proto` verbatim and inherit upstream's `PROTOCOL_VERSION`. If a
Carlos-only field is ever needed, the rule is: add it with a fresh field number
(never reuse or `reserved`), keep proto3 default = "feature absent", and bump
`PROTOCOL_VERSION` only for changes that are not wire-compatible. That matches
upstream's own comment in `Protocol.h`. Upstream's `2b089d6` ("Fix protocol")
swapped two field numbers **and** bumped the version; it is cited here only as
a precedent for the rule, not as evidence of what v1.9.0 contains (see §2.4).

### 2.4 Which upstream revision to vendor

Evidence is a **direct final-tree comparison**, not ancestry. `2b089d6`
("Fix protocol") is **not** an ancestor of `v1.9.0`
(`git merge-base --is-ancestor 2b089d64 v1.9.0` exits 1; the histories
diverged), so nothing about v1.9.0 is inferred from it. The two candidate trees:

```
$ git show v1.9.0:lib/NanoPbComm/proto/gaggimate.proto        # lines 161-167
message Capabilities {
    bool dimming = 1;
    bool pressure = 2;
    bool led_control = 3;
    bool tof = 4;
    repeated Addon addons = 5;
}
$ git show v1.9.0:lib/NanoPbComm/src/Protocol.h               # line 22
static constexpr uint32_t PROTOCOL_VERSION = 5;

$ git show upstream/master:lib/NanoPbComm/proto/gaggimate.proto  # lines 161-168
message Capabilities {
    bool dimming = 1;
    bool pressure = 2;
    bool led_control = 3;
    bool tof = 4;
    repeated Addon addons = 5;
    bool dual_boiler = 6;
}
$ git show upstream/master:lib/NanoPbComm/src/Protocol.h      # line 22
static constexpr uint32_t PROTOCOL_VERSION = 6;
```

The vendored `lib/NanoPbComm` is byte-identical to the v1.9.0 tree
(`diff -r` against a pristine v1.9.0 checkout is empty), so this PR ships the
first shape: no `dual_boiler`, version 5. `upstream/master` also changes the
`SensorCallback`/`SystemInfoCallback` signatures (dual-boiler work in
`346dfec0`, `b91efd5b`, `c741e1b1`).

**Current default**: vendor **v1.9.0 exactly** (v5). Moving to v6 (PRO-666
"adopt now") is a pending **Carlos decision** (§9 Q1). It is not decided here.
Here is what each option changes downstream:

| | Option A: stay v5 (default) | Option B: adopt v6 now (PRO-666) |
|---|---|---|
| Inc 1 (this PR) | as is, verbatim v1.9.0 | no longer verbatim: add `Capabilities.dual_boiler = 6`, `PROTOCOL_VERSION = 6`, update `test_protocol_version_pinned` to 6 plus a `dual_boiler` round-trip |
| Inc 2 (controller) | `GaggiMateServer::init(..., caps)` with v1.9.0 caps; publishes `protocol_version = 5` | same, plus it sets `caps.dual_boiler = false` (Carlos hardware is single boiler); publishes 6. Do **not** port upstream's obsolete intermediate server bool API; use master's final shape |
| Inc 3 (display) | v1.9.0 callback signatures; mismatch gate `!= 5` | master's final `SensorCallback`/`SystemInfoCallback` signatures (boiler index / dual-boiler flag); mismatch gate `!= 6`; UI ignores `dual_boiler` (always false) |
| Interop | pairs with stock v1.9.0 firmware | pairs with upstream master/next tag; stock v1.9.0 halves are reported as mismatch → OTA-only |
| HIL §8 step 2 | expects `proto=5` | expects `proto=6` |

Increments 2 and 3 have to use the **same** option. Mixing them is the §4
mismatch path by construction.

## 3. Upstream NanoPbComm (v1.9.0)

Layers (`lib/NanoPbComm/src`, ~2.6 kLOC):
- `gaggimate.proto` + `.options`: `Frame{id, ack, repeated Payload payloads(max 6)}`,
  `Payload` is a `oneof` of 17 messages (D→C tags 1–11, C→D tags 20–26).
  nanopb codegen runs at build time (`custom_nanopb_protos`,
  `custom_nanopb_options = --error-on-unmatched`) and nothing generated is committed.
- `Messages.h`: `gm::` aliases for the nanopb structs. `Protocol.h`: UUIDs,
  `PROTOCOL_VERSION`, priorities, `coalescingKey()` (header-only, pure).
- `CoalescingPriorityQueue.h`: pure, header-only.
- `Endpoint.{h,cpp}`: reliable framed link (ids/acks, retransmit, latency,
  coalescing queue). It is **FreeRTOS-bound** (queue/semaphore/task notify).
- `Transport.h`: abstract datagram transport. `ble/BleClientTransport`,
  `ble/BleServerTransport` (TX/RX/INFO chars), `uart/UartTransport` +
  `UartFraming.h` (pure).
- `GaggiMateClient` (display) / `GaggiMateServer` (controller): typed facades.

How upstream wires it:
- Display `Controller` owns `GaggiMateClient comms`. `setup()` calls
  `comms.init("GPBLC")` and registers `onSystemInfo`, `onSensorData`,
  `onButtonState`, `onConnectionChanged`, `onIncompatibleController`, etc.
  `loop()` calls `comms.loop()` (send pump + retransmit). Outputs are sent as
  batched Relay/Pump/Boiler payloads.
- Controller `GaggiMateController` owns `GaggiMateServer`. `init(name, hw, ver,
  caps)` pushes SystemInfo, and there are `on*` handlers per D→C payload. Sensor
  data goes out as `sendUnreliable` telemetry.

## 4. Version handling and mixed-version recovery

Upstream semantics (kept verbatim):
1. The controller publishes `SystemInfo.protocol_version` over the framed link.
   The display compares it with `gm_proto::PROTOCOL_VERSION`. On mismatch it
   triggers `controller:protocol:mismatch`, inhibits control and offers OTA only.
2. If the TX/RX chars are missing (a pre-NanoPb controller), `BleClientTransport`
   sets `_incompatible`, keeps the link up for the OTA service, and reads the
   legacy INFO char. `Controller::onIncompatibleController` parses it as
   **JSON** (`hw`/`v`/`cp`).

Carlos-specific wrinkle: **Carlos's current controller encodes the INFO char as
nanopb (PRO-243), not JSON.** A new display paired with an old Carlos controller
therefore hits the `deserializeJson` error branch. It reports "Legacy controller
0.0.0", keeps OTA reachable and inhibits control. That is safe and still
recoverable, but it loses the hw/version display. Options (increment 4):
(a) accept it, (b) teach `onIncompatibleController` to try nanopb `SystemInfo`
decode as a fallback (display-only, no wire change). Recommend (b). It is small
and host-testable.
Reverse case (old Carlos display + new NanoPbComm controller). This is a
**false-ready / degraded** state, not a clean failure. Code at the current tip:
- The service UUID did not change. `lib/NanoPbComm/src/Protocol.h:11`
  `SERVICE_UUID` equals `lib/NimBLEComm/src/NimBLEComm.h:8`. The new server
  also exposes INFO (same UUID, `NimBLEComm.h:18` / `Protocol.h:17`) and the
  legacy error char (`BleServerTransport.cpp:31,34`). The old display's scan
  therefore matches, and it connects.
- `NimBLEClientController::connectToServer()`
  (`lib/NimBLEComm/src/NimBLEClientController.cpp:86-180`) fails only when the
  *service* is missing (`:113-118`). Each per-message `getCharacteristic()`
  (`:121-171`) can return null, subscriptions are skipped when null, and it
  **returns true** (`:179`).
- `Controller::loop()` (`src/display/core/Controller.cpp:482-497`) then clears
  `waitingForController` and calls `setupInfos()` (`:345`). `readInfo()`
  (`NimBLEClientController.cpp:79-84`) returns the new server's **JSON** INFO
  (`GaggiMateServer.cpp:43-50`). `pb_decode` of that text (`Controller.cpp:375`)
  fails or yields garbage, and the fallback is "GaggiMate Standard 1.x v1.0.0,
  no caps". It then sends PID/pump coeffs (no-ops) and fires `CONTROLLER_READY`
  and `CONTROLLER_BLUETOOTH_CONNECT`.
- Result: the UI believes a controller is connected and ready, but it gets no
  sensor, button or error telemetry. Every write (`sendOutputControl`, `sendPing`,
  PID, and so on) is null-guarded (`NimBLEClientController.cpp:191,213,233,…,330`)
  and silently dropped. **No heater or pump command reaches the controller**, and
  because the display stops pinging, the controller's ping-timeout failsafe (heater,
  pump, valve and alt off; `lib/GaggiMateController/src/GaggiMateController.cpp:202-208`,
  same in v1.9.0) keeps outputs off. The hazard is therefore misleading UI (a
  "brew" that does nothing, stale 0 °C/0 bar readings), not actuation. It still
  has to be prevented.

**Hard rule: upgrade the display first, then the controller through the new
display's controller-OTA.** Never OTA a controller to NanoPbComm while its
display runs legacy NimBLEComm. Release notes and the HIL checklist must say
this explicitly.

Safety requirements carried into increments 2/3:
- R1 (inc 2): the new controller must keep a link-liveness failsafe that works
  whatever the display sends. With no framed traffic, heater/pump/valve/alt
  stay off (upstream's ping timeout; host-test or HIL it).
- R2 (inc 2): the new controller must not act on any legacy-char write (it
  exposes none, and that must stay true).
- R3 (inc 3): the new display treats missing TX/RX as `onIncompatibleController`.
  It shows a clear "incompatible controller, update required" UI, offers
  controller OTA only, and sends no heater/pump commands.
- R4a (inc 2 + small `dev-master` PR, recommended): legacy-display guard. Scope
  is deliberately narrow; see below.
- R4b (inc 3): the real incompatible-controller UX, from upstream's mismatch
  model. See below.

**R4a: legacy dev-master display guard.** In
`NimBLEClientController::connectToServer()`, treat a missing *required*
characteristic (`outputControlChar`, `sensorChar`, `pingChar`, `infoChar`) as
not connected: log a distinct line (e.g. `ESP_LOGE "Controller missing required
char <uuid>: incompatible firmware (NanoPbComm?), not connecting"`), disconnect,
latch `incompatible = true` (getter `isIncompatible()`, host/log visibility
only), and return false.

What R4a guarantees, and nothing more:
- No false-ready: `Controller::loop()` (`src/display/core/Controller.cpp:482`)
  never takes the `connectToServer() == true` branch, so `setupInfos()`,
  `CONTROLLER_READY` and `CONTROLLER_BLUETOOTH_CONNECT` never fire.
- No control or telemetry dependence: nothing is sent to, or read from, the
  incompatible controller.
- The display stays in the existing generic path: after
  `CONTROLLER_WAITING_TIMEOUT_MS` (`Controller.h:307`, 10 s) the
  `waitingForController` branch (`Controller.cpp:476-480`) fires
  `CONTROLLER_BLUETOOTH_WAITING`, and the standby kicker shows the existing
  "WAITING FOR CONTROLLER" (`DefaultUI.cpp:1049`). **That is what the user
  sees.** There is no "incompatible controller" screen in R4a.
- It re-scans and re-rejects the controller on every attempt (bounded by the
  scan cadence; log only).

Why R4a does not reuse the error UI: `ERROR_CODE_*`
(`lib/NimBLEComm/src/NimBLEComm.h:28-33`) reaches `Controller::error` only
through the remote-error callback (`Controller.cpp:317-325`), which needs a
connected controller. Setting it locally would need new Controller plumbing, and
the only rendered text is the generic "ERROR · RESTART" (`DefaultUI.cpp:1045`),
which is wrong advice (restart does not fix it) and latches `isErrorState()`.
Adding a proper message would need SquareLine/LVGL work. So R4a stays at
waiting + log, with no UI change.

Recovery (release notes/HIL): the display cannot recover itself. Either (a)
update the display to the NanoPbComm build (increment 3), then OTA the controller
from it, or (b) re-flash the controller to the legacy build over USB. A
power-cycle alone only re-enters the same waiting state.

R4a is display-only, adds no wire change, and is about 10-15 lines plus a log
line. It covers only displays that ship it *before* a NanoPbComm controller
exists in the field, which is why `dev-master` (the shipping legacy build) should
carry it too, as a separate small PR/issue. Already-deployed displays cannot get
it, so the display-first rule stays.

**R4b: integrated display (increment 3).** The proper incompatible-controller
UX is upstream's existing mismatch model, taken as-is with increment 3 (refs
from `v1.9.0`, `src/display/core/Controller.cpp` unless noted):
- `onSystemInfo()` (:322-352) sets `systemInfo.protocolMismatch`. On mismatch
  it logs "control inhibited, OTA only", triggers `controller:protocol:mismatch`,
  and skips PID/pressure/pump-coeff config. `controller:ready` still fires, but
  standby is not activated for a mismatch.
- The keepalive ping is suppressed on mismatch (:636), so the controller's own
  ping-timeout failsafe keeps outputs off.
- `updateControl()` returns early on mismatch (:897): control is inhibited.
- `getSystemState()` returns `SYSTEM_PROTOCOL_MISMATCH` (state key "mismatch",
  :652-661), and `getSystemStateMessage()` returns "Version mismatch, update
  display" or "... update controller", depending on which side is older
  (:677-678).
- `src/display/ui/default/DefaultUI.cpp:174` handles `controller:protocol:mismatch`
  by switching to the standby screen, where the state message is rendered.
- R3's missing-TX/RX path (`onIncompatibleController`) feeds the same model.

UI dependency: the core half (the mismatch flag, ping suppression, control
inhibition, `SystemState`/message, event, and the WebSocket state key via
`WebSocketHandler.cpp`) is UI-agnostic and comes with increment 3. The
rendering half (`DefaultUI.cpp:174` plus the standby-screen label) is upstream's
**EEZ** UI (`src/display/ui/default/eez`). Carlos's display uses the SquareLine **LVGL**
`DefaultUI` (`src/display/ui/default/lvgl`), so increment 3 must add an
equivalent handler plus a kicker message ("VERSION MISMATCH · UPDATE
DISPLAY/CONTROLLER") in Carlos's `DefaultUI`. That is an LVGL-side change unless
PRO-658 (EEZ UI port) lands first, and it needs a host test for the
state-to-message mapping.

## 5. BLE scale coexistence, PSRAM, NimBLE pin

- Both the old and the new client call `NimBLEDevice::init` and share the single
  `NimBLEDevice::getScan()`. Carlos's `BLEScalePlugin` (PRO-459/647/5 teardown
  order, `stopAsyncScan` before delete, mutex, UAF fixes) owns its own
  `RemoteScalesScanner` and does not touch the comms client. Upstream
  `BleClientTransport` calls `setAdvertisedDeviceCallbacks(this, true)` on the
  shared scanner, **exactly like** `NimBLEClientController` does today. So the
  coexistence contract is unchanged. HIL must still cover "scale connected
  during a shot" and "controller reconnect while scale connected".
  Upstream's transport stores the paired controller address in its own NVS and
  only clears the controller bond (`clearBonds`), which is compatible with scale
  bonds (3-slot store).
- NimBLE host PSRAM (`-DCONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL=1`, F10,
  `886bbc3e`) is an env build flag. It is independent of the comms lib and stays.
- NimBLE-Arduino: keep Carlos's exact pin **`1.4.3`**. It satisfies upstream's
  `^1.4.0` and is what `ea232239` settled on. The vendored `library.json` keeps
  `^1.4.0`. The env-level pin wins.
- **PRO-10**: upstream `BleClientTransport` still has plain `_serverAddress` /
  `_readyForConnection` members written from the scan callback and read in
  `connectToServer()`. **The race still exists upstream.** Re-scope PRO-10 to
  `lib/NanoPbComm/src/ble/BleClientTransport` rather than closing it.
  Evidence: `onResult()` (NimBLE host task) writes both members at
  `BleClientTransport.cpp:256+`. `maintain()` reads them at lines 41/59 from
  the display loop task.

## 6. Ordered increments (each one is a PR that builds and passes tests on its own)

| # | Scope | Runtime change | Status |
|---|---|---|---|
| 1 | Vendor `lib/NanoPbComm` from **v1.9.0 verbatim**. Add host codec tests (Frame/Payload round-trip for all 17 payloads, `protocol_version` missing-field codec default (the production mismatch gate is not host-testable yet; it is tested in increment 3 and by HIL §8), `coalescingKey`, `CoalescingPriorityQueue`, `UartFraming`) in dedicated envs `native-nanopbcomm{,-sanitize}` plus a CI step. Nothing links it yet. | none (0-byte firmware delta) | **this PR** |
| 2 | Controller: `GaggiMateController` → `GaggiMateServer` (`-e controller` switches to `gaggimate.proto`), and drop `comms.proto` from the controller env. Also R1/R2 (§4), plus the R4a legacy-client guard (no false-ready; waiting path + log only, no new UI). | controller (+ tiny legacy-display guard) | todo |
| 3 | Display: `Controller` → `GaggiMateClient`, output batching, `onConnectionChanged`/`onIncompatibleController`, mismatch → OTA-only UI/plugin event, plus R3 (§4). Host-test the mismatch/inhibit policy (extract it into a pure `*Policy.h`); the codec-level test in inc 1 does not cover it. Envs `display*`, `display-sim` (sim comms shim must learn the new API). After this, `lib/NimBLEComm` is unreferenced (AC 1). | display | todo |
| 4 | Mixed-version: nanopb `SystemInfo` fallback in `onIncompatibleController` for old Carlos controllers (§4), with a host test. | display | todo |
| 5 | UART transport build leg (upstream `display-*-uart`/controller UART envs, if Carlos wants them), plus PRO-10 re-scoped fix in `BleClientTransport` (atomics/portMUX). | optional | todo |
| 6 | HIL (Carlos): see §8. Then PRO-665 deletes `lib/NimBLEComm`. | — | todo |

Increments 2 and 3 **must ship together to devices** (the wire is incompatible),
but they can merge separately on the integration branch because nothing is
released from it. Increment 1 cannot enable `gaggimate.proto` in the firmware
envs: both protos declare `package gaggimate`, so the generated
`gaggimate_SensorData` etc. would collide with `comms.pb.h`. For the same
reason the new host tests live in their own env rather than in `env:native`
(whose `test_nanopb_comms` still covers `comms.proto` until PRO-665).

## 7. Size

Increment 1 adds no runtime code, so the firmware delta is expected to be zero
(measured, see the table below).

Increment 1 measured on this host (pio 6.1.19). The numbers are **identical to
the byte** with and without `lib/NanoPbComm` present (re-built controller and
headless-8m with the lib moved away). No firmware env references it, so LDF
does not compile it:

| Env | App / Flash (B) | % of app0 | Static DRAM (B) |
|---|---|---|---|
| controller | 681,917 | 20.4% | 32,640 |
| display | 2,999,297 | 45.8% | 79,428 |
| display-headless | 2,143,145 | 32.7% | 73,836 |
| display-headless-8m | 2,132,493 | 63.8% | 73,404 |
| display-sim (host ELF text/data/bss) | 2,869,063 / 33,672 / 12,748 | n/a | n/a |

(`upstream-v1.9-baseline.md` records *upstream* v1.9.0: headless-8m at 78.6%,
2,628,425 B. Carlos's branch is currently about 496 KB smaller there. The 78.6%
figure is the ceiling reference for after increment 3.) The real deltas land in increments 2 and 3. The 8 MB
headless env is at 78.6% (baseline doc), and upstream's own v1.9.0 build of the
same lib fits there, so that is not expected to be a blocker. Re-measure then.

## 8. HIL plan (Carlos only; agents must not flash)

1. Flash the display (increment 3 build) and confirm that the old controller is
   detected as incompatible/legacy, the OTA path is offered, and no boiler
   control is sent.
2. Update the controller via the display's controller-OTA. It should pair, and
   SystemInfo should show `proto=5` (or 6 under §2.4 option B).
2a. Reverse case (hard-rule check, controlled bench only): pair a **legacy**
   display with the **new** controller. Confirm (i) no heater/pump actuation
   (boiler temperature flat, pump silent), (ii) the controller's ping-timeout failsafe log
   fires, (iii) with the R4a guard: the serial log shows the distinct
   missing-char line, no `CONTROLLER_READY`/connect event fires, and after
   ~10 s the display shows the generic "WAITING FOR CONTROLLER" (no
   incompatible screen is expected). Without the guard, record the false-ready
   UI. Then (iv) recover by flashing the display first (or re-flash the
   controller to legacy over USB) and confirm that a power-cycle alone does not
   recover. The real "Version mismatch" UX (R4b) is checked in step 1.
3. Brew, steam and hot water. Power-cycle the controller mid-idle and confirm
   it reconnects.
4. With the BLE scale connected, run a shot. Weight should stream, and the scale
   should survive a controller reconnect. BLE scale weight keeps streaming
   smoothly during a shot (no gaps/stutter in the weight trace vs. idle; the
   display now sends the full control frame only on change + 1 s keepalive,
   PRO-655 B-P3-2).
   Also: with a mismatched controller, Start/wake/web `req:process:activate` are
   refused (web shows "Cannot start: ..."), and autotune does not latch AUTOTUNING.
4a. Under mismatch (and with an incompatible/legacy controller), AutoWakeup /
   HomeKit / web mode change keep the display in standby (PRO-670): an
   AutoWakeup schedule hit logs "Auto-wakeup refused", HomeKit "on" logs
   "HomeKit wake refused" and flips back to off, and a web BREW/STEAM mode
   change shows "Cannot leave standby: ...". Entering standby, stop and
   controller OTA still work.
5. 30-minute soak with no disconnect/panic (serial log).
6. Rollback path: previous display+controller images kept for re-flash.

## 9. Open questions for Carlos

1. Track upstream `master` (PROTOCOL_VERSION 6, `dual_boiler`) now, or stay on
   v1.9.0 = 5 until the next upstream tag? (Default in this plan: v1.9.0.
   PRO-666 "adopt now" is **pending your decision**; §2.4 table lists the exact
   inc 1/2/3 changes per option.)
2. Should increment 4 (nanopb SystemInfo fallback for old Carlos controllers) be done, or is
   "Legacy controller 0.0.0 + OTA" good enough?
3. Do you want the UART env legs (increment 5)? Is there UART hardware to HIL?
4. OK to open the R4a legacy-client guard (§4) as a small separate `dev-master`
   PR as well as carrying it in increment 2? It only prevents false-ready: the
   user sees the generic "WAITING FOR CONTROLLER" plus a log line, not an
   incompatible screen. The proper "Version mismatch" UX is R4b (increment 3)
   and needs a `DefaultUI` (LVGL) handler unless PRO-658 lands first.
