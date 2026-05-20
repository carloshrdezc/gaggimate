# Firmware Lifetime / Use-After-Free Cluster — Design

**Date:** 2026-05-20
**Linear issues:** CAR-100, CAR-101, CAR-105, CAR-106
**Project:** Gaggimate
**Target branch:** `dev-master`

## Context

Four firmware bugs filed under the "memory / lifetime" cluster (all P2, all in
`src/display` or `lib/NimBLEComm`). Selected together because they sit in
adjacent code paths (BLE stack, plugin lifecycle) and share a discipline-of-
ownership theme. Each ships as its own PR per the project's AGENTS.md workflow.

| Issue   | One-line summary                                         | Severity                |
|---------|----------------------------------------------------------|-------------------------|
| CAR-100 | `WebUIPlugin::stop()` deletes `ota` while `loop()` uses it | Real cross-task UAF + latent "ota nulled forever after WiFi cycle" |
| CAR-101 | `Controller::getLastProcess()` raw-pointer accessor      | Latent (zero callers); API foot-gun |
| CAR-105 | `&BLEScales != nullptr` always true — dead guards        | Trivial; dead code      |
| CAR-106 | `errorChar->canNotify()` no null check                   | Trivial; crash on older controller firmware |

## Non-Goals

- No broader refactor of plugin lifecycle, smart-pointer migration, or BLE
  client rewrite. Each fix is the smallest change that closes the bug and
  matches surrounding style.
- No introduction of a unit-test framework. The repo's `test/` directory is
  empty boilerplate; verification stays build + manual per AGENTS.md.
- No bundling — four separate Linear issues, four separate PRs targeting
  `dev-master`.

---

## CAR-100 — `WebUIPlugin` `ota` use-after-free

### Root cause

`ota` (a `GitHubOTA *`) is allocated once in `WebUIPlugin::setup()` and freed
in `WebUIPlugin::stop()`. Three FreeRTOS tasks read it concurrently:

| Task                     | Access site(s)                                                                                  |
|--------------------------|--------------------------------------------------------------------------------------------------|
| Arduino main loop        | `WebUIPlugin.cpp:144` (`ota->update()`, blocks for seconds–minutes); 157, 158, 160              |
| Arduino WiFi event       | `WebUIPlugin.cpp:97` registers `stop()` on `controller:wifi:disconnect`; line 372 `delete ota`  |
| AsyncTCP / WebSocket     | `WebUIPlugin.cpp:618` (`handleOTASettings`); 1074–1076 (`updateOTAStatus`)                       |

There is no mutex, atomic, or queue serialising these. The `if (!serverRunning)
return;` guard at line 152 does not close the window because `stop()` writes
`serverRunning = false` *after* the `delete` (line 375 vs 372).

There is also a **latent secondary bug**: `start()` does not reallocate `ota`,
so after the first WiFi disconnect, `ota` is permanently `nullptr` for the rest
of the boot — silently breaking OTA.

### Fix

Treat `ota` as a long-lived singleton owned by the plugin. It is small, has no
WiFi-dependent handles to tear down, and was being freed for no good reason.

1. **`WebUIPlugin.cpp:360-376`** — in `stop()`, remove the block:

   ```cpp
   if (ota != nullptr) {
       delete ota;
       ota = nullptr;
   }
   ```

   `stop()` retains responsibility for `server.end()`, `ws.closeAll()`, the
   `dnsServer` teardown, `stopRelay()`, and `serverRunning = false`.

2. **`WebUIPlugin.cpp:76-101`** — leave the allocation in `setup()` as-is.
   `ota` lives until reboot.

Result:
- No cross-task free → no UAF window.
- Latent "permanently null after WiFi cycle" bug also resolved.
- `start()` no longer needs to think about `ota`.

### Why not other approaches

- **Coarse mutex around all `ota` accesses.** Heavier diff with 9+ access
  sites across 3 tasks. Worse: holding a mutex across the multi-minute
  `ota->update()` call would block `stop()` for the duration, defeating the
  purpose of running it on the WiFi event task at all.
- **Defer `stop()` onto the loop task via a flag.** Cleaner long-term but
  more code churn for a problem the simple fix already solves. Worth
  revisiting only if a future change reintroduces a freeing path.

### Risk

None identified. `GitHubOTA` does not hold WiFi-dependent state (HTTPClient is
constructed per-call inside `update()` / `checkForUpdates()`).

### Verification

- `pio run -e display` — clean build.
- `scripts/format.sh` — clean.
- Manual: trigger WiFi disconnect mid-session, reconnect, confirm OTA check
  still works (this was previously silently broken).
- Stress (best-effort): trigger WiFi disconnect during an `update()` call;
  hard to reproduce on the bench, document attempt in the PR.

---

## CAR-101 — `Controller::getLastProcess()` raw-pointer accessor

### Root cause

`Controller.h:117` declares:

```cpp
Process *getLastProcess() const { return lastProcess; }
```

Every other read or write of `lastProcess` in `Controller.cpp` takes
`processMutex` (lines 346, 799, 823, 826, 989, 1122). The pointee is `delete`d
in `clear()` (called from button handlers, `activateGrind`, `onFlush`) and
in `deactivate()` (which `delete`s the previous `lastProcess` then promotes
`currentProcess` into its slot). A caller that holds the returned pointer
across any of those events reads freed memory.

