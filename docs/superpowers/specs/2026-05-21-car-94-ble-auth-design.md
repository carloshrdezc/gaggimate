# CAR-94: BLE Authentication & Encryption for Display↔Controller Link

**Date:** 2026-05-21
**Linear:** [CAR-94](https://linear.app/) — `[F-01] Unauthenticated remote BLE flash format on controller`
**Status:** Design approved, pending implementation plan

## Problem

The controller ESP32 exposes a NimBLE GATT server with no link-layer security. Two vulnerability classes exist:

1. **Remote flash format (root finding F-01).** The OTA RX characteristic in `lib/ble_ota_dfu/src/ble_ota_dfu.cpp` accepts command byte `0xEF` from any peer and immediately calls `FLASH.format()` (lines 200–216). Any BLE peer in range can wipe the controller's filesystem.
2. **Unauthenticated control surface.** Every WRITE characteristic registered by `NimBLEServerController::initServer()` — output control, alt control, ping, PID, pump model, autotune, pressure scale, volumetric tare, LED — accepts writes from any peer with `NIMBLE_PROPERTY::WRITE` only. A nearby attacker can drive the boiler, valve, and pump.

The controller has no UI for passkey display and ships paired with one specific display. The chosen response is link-layer bonding + encryption with Just-Works pairing, gated on the **full BLE write surface** (not only OTA).

## Goal

After this work:

- The controller refuses unencrypted writes to any control characteristic and to OTA RX. The 0xEF format command, and every other write, requires an authenticated, encrypted link.
- The display and controller pair once on first boot and reconnect transparently across reboots using bonds persisted in NVS.
- Coordinated re-pair (both devices factory-reset together) is the supported field-upgrade path. Asymmetric bond loss is a recognized error state with a clear recovery procedure.
- Out of scope: CI hardware-in-the-loop rig, LED/buzzer pairing signals, OTA fuzzing once bonded.

## Approach

**Approach A — Bonding + encryption with Just-Works pairing.**

Both ESP32s call `NimBLEDevice::setSecurityAuth(BOND=true, MITM=false, SC=true)` during init. All WRITE characteristics on the server move from `WRITE`/`WRITE_NR` to `WRITE_ENC`/`WRITE_NR_ENC`. NimBLE rejects unencrypted writes at the ATT layer before any application handler fires. Bonds (LTKs) persist in NVS namespace `nimble_bond` and survive reboots.

Just-Works is selected because the controller has no display or input. MITM protection is not viable; the threat model accepts that an attacker present *during the one-time pairing window* could MITM the initial key exchange. After bond establishment, the link is encrypted and the attacker is locked out.

## Architecture

```
┌─────────────────────────┐    encrypted GATT     ┌──────────────────────────┐
│  Display (NimBLE        │ ◄──── (post-bond) ──► │  Controller (NimBLE      │
│  client)                │                       │  server)                 │
│                         │                       │                          │
│  setSecurityAuth(       │                       │  setSecurityAuth(        │
│    BOND, !MITM, SC)     │                       │    BOND, !MITM, SC)      │
│                         │                       │                          │
│  bond LTK in NVS        │                       │  bond LTK in NVS         │
│  (nimble_bond)          │                       │  (nimble_bond)           │
│                         │                       │                          │
│  bleAuthFailed flag ──► │                       │  WRITE_ENC on every      │
│  WebSocket status ──►   │                       │  control char + OTA RX   │
│  Web UI                 │                       │                          │
└─────────────────────────┘                       └──────────────────────────┘
```

- **First boot:** neither side has a stored bond. The display connects, NimBLE performs Just-Works pairing, both stacks store the LTK in NVS. The user sees a one-time delay (~1–2 s) on first power-up.
- **Steady state:** subsequent connects auto-encrypt using the stored LTK. No user interaction.
- **Coordinated re-pair:** user holds factory-reset on both devices, both call `NimBLEDevice::deleteAllBonds()`, next connect establishes a new bond.
- **Asymmetric bond loss:** one device has a bond pointing at a peer the other side no longer knows. The side with no bond rejects pairing requests from the encrypted side (or vice versa); pairing fails with `BLE_HS_ATT_ERR_INSUFFICIENT_AUTHEN`. Recovery requires per-device factory-reset of the still-bonded side.

## Components

### Server changes — `lib/NimBLEComm/src/NimBLEServerController.cpp`

In `initServer()`, immediately after `NimBLEDevice::setMTU(128)`:

```cpp
NimBLEDevice::setSecurityAuth(/*bonding*/ true, /*MITM*/ false, /*SC*/ true);
NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
```

Every WRITE characteristic registration changes property flags:

| Characteristic | Old flags | New flags |
|---|---|---|
| `outputControlChar` | `WRITE` | `WRITE_ENC` |
| `altControlChar` | `WRITE` | `WRITE_ENC` |
| `pingChar` | `WRITE` | `WRITE_ENC` |
| `pidControlChar` | `WRITE` | `WRITE_ENC` |
| `pumpModelCoeffsChar` | `WRITE` | `WRITE_ENC` |
| `autotuneChar` | `WRITE` | `WRITE_ENC` |
| `pressureScaleChar` | `WRITE` | `WRITE_ENC` |
| `volumetricTareChar` | `WRITE` | `WRITE_ENC` |
| `ledControlChar` | `WRITE` | `WRITE_ENC` |
| OTA RX (`pCharacteristic_BLE_OTA_DFU_RX` in `ble_ota_dfu.cpp:388`) | `WRITE \| WRITE_NR` | `WRITE_ENC \| WRITE_NR_ENC` |

NOTIFY characteristics stay unchanged. The `onWrite` handler logic is untouched — NimBLE rejects unencrypted writes at the ATT layer before invoking the callback.

### Client changes — `lib/NimBLEComm/src/NimBLEClientController.cpp`

In `initClient()`, immediately after `NimBLEDevice::setMTU(128)`:

```cpp
NimBLEDevice::setSecurityAuth(/*bonding*/ true, /*MITM*/ false, /*SC*/ true);
NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
```

`connectToServer()` itself does not change — pairing happens implicitly during `client->connect()` and during the first encrypted write.

### Bond-mismatch detection (pure function for unit testing)

Extract bond-wipe decision logic into a free function:

```cpp
// Returns true if local bond store should be wiped before attempting pairing.
// Logic: if we have a bond but it points at a different peer than the one
// connecting now, the peer factory-reset and we must too.
bool shouldWipeBondsBeforePair(size_t numBonds,
                               const ble_addr_t *storedPeerAddr,
                               const ble_addr_t *connectingPeerAddr);
```

Call site is in the client's `onConnect` / pre-pair hook. Server side relies on NimBLE's automatic rejection — if a connecting client presents an LTK the server doesn't recognize, NimBLE returns `BLE_HS_ATT_ERR_INSUFFICIENT_AUTHEN` and the application surfaces the failure.

### `bleAuthFailed` status flag (display side)

A new sticky boolean on the display's BLE state. Set on:
- Pairing failure (NimBLE pairing-complete event with non-zero status)
- Encrypted write rejected with `BLE_HS_ATT_ERR_INSUFFICIENT_AUTHEN`

Cleared on successful encrypted write after a fresh pair. Flag is included in the WebSocket status payload so the web UI can display "BLE pairing failed — factory reset required" guidance.

### Factory-reset entry point

Add a single call site (initially exposed as a serial command, future work could surface in UI):

```cpp
NimBLEDevice::deleteAllBonds();
```

Both firmwares need this hook. Triggers documented in user-facing docs as: "Hold X for Y seconds to factory-reset the BLE bond on this device. Repeat on the other device for coordinated re-pair."

## Data Flow

### First boot (no stored bond on either side)

1. Controller boots, advertises with the same UUID as today.
2. Display scans, finds controller, calls `client->connect(addr)`.
3. NimBLE on the display issues a pairing request (Just-Works, SC, no-IO).
4. NimBLE on the controller accepts; ECDH key exchange completes, both sides derive LTK.
5. Both sides write LTK to NVS `nimble_bond`.
6. Display performs first encrypted write to `pingChar`. Controller's `onWrite` fires normally.
7. `bleAuthFailed = false`, status pushed over WebSocket.

### Warm reconnect (both sides have matching bond)

1. Controller advertises, display connects.
2. NimBLE link-layer encryption resumes from stored LTK transparently.
3. First write is already encrypted. No pairing UI, no delay beyond connection time.

### Asymmetric wipe — controller factory-reset, display still bonded

1. Display connects with stored LTK, NimBLE attempts to encrypt.
2. Controller has no LTK for this peer; sends Pairing Failed with `BLE_SM_ERR_KEY_REJ` or rejects encryption.
3. Display's pairing-complete callback fires with non-zero status. `bleAuthFailed = true`. WebSocket pushes status.
4. Web UI surfaces: "BLE auth failed — controller bond cleared. Factory-reset display to re-pair."
5. User factory-resets display. `deleteAllBonds()` runs. Next connect goes through first-boot flow.

### Asymmetric wipe — display factory-reset, controller still bonded

1. Display connects with no LTK and requests fresh pairing.
2. `shouldWipeBondsBeforePair(numBonds=1, stored=display_old_addr, connecting=display_new_addr_or_same)` decides whether the controller should also clear its stale bond.
3. If addresses match (display reused address, just lost its key), the controller still has a stale LTK; pairing fails. The pure function returns `true`, controller calls `deleteAllBonds()`, retries pairing.
4. After retry: fresh pair, both sides store new LTK.

### OTA path (post-fix)

1. Bonded display issues OTA `0xFD` (clear previous), `0xFF` (parts/MTU setup), `0xFB` (chunk writes), `0xFC` (commit) — all over encrypted link.
2. The previously-vulnerable `0xEF` (format) command can only arrive over an encrypted link with a valid bond. Unbonded peers cannot reach the handler.

## Error Handling

| Failure | Detection | Response |
|---|---|---|
| Pairing rejected by peer | NimBLE pairing-complete callback, status != 0 | Set `bleAuthFailed=true`, log peer addr + reason, retry up to `MAX_CONNECT_RETRIES` (3), then back off and rescan |
| Encrypted write returns `BLE_HS_ATT_ERR_INSUFFICIENT_AUTHEN` | NimBLE write callback error | Set `bleAuthFailed=true`, force disconnect, retry connect once; if still failing, surface to UI |
| Bond storage exhausted | `getNumBonds()` >= NimBLE max (default 3) before pair | Call `deleteAllBonds()`, then pair fresh. Log the wipe. |
| Stale bond on one side, peer wants fresh pair | `shouldWipeBondsBeforePair()` returns true | Call `deleteAllBonds()` on local side, retry pair |
| Repeated pair failures (>3) | Retry counter | Stop retrying, leave `bleAuthFailed=true` set, scan continues for diagnostic visibility, web UI shows persistent error |
| OTA `0xEF` from unbonded peer | NimBLE rejects at ATT layer before `onWrite` | No application code runs. No log. (Working as intended.) |
| OTA `0xEF` from bonded peer | Reaches handler as before | Honored — bonded peers are trusted by design. (Out of scope to fuzz further.) |

`bleAuthFailed` is **sticky**: once set, only a successful encrypted write clears it. This prevents a flicker during retry from clearing the UI warning prematurely.

## Testing

### Off-target unit tests (host build)

These compile without NimBLE and run in the existing host test harness:

1. **`shouldWipeBondsBeforePair(numBonds, storedPeerAddr, connectingPeerAddr)`** — pure function.
   - `numBonds=0` → false (nothing to wipe).
   - `numBonds=1, stored==connecting` → false (matching bond, normal reconnect).
   - `numBonds=1, stored!=connecting` → true (stale, peer changed).
   - `numBonds>=max` → true (exhaustion).
2. **`bleAuthFailed` flag state machine.**
   - Set on pairing-complete failure.
   - Set on `INSUFFICIENT_AUTHEN` write error.
   - Sticky across retry attempts.
   - Cleared only on successful encrypted write.
3. **WebSocket status payload serialization.**
   - `bleAuthFailed=true` produces `"bleAuthFailed": true` in the JSON payload.
   - Field is present even when false (for stable UI parsing).

### On-target manual test matrix (acceptance gate)

All 10 scenarios must pass on at least one **LilyGo-T-RGB display + controller** pair before merge. Headless display variants get smoke tests on #1, #2, #7.

| # | Scenario | Procedure | Pass criteria |
|---|---|---|---|
| 1 | First pair | Flash both with no NVS bonds. Power on. | Display connects; pairing completes within 5 s; brew button works. |
| 2 | Warm reconnect | After #1, power-cycle display only. | Reconnects in <3 s, no pairing prompt, no `bleAuthFailed`. |
| 3 | Cold reconnect | After #1, power-cycle both. | Same as #2 but for both sides. |
| 4 | Coordinated re-pair | Factory-reset both, power on. | Goes through #1 flow cleanly. |
| 5 | Asymmetric wipe — controller wiped | After #1, factory-reset controller only, power on. | Display sets `bleAuthFailed=true`; web UI shows error; after factory-resetting display, #1 flow succeeds. |
| 6 | Asymmetric wipe — display wiped | After #1, factory-reset display only, power on. | Either: pure function detects mismatch and controller clears bond, leading to fresh pair; or `bleAuthFailed=true` until controller is also reset. Either is acceptable; documented behavior must match observed. |
| 7 | OTA hardening — unbonded `0xEF` | From a separate unbonded BLE peer (e.g., nRF Connect on phone), connect to controller and write `0xEF` to OTA RX. | Write rejected at ATT layer; no `FLASH.format()` runs; controller filesystem intact. |
| 8 | OTA happy path — bonded | After #1, run a normal OTA update from the display. | Update completes; controller reboots into new firmware. |
| 9 | Brew/steam/grind regression | After #1, run a full brew, full steam, full grind cycle. | All three complete normally. No protocol regressions. |
| 10 | Re-pair retry bound | Force pairing failure (e.g., partial bond corruption), observe retry behavior. | Retries cap at `MAX_CONNECT_RETRIES`; after cap, `bleAuthFailed` stays sticky; logs show bounded retry. |

### Out of scope

- CI hardware-in-the-loop rig (manual matrix is the gate)
- LED/buzzer signals during pairing
- OTA fuzzing post-bond (bonded peers are trusted by design)
- MITM-resistant pairing (no UI on controller; Just-Works is the chosen tradeoff)

## Implementation order

(For the writing-plans skill to expand into a plan.)

1. Add `setSecurityAuth` + `setSecurityIOCap` calls on both server and client init.
2. Flip WRITE characteristic flags to `WRITE_ENC` on the controller.
3. Flip OTA RX characteristic flags to `WRITE_ENC | WRITE_NR_ENC`.
4. Extract `shouldWipeBondsBeforePair` pure function with unit tests.
5. Add `bleAuthFailed` flag, NimBLE pairing-complete and write-error hooks, WebSocket payload field.
6. Add `deleteAllBonds()` factory-reset entry point on both firmwares.
7. Run the manual test matrix on LilyGo-T-RGB pair.
8. Update user docs with the coordinated re-pair procedure.
