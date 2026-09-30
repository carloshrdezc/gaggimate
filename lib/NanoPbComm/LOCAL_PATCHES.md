# Local patches to vendored `lib/NanoPbComm`

Base: upstream `jniebuhr/gaggimate` `upstream/master` final shape of `lib/NanoPbComm`
(v1.9.0 + `dual_boiler = 6`, `PROTOCOL_VERSION = 6`), vendored for PRO-655.
Everything not listed here is byte-identical to that tree
(`git diff upstream/master -- lib/NanoPbComm` shows only these hunks + this file).

## 1. PRO-10: synchronize BLE connect state in `ble/BleClientTransport`

Files: `src/ble/BleClientTransport.h`, `src/ble/BleClientTransport.cpp`
(every hunk is marked `LOCAL PATCH (PRO-10)`).

Problem (also present upstream): `onResult()` runs on the NimBLE host task and
writes `_serverAddress` (multi-byte `NimBLEAddress`), `_haveServerAddress` and
`_readyForConnection`. `maintain()` / `connectToServer()` / `isReadyForConnection()`
read them on the display loop task (other core) with no atomics, lock or barrier,
so the loop could observe the ready flag with a torn / not-yet-visible address.

Fix (minimal):
- `_readyForConnection`, `_haveServerAddress` become `std::atomic<bool>`
  (release on publish, acquire on read).
- `onResult()` publishes address + both flags inside a `portMUX` critical section
  (`_addrMux`); `disconnect()` clears the flags under the same lock.
- `connectToServer()` takes a local snapshot of the address under the lock
  (`snapshotServerAddress()`) and uses only that copy for connect / bond checks /
  `savePairedPeer`, so a concurrent re-publish cannot tear it mid-connect.
- The critical section only copies a trivially-copyable 7-byte value; no
  allocation, logging or NimBLE calls happen while it is held.

Upstream candidate: yes, self-contained; propose it upstream later.

## 2. PRO-655 A-P2-1: clear the OTA watchdog exemption on disconnect

File: `src/ble/BleServerTransport.cpp` (`onDisconnect`, marked `PRO-655 A-P2-1`).

Problem (also present upstream): `BLE_OTA_DFU::updating` was set when a DFU
transfer started and never cleared, so after any aborted OTA the controller's
ping watchdog never force-dropped a wedged link again until reboot.

Fix: `onDisconnect()` calls `_otaDfu.onPeerDisconnect()`. The rest of the
lifecycle (reject/size-mismatch/install fail/finish) lives in `lib/ble_ota_dfu`
(`ota_updating_policy.hpp`, host-tested).

Upstream candidate: yes, together with the `ble_ota_dfu` change.