Current callers: **zero** (`rg getLastProcess` matches only the declaration).
The bug is latent — an API foot-gun that will bite the next person who
reaches for it.

### Fix

Mirror the treatment already applied to the sibling `getProcess()` directly
above (Controller.h:111-115). Mark `getLastProcess()` `[[deprecated]]` and
point users at `getProcessSnapshot()`, which already falls back to
`lastProcess` under the mutex (Controller.cpp:989-991).

`Controller.h:117`:

```cpp
// DEPRECATED: Direct pointer access is unsafe due to race conditions.
// Use getProcessSnapshot() or other thread-safe accessor methods instead.
// This method will be removed in a future version.
[[deprecated("Use getProcessSnapshot() or thread-safe accessor methods instead")]]
Process *getLastProcess() const { return lastProcess; }
```

### Risk

None — no callers, no behaviour change, no new warnings (no call sites to
trip the deprecation attribute).

### Verification

- `pio run -e display` — clean build, no new warnings.
- `scripts/format.sh` — clean.

---

## CAR-105 — `&BLEScales != nullptr` dead guards

### Root cause

`BLEScales` is a global object (`BLEScalePlugin BLEScales;` at
`BLEScalePlugin.cpp:24`), not a pointer or reference. `&BLEScales` is the
address of a non-reference global, which is never null. Two call sites guard
on this dead expression:

- `BLEScalePlugin.cpp:18-22` — free function `on_ble_measurement`.
- `BLEScalePlugin.cpp:264-274` — lambda inside the BLE scale weight-updated
  callback. Comment on line 270 misleadingly says "with null check".

### Fix

Remove the dead guard at both sites.

`BLEScalePlugin.cpp:18-22` becomes:

```cpp
void on_ble_measurement(float value) {
    BLEScales.onMeasurement(value);
}
```

`BLEScalePlugin.cpp:264-274` — keep the `xPortInIsrContext()` check (real and
useful), drop the `if (&BLEScales != nullptr)` wrapping
`BLEScales.onMeasurement(weight)`, and update the misleading comment on line
270 to reflect what the remaining check actually guarantees (ISR-context
safety only).

### Risk

None — removing dead code that the optimizer was already eliminating.

### Verification

- `pio run -e display` — clean build.
- `scripts/format.sh` — clean.
- Manual: confirm BLE scale weight readings still propagate to brew/grind
  flows.

---

## CAR-106 — `errorChar->canNotify()` missing null check

### Root cause

`NimBLEClientController.cpp:113-117`:

```cpp
errorChar = pRemoteService->getCharacteristic(NimBLEUUID(ERROR_CHAR_UUID));
if (errorChar->canNotify()) {
    errorChar->subscribe(...);
}
```

`getCharacteristic()` returns `nullptr` when the controller's GATT server
does not expose `ERROR_CHAR_UUID` — true for any controller firmware
predating `NimBLEServerController.cpp:39`. Line 114 then dereferences null,
crashing the display.

Every other notify characteristic in the same function uses
`if (X != nullptr && X->canNotify())` — `brewBtnChar` (120), `steamBtnChar`
(126), `autotuneResultChar` (132), `sensorChar` (138),
`volumetricMeasurementChar` (144), `tofMeasurementChar` (151). `errorChar` is
the lone outlier.

### Fix

`NimBLEClientController.cpp:114`:

```cpp
if (errorChar != nullptr && errorChar->canNotify()) {
```

Matches the surrounding style exactly. No other change.

### Risk

None — strictly safer.

### Verification

- `pio run -e display` — clean build.
- `scripts/format.sh` — clean.
- Manual: if a pre-error-char-era controller is available, pair it and
  confirm the display no longer crashes on connect; otherwise document in
  the PR.

---

## Cross-Cutting

### Linear workflow (per AGENTS.md)

All four issues already exist in `Backlog` with appropriate type + domain
labels. For each issue, the workflow is:

1. Move to **In progress** when starting that PR.
2. Implement, then move to **QA** for self-review:
   - `pio run -e display`
   - `scripts/format.sh`
   - Re-read the diff against the acceptance criteria above.
3. Open PR targeting `dev-master` with `Fixes CAR-XXX` in the description;
   move issue to **Ready for Testing**.
4. Self-review the PR diff; fix and re-push if anything is off.
5. When approved → **PR ready** → merge → **Done**.

### PR shape

Four PRs, all against `dev-master`. Suggested titles:

- `fix(webui): keep ota allocated for plugin lifetime (CAR-100)`
- `chore(controller): deprecate getLastProcess() raw-pointer accessor (CAR-101)`
- `chore(ble): remove dead &BLEScales != nullptr guards (CAR-105)`
- `fix(ble): null-check errorChar before canNotify() (CAR-106)`

### Order of execution

Suggested order (lowest risk first, lets us merge quick wins while reviewing
the real one):

1. CAR-105 (dead-code removal)
2. CAR-106 (one-line guard)
3. CAR-101 (deprecation attribute, no behaviour change)
4. CAR-100 (the actual UAF fix)

Each is independent — order can change without blocking.
