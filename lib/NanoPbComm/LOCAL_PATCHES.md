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
