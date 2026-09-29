# PRO-655: migrate display↔controller comms from NimBLEComm to upstream NanoPbComm

Status: design + increment 1 (vendor + codec tests). Ref PRO-655, epic PRO-651.
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
upstream's own comment in `Protocol.h`. Note that the upstream `2b089d6` fix swapped
two field numbers **and** bumped the version, which is the precedent for
this rule.

### 2.4 Which upstream revision to vendor

- `2b089d6` ("Fix protocol", `Capabilities.dual_boiler`/`addons` renumber, v3→4)
  is **already an ancestor of `v1.9.0`**. v1.9.0 ships `PROTOCOL_VERSION = 5`
  with no `dual_boiler` field. PRO-666 therefore needs no extra cherry-pick for
  this migration.
- `upstream/master` has since re-added `Capabilities.dual_boiler = 6` and bumped
  to `PROTOCOL_VERSION = 6` (dual-boiler work in `346dfec0`, `b91efd5b`,
  `c741e1b1`). It also changes the `SensorCallback`/`SystemInfoCallback`
  signatures.
- **Decision**: vendor **v1.9.0 exactly** (version 5). The v6 delta is a separate
  follow-up once Carlos decides to track master (see open questions). Mixing it
  in would make the controller/display pair incompatible with stock v1.9.0
  firmware.

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
Reverse case (old Carlos display + new controller): the old display finds none
of its per-message chars and never becomes ready. Recovery is to flash the
display first. **HIL order: display first, then the controller via the display's
OTA path.**

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
| 1 | Vendor `lib/NanoPbComm` from **v1.9.0 verbatim**. Add host codec tests (Frame/Payload round-trip for all 17 payloads, `protocol_version` mismatch detection, `coalescingKey`, `CoalescingPriorityQueue`, `UartFraming`) in dedicated envs `native-nanopbcomm{,-sanitize}` plus a CI step. Nothing links it yet. | none (0-byte firmware delta) | **this PR** |
| 2 | Controller: `GaggiMateController` → `GaggiMateServer` (`-e controller` switches to `gaggimate.proto`), and drop `comms.proto` from the controller env. | controller only | todo |
| 3 | Display: `Controller` → `GaggiMateClient`, output batching, `onConnectionChanged`/`onIncompatibleController`, mismatch → OTA-only UI/plugin event. Envs `display*`, `display-sim` (sim comms shim must learn the new API). After this, `lib/NimBLEComm` is unreferenced (AC 1). | display | todo |
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
(recorded in the PR). The real deltas land in increments 2 and 3. The 8 MB
headless env is at 78.6% (baseline doc), and upstream's own v1.9.0 build of the
same lib fits there, so that is not expected to be a blocker. Re-measure then.

## 8. HIL plan (Carlos only; agents must not flash)

1. Flash the display (increment 3 build) and confirm that the old controller is
   detected as incompatible/legacy, the OTA path is offered, and no boiler
   control is sent.
2. Update the controller via the display's controller-OTA. It should pair, and
   SystemInfo should show `proto=5`.
3. Brew, steam and hot water. Power-cycle the controller mid-idle and confirm
   it reconnects.
4. With the BLE scale connected, run a shot. Weight should stream, and the scale
   should survive a controller reconnect.
5. 30-minute soak with no disconnect/panic (serial log).
6. Rollback path: previous display+controller images kept for re-flash.

## 9. Open questions for Carlos

1. Track upstream `master` (PROTOCOL_VERSION 6, `dual_boiler`) now, or stay on
   v1.9.0 = 5 until the next upstream tag? (Default in this plan: v1.9.0.)
2. Should increment 4 (nanopb SystemInfo fallback for old Carlos controllers) be done, or is
   "Legacy controller 0.0.0 + OTA" good enough?
3. Do you want the UART env legs (increment 5)? Is there UART hardware to HIL?
