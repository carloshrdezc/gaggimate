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
