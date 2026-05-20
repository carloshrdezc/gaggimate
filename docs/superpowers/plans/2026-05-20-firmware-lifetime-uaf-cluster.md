# Firmware Lifetime / UAF Cluster Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close four firmware lifetime/use-after-free bugs (CAR-100, CAR-101, CAR-105, CAR-106) as four separate Linear issues, four separate PRs into `dev-master`.

**Architecture:** Each task is one Linear issue end-to-end: Backlog → In progress → implement → QA self-review (build + format) → Ready for Testing (PR open) → PR ready → Done (merge). No bundling. Sequenced lowest-risk first so quick wins land while the real UAF fix gets focused review.

**Tech Stack:** ESP32 / PlatformIO / Arduino-ESP32 / NimBLE-Arduino. Build env: `display`. Format: `scripts/format.sh` (clang-format, excludes `src/display/ui` and `src/display/drivers`). No unit-test framework — verification is build + manual on hardware.

**Spec:** `docs/superpowers/specs/2026-05-20-firmware-lifetime-uaf-cluster-design.md`

---

## File Structure

| Bug     | File(s) touched                                              | LOC delta |
|---------|--------------------------------------------------------------|-----------|
| CAR-105 | `src/display/plugins/BLEScalePlugin.cpp`                     | -6 / +1   |
| CAR-106 | `lib/NimBLEComm/src/NimBLEClientController.cpp`              | -1 / +1   |
| CAR-101 | `src/display/core/Controller.h`                              | -1 / +5   |
| CAR-100 | `src/display/plugins/WebUIPlugin.cpp`                        | -4 / 0    |

Each task is self-contained — files don't overlap between tasks.

## Conventions for every task

- **Branch naming:** `carloshrdezc/car-<NNN>-<short-slug>` (matches the project's existing pattern in `git log`).
- **Commit message format:** `<type>(<scope>): <subject> (CAR-NNN)` where type ∈ `fix|chore|refactor` and scope ∈ `webui|controller|ble`. Examples in each task below.
- **PR base:** `dev-master` (per AGENTS.md — `master` is downstream).
- **PR body MUST contain** `Fixes CAR-NNN` (auto-closes the Linear issue on merge).
- **Linear state machine** (run via the GraphQL API helper since `lin`/`linear` CLIs aren't installed; `LINEAR_API_KEY` env var is set):
  - Get state IDs once at the start with the helper script in Task 0.
  - Move to `In progress` before coding.
  - Move to `QA` after implementation, before opening PR.
  - Move to `Ready for Testing` immediately after `gh pr create`.
  - Move to `PR ready` after self-review of the diff.
  - Move to `Done` after merge.

---

## Task 0: Setup — fetch Linear state IDs once

**Files:**
- Create (temporary, untracked): `scripts/.linear-states.json` — local cache so subsequent tasks don't re-query.
- Verify: `git status` shows clean tree on `dev-master`.

**Why a single setup task:** Linear's GraphQL API needs UUIDs for state transitions, and we'll do ~20 of those across this plan. Caching avoids re-querying.

- [ ] **Step 0.1: Verify on `dev-master` with clean tree**

Run:
```powershell
git status; git rev-parse --abbrev-ref HEAD
```
Expected: `On branch dev-master`, `nothing to commit, working tree clean`.
If not clean → stop and resolve before continuing.

- [ ] **Step 0.2: Pull latest `dev-master`**

Run:
```powershell
git pull --ff-only origin dev-master
```
Expected: `Already up to date.` or a fast-forward.

- [ ] **Step 0.3: Fetch and cache Linear team workflow state IDs**

Run (PowerShell):
```powershell
$query = 'query { team(id: "9779188a-20da-468b-b86d-a312eafb20c8") { states { nodes { id name type } } } }'
$body = @{ query = $query } | ConvertTo-Json
$response = Invoke-RestMethod -Uri 'https://api.linear.app/graphql' -Method Post -Headers @{ Authorization = $env:LINEAR_API_KEY; 'Content-Type' = 'application/json' } -Body $body
$response.data.team.states.nodes | ConvertTo-Json | Out-File -Encoding utf8 scripts/.linear-states.json
$response.data.team.states.nodes | Format-Table -AutoSize
```
Expected: a table listing states. We need at least these names to exist (transcribe the IDs into the table below as you go):

| State name              | ID (fill in)                          |
|-------------------------|---------------------------------------|
| `Backlog`               |                                       |
| `In progress`           |                                       |
| `QA`                    |                                       |
| `Ready for Testing`     |                                       |
| `PR ready`              |                                       |
| `Done`                  |                                       |

If any name doesn't exist, stop and ask the user how their workspace names the equivalent state — AGENTS.md defines the canonical names but workspaces can differ.

- [ ] **Step 0.4: Confirm baseline build passes before any change**

Run:
```powershell
pio run -e display
```
Expected: build succeeds. Note the elapsed time and the final binary size — we'll compare in QA. If baseline fails, stop and resolve before touching anything.

- [ ] **Step 0.5: Confirm format check is clean**

Run (Git Bash or WSL — `format.sh` is a bash script):
```bash
scripts/format.sh
```
Expected: no diff produced (`git diff --exit-code` after — should be empty). If the baseline has format drift, stop and ask the user before proceeding.

---

## Task 1: CAR-105 — Remove dead `&BLEScales != nullptr` guards

**Linear issue:** CAR-105 — `[F-12] &BLEScales != nullptr always true - dead guard`

**Files:**
- Modify: `src/display/plugins/BLEScalePlugin.cpp` (lines 18-22 and 264-274)

- [ ] **Step 1.1: Move CAR-105 to `In progress`**

Run (PowerShell, substituting `<INPROGRESS_ID>` from Task 0's table):
```powershell
$mut = 'mutation { issueUpdate(id: "CAR-105", input: { stateId: "<INPROGRESS_ID>" }) { success } }'
# Note: id field accepts the human identifier directly in Linear's API
$body = @{ query = $mut } | ConvertTo-Json
Invoke-RestMethod -Uri 'https://api.linear.app/graphql' -Method Post -Headers @{ Authorization = $env:LINEAR_API_KEY; 'Content-Type' = 'application/json' } -Body $body
```
Expected: `{ data: { issueUpdate: { success: true } } }`.

- [ ] **Step 1.2: Create branch**

Run:
```powershell
git checkout -b carloshrdezc/car-105-blescales-dead-guard
```

- [ ] **Step 1.3: Edit `BLEScalePlugin.cpp` lines 18-22**

Replace this block:
```cpp
void on_ble_measurement(float value) {
    if (&BLEScales != nullptr) {
        BLEScales.onMeasurement(value);
    }
}
```
With:
```cpp
void on_ble_measurement(float value) {
    BLEScales.onMeasurement(value);
}
```

- [ ] **Step 1.4: Edit `BLEScalePlugin.cpp` lines 264-274**

Replace this block:
```cpp
            scale->setWeightUpdatedCallback([](float weight) {
                // Check if we're in an ISR context
                if (xPortInIsrContext()) {
                    // Skip measurement to avoid FreeRTOS deadlocks from interrupt context
                    return;
                }
                // Safe to call directly from task context with null check
                if (&BLEScales != nullptr) {
                    BLEScales.onMeasurement(weight);
                }
            });
```
With:
```cpp
            scale->setWeightUpdatedCallback([](float weight) {
                // Skip measurement from ISR context to avoid FreeRTOS deadlocks
                if (xPortInIsrContext()) {
                    return;
                }
                BLEScales.onMeasurement(weight);
            });
```
(The misleading "with null check" comment is removed; the ISR check is preserved with clearer wording.)

- [ ] **Step 1.5: QA self-review — build**

Run:
```powershell
pio run -e display
```
Expected: build succeeds. Binary size delta should be near zero (the dead guards were already optimized away). If the build fails, stop and inspect — likely a typo.

- [ ] **Step 1.6: QA self-review — format**

Run (Git Bash/WSL):
```bash
scripts/format.sh
git diff --exit-code
```
Expected: exit code 0 (no formatting drift). If clang-format reformatted anything, accept the changes (`git add -A`).

- [ ] **Step 1.7: Move CAR-105 to `QA`**

Run with `<QA_ID>`:
```powershell
$mut = 'mutation { issueUpdate(id: "CAR-105", input: { stateId: "<QA_ID>" }) { success } }'
$body = @{ query = $mut } | ConvertTo-Json
Invoke-RestMethod -Uri 'https://api.linear.app/graphql' -Method Post -Headers @{ Authorization = $env:LINEAR_API_KEY; 'Content-Type' = 'application/json' } -Body $body
```

- [ ] **Step 1.8: Re-read the diff**

Run:
```powershell
git diff
```
Acceptance criteria for the diff:
- Exactly two hunks in `src/display/plugins/BLEScalePlugin.cpp`.
- No other files changed.
- Both `&BLEScales != nullptr` occurrences removed.
- ISR-context check preserved.
- No behavioural change beyond the dead-code removal.

If the diff fails any criterion, transition CAR-105 back to `In progress` and fix.

- [ ] **Step 1.9: Commit**

Run:
```powershell
git add src/display/plugins/BLEScalePlugin.cpp
git commit -m "chore(ble): remove dead `&BLEScales != nullptr` guards (CAR-105)"
```

- [ ] **Step 1.10: Push and open PR**

Run:
```powershell
git push -u origin carloshrdezc/car-105-blescales-dead-guard
gh pr create --base dev-master --title "chore(ble): remove dead `&BLEScales != nullptr` guards (CAR-105)" --body @"
Fixes CAR-105

`BLEScales` is a global object (`BLEScalePlugin BLEScales;` in
`BLEScalePlugin.cpp`), not a pointer or reference. `&BLEScales` is the address
of a non-reference global, which can never be null — the guards are dead code
and the compiler was already optimizing them out.

Changes:
- `on_ble_measurement` (free function): drop the dead null-guard.
- Weight-updated callback lambda: drop the dead null-guard, keep the
  `xPortInIsrContext()` check (which is real and useful), update the
  misleading comment.

Verification: `pio run -e display` clean; `scripts/format.sh` clean. No
behavioural change — manual smoke test of BLE scale weight propagation
recommended on hardware.

Spec: docs/superpowers/specs/2026-05-20-firmware-lifetime-uaf-cluster-design.md
"@
```
Capture the printed PR URL.

- [ ] **Step 1.11: Move CAR-105 to `Ready for Testing`**

Run with `<R4T_ID>`:
```powershell
$mut = 'mutation { issueUpdate(id: "CAR-105", input: { stateId: "<R4T_ID>" }) { success } }'
$body = @{ query = $mut } | ConvertTo-Json
Invoke-RestMethod -Uri 'https://api.linear.app/graphql' -Method Post -Headers @{ Authorization = $env:LINEAR_API_KEY; 'Content-Type' = 'application/json' } -Body $body
```

- [ ] **Step 1.12: Self-review the PR diff in `gh`**

Run:
```powershell
gh pr view --web
```
Re-read the diff in the browser as if reviewing someone else's code. Confirm:
- Diff matches the intent.
- No accidental file additions/deletions.
- PR title and `Fixes CAR-105` line are present.
If issues found → back to `In progress`, fix, push, return here.

- [ ] **Step 1.13: Move CAR-105 to `PR ready`**

Run with `<PRREADY_ID>`:
```powershell
$mut = 'mutation { issueUpdate(id: "CAR-105", input: { stateId: "<PRREADY_ID>" }) { success } }'
$body = @{ query = $mut } | ConvertTo-Json
Invoke-RestMethod -Uri 'https://api.linear.app/graphql' -Method Post -Headers @{ Authorization = $env:LINEAR_API_KEY; 'Content-Type' = 'application/json' } -Body $body
```

- [ ] **Step 1.14: Return to `dev-master`**

Run:
```powershell
git checkout dev-master
```
(Issue stays in `PR ready` until the user merges; merging then transitions it to `Done`. Re-fetch and pull on `dev-master` before starting Task 2 so the next branch starts from a state including any previously-merged tasks.)

---

## Task 2: CAR-106 — Null-check `errorChar` before `canNotify()`

**Linear issue:** CAR-106 — `[F-13] errorChar->canNotify() no null check - crashes on older controller firmware`

**Files:**
- Modify: `lib/NimBLEComm/src/NimBLEClientController.cpp` line 114

- [ ] **Step 2.1: Refresh `dev-master`**

Run:
```powershell
git checkout dev-master
git pull --ff-only origin dev-master
```

- [ ] **Step 2.2: Move CAR-106 to `In progress`**

Run with `<INPROGRESS_ID>`:
```powershell
$mut = 'mutation { issueUpdate(id: "CAR-106", input: { stateId: "<INPROGRESS_ID>" }) { success } }'
$body = @{ query = $mut } | ConvertTo-Json
Invoke-RestMethod -Uri 'https://api.linear.app/graphql' -Method Post -Headers @{ Authorization = $env:LINEAR_API_KEY; 'Content-Type' = 'application/json' } -Body $body
```

- [ ] **Step 2.3: Create branch**

Run:
```powershell
git checkout -b carloshrdezc/car-106-errorchar-nullcheck
```

- [ ] **Step 2.4: Edit `NimBLEClientController.cpp` line 114**

In `lib/NimBLEComm/src/NimBLEClientController.cpp`, change line 114 from:
```cpp
    if (errorChar->canNotify()) {
```
To:
```cpp
    if (errorChar != nullptr && errorChar->canNotify()) {
```
This matches the surrounding style — every other notify characteristic in the same `connectToServer()` function uses this exact pattern (lines 120, 126, 132, 138, 144, 151).

- [ ] **Step 2.5: QA self-review — build**

Run:
```powershell
pio run -e display
```
Expected: build succeeds.

- [ ] **Step 2.6: QA self-review — format**

Run (Git Bash/WSL):
```bash
scripts/format.sh
git diff --exit-code
```
Expected: no formatting drift.

- [ ] **Step 2.7: Move CAR-106 to `QA`**

Run with `<QA_ID>`:
```powershell
$mut = 'mutation { issueUpdate(id: "CAR-106", input: { stateId: "<QA_ID>" }) { success } }'
$body = @{ query = $mut } | ConvertTo-Json
Invoke-RestMethod -Uri 'https://api.linear.app/graphql' -Method Post -Headers @{ Authorization = $env:LINEAR_API_KEY; 'Content-Type' = 'application/json' } -Body $body
```

- [ ] **Step 2.8: Re-read the diff**

Run:
```powershell
git diff
```
Acceptance criteria:
- Exactly one line changed in `lib/NimBLEComm/src/NimBLEClientController.cpp`.
- New line reads `    if (errorChar != nullptr && errorChar->canNotify()) {`.
- No other files changed.

If the diff fails → back to `In progress`, fix, return.

- [ ] **Step 2.9: Commit**

Run:
```powershell
git add lib/NimBLEComm/src/NimBLEClientController.cpp
git commit -m "fix(ble): null-check errorChar before canNotify() (CAR-106)"
```

- [ ] **Step 2.10: Push and open PR**

Run:
```powershell
git push -u origin carloshrdezc/car-106-errorchar-nullcheck
gh pr create --base dev-master --title "fix(ble): null-check errorChar before canNotify() (CAR-106)" --body @"
Fixes CAR-106

`NimBLEClientController::connectToServer()` calls
`errorChar->canNotify()` immediately after
`pRemoteService->getCharacteristic(ERROR_CHAR_UUID)`. Older controller
firmware (pre-`NimBLEServerController.cpp:39`) does not expose that
characteristic, so the lookup returns `nullptr` and the dereference crashes
the display.

Every other notify characteristic in the same function already guards with
`if (X != nullptr && X->canNotify())` — `errorChar` was the lone outlier.
Fix matches the surrounding style verbatim.

Verification: `pio run -e display` clean; `scripts/format.sh` clean. Manual
test against pre-error-char-era controller firmware recommended where
available.

Spec: docs/superpowers/specs/2026-05-20-firmware-lifetime-uaf-cluster-design.md
"@
```

- [ ] **Step 2.11: Move CAR-106 to `Ready for Testing`**

Run with `<R4T_ID>`:
```powershell
$mut = 'mutation { issueUpdate(id: "CAR-106", input: { stateId: "<R4T_ID>" }) { success } }'
$body = @{ query = $mut } | ConvertTo-Json
Invoke-RestMethod -Uri 'https://api.linear.app/graphql' -Method Post -Headers @{ Authorization = $env:LINEAR_API_KEY; 'Content-Type' = 'application/json' } -Body $body
```

- [ ] **Step 2.12: Self-review the PR diff**

Run:
```powershell
gh pr view --web
```
Confirm: one-line diff, correct file, `Fixes CAR-106` in body.

- [ ] **Step 2.13: Move CAR-106 to `PR ready`**

Run with `<PRREADY_ID>`:
```powershell
$mut = 'mutation { issueUpdate(id: "CAR-106", input: { stateId: "<PRREADY_ID>" }) { success } }'
$body = @{ query = $mut } | ConvertTo-Json
Invoke-RestMethod -Uri 'https://api.linear.app/graphql' -Method Post -Headers @{ Authorization = $env:LINEAR_API_KEY; 'Content-Type' = 'application/json' } -Body $body
```

- [ ] **Step 2.14: Return to `dev-master`**

Run:
```powershell
git checkout dev-master
```

---

## Task 3: CAR-101 — Deprecate `Controller::getLastProcess()`

**Linear issue:** CAR-101 — `[F-08] Use-after-free risk in getLastProcess() raw pointer accessor`

**Files:**
- Modify: `src/display/core/Controller.h` line 117

- [ ] **Step 3.1: Refresh `dev-master`**

Run:
```powershell
git checkout dev-master
git pull --ff-only origin dev-master
```

- [ ] **Step 3.2: Move CAR-101 to `In progress`**

Run with `<INPROGRESS_ID>`:
```powershell
$mut = 'mutation { issueUpdate(id: "CAR-101", input: { stateId: "<INPROGRESS_ID>" }) { success } }'
$body = @{ query = $mut } | ConvertTo-Json
Invoke-RestMethod -Uri 'https://api.linear.app/graphql' -Method Post -Headers @{ Authorization = $env:LINEAR_API_KEY; 'Content-Type' = 'application/json' } -Body $body
```

- [ ] **Step 3.3: Create branch**

Run:
```powershell
git checkout -b carloshrdezc/car-101-deprecate-getlastprocess
```

- [ ] **Step 3.4: Verify zero callers (sanity check)**

Run:
```powershell
rg -n "getLastProcess"
```
Expected: exactly one match — the declaration at `src/display/core/Controller.h:117`. If any other match appears, **stop** and update the plan: those callers must be migrated to `getProcessSnapshot()` first or this becomes a behaviour-changing PR.

- [ ] **Step 3.5: Edit `Controller.h` lines 117**

Replace this single line:
```cpp
    Process *getLastProcess() const { return lastProcess; }
```
With:
```cpp
    // DEPRECATED: Direct pointer access is unsafe due to race conditions.
    // Use getProcessSnapshot() or other thread-safe accessor methods instead.
    // This method will be removed in a future version.
    [[deprecated("Use getProcessSnapshot() or thread-safe accessor methods instead")]]
    Process *getLastProcess() const { return lastProcess; }
```
This mirrors the treatment already applied to the sibling `getProcess()` directly above (lines 111-115).

- [ ] **Step 3.6: QA self-review — build**

Run:
```powershell
pio run -e display
```
Expected: build succeeds with no new deprecation warnings (we just confirmed there are no callers in Step 3.4). If a warning surfaces, stop — it means a caller was missed by `rg`, possibly behind a preprocessor flag like `GAGGIMATE_HEADLESS`.

- [ ] **Step 3.7: QA self-review — format**

Run (Git Bash/WSL):
```bash
scripts/format.sh
git diff --exit-code
```
Expected: no formatting drift. (Note: `format.sh` excludes `src/display/ui` and `src/display/drivers` — `Controller.h` is not excluded.)

- [ ] **Step 3.8: Move CAR-101 to `QA`**

Run with `<QA_ID>`:
```powershell
$mut = 'mutation { issueUpdate(id: "CAR-101", input: { stateId: "<QA_ID>" }) { success } }'
$body = @{ query = $mut } | ConvertTo-Json
Invoke-RestMethod -Uri 'https://api.linear.app/graphql' -Method Post -Headers @{ Authorization = $env:LINEAR_API_KEY; 'Content-Type' = 'application/json' } -Body $body
```

- [ ] **Step 3.9: Re-read the diff**

Run:
```powershell
git diff
```
Acceptance criteria:
- One file changed: `src/display/core/Controller.h`.
- One line removed (the bare declaration), four lines added (3-line comment + `[[deprecated(...)]]` attribute + the same declaration).
- The deprecation comment matches the wording used on `getProcess()` above.

- [ ] **Step 3.10: Commit**

Run:
```powershell
git add src/display/core/Controller.h
git commit -m "chore(controller): deprecate getLastProcess() raw-pointer accessor (CAR-101)"
```

- [ ] **Step 3.11: Push and open PR**

Run:
```powershell
git push -u origin carloshrdezc/car-101-deprecate-getlastprocess
gh pr create --base dev-master --title "chore(controller): deprecate getLastProcess() raw-pointer accessor (CAR-101)" --body @"
Fixes CAR-101

`Controller::getLastProcess()` returns a raw `Process *` without taking
`processMutex`. Every other read or write of `lastProcess` in
`Controller.cpp` takes the mutex (lines 346, 799, 823, 826, 989, 1122). The
pointee is `delete`d in `clear()` (button handlers, `activateGrind`,
`onFlush`) and in `deactivate()` (which `delete`s the previous `lastProcess`
then promotes `currentProcess`). A caller holding the returned pointer
across any of those events would read freed memory.

Current callers: zero (confirmed by ripgrep). The bug is latent — an API
foot-gun. This PR mirrors the treatment already applied to the sibling
`getProcess()` directly above: marks the accessor `[[deprecated]]` and
points users at `getProcessSnapshot()`, which already falls back to
`lastProcess` under the mutex.

No behaviour change — compile-time deprecation only.

Verification: `pio run -e display` clean (no new warnings); `scripts/format.sh` clean.

Spec: docs/superpowers/specs/2026-05-20-firmware-lifetime-uaf-cluster-design.md
"@
```

- [ ] **Step 3.12: Move CAR-101 to `Ready for Testing`**

Run with `<R4T_ID>`:
```powershell
$mut = 'mutation { issueUpdate(id: "CAR-101", input: { stateId: "<R4T_ID>" }) { success } }'
$body = @{ query = $mut } | ConvertTo-Json
Invoke-RestMethod -Uri 'https://api.linear.app/graphql' -Method Post -Headers @{ Authorization = $env:LINEAR_API_KEY; 'Content-Type' = 'application/json' } -Body $body
```

- [ ] **Step 3.13: Self-review the PR diff**

Run:
```powershell
gh pr view --web
```
Confirm: only `Controller.h` changed; deprecation comment matches `getProcess()`'s; `Fixes CAR-101` in body.

- [ ] **Step 3.14: Move CAR-101 to `PR ready`**

Run with `<PRREADY_ID>`:
```powershell
$mut = 'mutation { issueUpdate(id: "CAR-101", input: { stateId: "<PRREADY_ID>" }) { success } }'
$body = @{ query = $mut } | ConvertTo-Json
Invoke-RestMethod -Uri 'https://api.linear.app/graphql' -Method Post -Headers @{ Authorization = $env:LINEAR_API_KEY; 'Content-Type' = 'application/json' } -Body $body
```

- [ ] **Step 3.15: Return to `dev-master`**

Run:
```powershell
git checkout dev-master
```

---

## Task 4: CAR-100 — Fix `WebUIPlugin` `ota` use-after-free

**Linear issue:** CAR-100 — `[F-07] WebUIPlugin::stop() deletes ota while loop() still uses it`

**Files:**
- Modify: `src/display/plugins/WebUIPlugin.cpp` (lines 360-376 — only the `if (ota != nullptr) { delete ota; ota = nullptr; }` block is removed)

**Why this is last:** It's the only one with real behavioural change and the most subtle. Doing it after the trivial three means landing those quick wins doesn't wait on review of this one.

- [ ] **Step 4.1: Refresh `dev-master`**

Run:
```powershell
git checkout dev-master
git pull --ff-only origin dev-master
```

- [ ] **Step 4.2: Move CAR-100 to `In progress`**

Run with `<INPROGRESS_ID>`:
```powershell
$mut = 'mutation { issueUpdate(id: "CAR-100", input: { stateId: "<INPROGRESS_ID>" }) { success } }'
$body = @{ query = $mut } | ConvertTo-Json
Invoke-RestMethod -Uri 'https://api.linear.app/graphql' -Method Post -Headers @{ Authorization = $env:LINEAR_API_KEY; 'Content-Type' = 'application/json' } -Body $body
```

- [ ] **Step 4.3: Create branch**

Run:
```powershell
git checkout -b carloshrdezc/car-100-webui-ota-uaf
```

- [ ] **Step 4.4: Verify `ota` is allocated only in `setup()` (sanity check)**

Run:
```powershell
rg -n "ota\s*=\s*new\s+GitHubOTA" src/display/plugins/WebUIPlugin.cpp
```
Expected: exactly one match in `setup()` around line 81. If `start()` (or any other site) also allocates `ota`, **stop** and revisit the design — removing the `delete` would then leak the previous allocation.

- [ ] **Step 4.5: Edit `WebUIPlugin.cpp` `stop()` body**

In `src/display/plugins/WebUIPlugin.cpp`, replace this method body (lines 360-376):
```cpp
void WebUIPlugin::stop() {
    stopRelay();
    if (!serverRunning)
        return;
    server.end();
    ws.closeAll();
    if (dnsServer != nullptr) {
        dnsServer->stop();
        delete dnsServer;
        dnsServer = nullptr;
    }
    if (ota != nullptr) {
        delete ota;
        ota = nullptr;
    }
    serverRunning = false;
}
```
With:
```cpp
void WebUIPlugin::stop() {
    stopRelay();
    if (!serverRunning)
        return;
    server.end();
    ws.closeAll();
    if (dnsServer != nullptr) {
        dnsServer->stop();
        delete dnsServer;
        dnsServer = nullptr;
    }
    // ota is owned by the plugin for its full lifetime: allocated once in
    // setup() and never freed. Freeing it here used to race with reads on
    // the Arduino loop task and the AsyncTCP/WS task (see CAR-100). It also
    // permanently nulled `ota` after the first WiFi cycle since start()
    // never reallocates it.
    serverRunning = false;
}
```
The only behavioural change is that the `if (ota != nullptr) { delete ota; ota = nullptr; }` block is gone. Everything else is identical.

- [ ] **Step 4.6: QA self-review — build**

Run:
```powershell
pio run -e display
```
Expected: build succeeds. Binary should be marginally smaller (4 fewer lines of code; deleter not emitted at this site).

- [ ] **Step 4.7: QA self-review — format**

Run (Git Bash/WSL):
```bash
scripts/format.sh
git diff --exit-code
```
Expected: no formatting drift on the modified file.

- [ ] **Step 4.8: Re-read the diff**

Run:
```powershell
git diff
```
Acceptance criteria:
- One file changed: `src/display/plugins/WebUIPlugin.cpp`.
- One hunk inside `stop()`.
- The `delete ota; ota = nullptr;` block is gone.
- A comment block replaces it explaining why.
- `stop()` still tears down server/ws/dnsServer/relay.
- `setup()` (line ~81) is unchanged — `ota` is still allocated there.

If anything else changed → back to `In progress`, fix, return.

- [ ] **Step 4.9: Move CAR-100 to `QA`**

Run with `<QA_ID>`:
```powershell
$mut = 'mutation { issueUpdate(id: "CAR-100", input: { stateId: "<QA_ID>" }) { success } }'
$body = @{ query = $mut } | ConvertTo-Json
Invoke-RestMethod -Uri 'https://api.linear.app/graphql' -Method Post -Headers @{ Authorization = $env:LINEAR_API_KEY; 'Content-Type' = 'application/json' } -Body $body
```

- [ ] **Step 4.10: Manual hardware QA (best-effort)**

If a flashable display is on hand:
1. Flash the build to the display: `pio run -e display -t upload`.
2. Connect to WiFi, wait for `lastUpdateCheck` to fire (every `UPDATE_CHECK_INTERVAL`; check current value in the file).
3. Confirm "Update available" status surfaces in the WebUI as before.
4. Disconnect WiFi (e.g. disable AP); reconnect.
5. After reconnect, trigger another OTA check — confirm it still works.
   Before this fix, after the first WiFi cycle `ota` was permanently null and
   subsequent checks would null-deref or silently no-op depending on the
   access path. After this fix, it should keep working.

If no hardware available, skip this step and document the gap in the PR
description. The build verification in Step 4.6 still proves no compile
regression.

- [ ] **Step 4.11: Commit**

Run:
```powershell
git add src/display/plugins/WebUIPlugin.cpp
git commit -m "fix(webui): keep ota allocated for plugin lifetime (CAR-100)"
```

- [ ] **Step 4.12: Push and open PR**

Run:
```powershell
git push -u origin carloshrdezc/car-100-webui-ota-uaf
gh pr create --base dev-master --title "fix(webui): keep ota allocated for plugin lifetime (CAR-100)" --body @"
Fixes CAR-100

## The bug

`WebUIPlugin::ota` was allocated once in `setup()` and `delete`d in
`stop()`. Three FreeRTOS tasks read it concurrently with zero
synchronization:

| Task | Access site(s) |
|---|---|
| Arduino main loop | `WebUIPlugin.cpp:144` (`ota->update()`, blocks for seconds–minutes); 157, 158, 160 |
| Arduino WiFi event | `WebUIPlugin.cpp:97` registers `stop()` on `controller:wifi:disconnect`; line 372 deletes |
| AsyncTCP / WebSocket | `WebUIPlugin.cpp:618` (`handleOTASettings`); 1074-1076 (`updateOTAStatus`) |

The `if (!serverRunning) return;` guard at line 152 doesn't close the window
because `stop()` writes `serverRunning = false` *after* the delete (line 375
vs 372).

There was also a latent secondary bug: `start()` never reallocated `ota`, so
after the first WiFi disconnect→reconnect cycle, `ota` was permanently
`nullptr` for the rest of the boot — silently breaking OTA.

## The fix

Treat `ota` as a long-lived singleton owned by the plugin. It's small, has
no WiFi-dependent handles to tear down (HTTPClient is constructed per-call
inside `update()`/`checkForUpdates()`), and was being freed for no good
reason.

Removed the `if (ota != nullptr) { delete ota; ota = nullptr; }` block from
`stop()`. `stop()` retains responsibility for tearing down the HTTP server,
WebSocket, DNS server, and relay. `ota` now lives until reboot.

## Why not a mutex / deferred-stop

- Mutex would have to wrap the multi-minute blocking `ota->update()`
  call — `stop()` would block on it for the entire OTA download, defeating
  the point of running on the WiFi event task.
- Deferred-stop via a flag is more code for the same outcome since the only
  thing `stop()` was doing to `ota` was deleting it, which we're removing.

## Verification

- `pio run -e display` — clean.
- `scripts/format.sh` — clean.
- Manual on hardware (best-effort): WiFi disconnect→reconnect → confirm OTA
  check still works (was previously silently broken).
- Stress (race window during `update()`): hard to reproduce on the bench,
  noted as a gap.

Spec: docs/superpowers/specs/2026-05-20-firmware-lifetime-uaf-cluster-design.md
"@
```

- [ ] **Step 4.13: Move CAR-100 to `Ready for Testing`**

Run with `<R4T_ID>`:
```powershell
$mut = 'mutation { issueUpdate(id: "CAR-100", input: { stateId: "<R4T_ID>" }) { success } }'
$body = @{ query = $mut } | ConvertTo-Json
Invoke-RestMethod -Uri 'https://api.linear.app/graphql' -Method Post -Headers @{ Authorization = $env:LINEAR_API_KEY; 'Content-Type' = 'application/json' } -Body $body
```

- [ ] **Step 4.14: Self-review the PR diff**

Run:
```powershell
gh pr view --web
```
Confirm:
- Only `WebUIPlugin.cpp` changed.
- Only `stop()` is touched; `setup()` is untouched.
- Comment block replaces the deleted block and explains the lifetime contract.
- `Fixes CAR-100` in body.

- [ ] **Step 4.15: Move CAR-100 to `PR ready`**

Run with `<PRREADY_ID>`:
```powershell
$mut = 'mutation { issueUpdate(id: "CAR-100", input: { stateId: "<PRREADY_ID>" }) { success } }'
$body = @{ query = $mut } | ConvertTo-Json
Invoke-RestMethod -Uri 'https://api.linear.app/graphql' -Method Post -Headers @{ Authorization = $env:LINEAR_API_KEY; 'Content-Type' = 'application/json' } -Body $body
```

- [ ] **Step 4.16: Return to `dev-master`**

Run:
```powershell
git checkout dev-master
```

---

## Task 5: Cleanup

- [ ] **Step 5.1: After all four PRs merge, transition each to `Done`**

For each of CAR-100, CAR-101, CAR-105, CAR-106 (substituting the issue ID and `<DONE_ID>` from Task 0):
```powershell
$mut = 'mutation { issueUpdate(id: "CAR-XXX", input: { stateId: "<DONE_ID>" }) { success } }'
$body = @{ query = $mut } | ConvertTo-Json
Invoke-RestMethod -Uri 'https://api.linear.app/graphql' -Method Post -Headers @{ Authorization = $env:LINEAR_API_KEY; 'Content-Type' = 'application/json' } -Body $body
```
Note: if the workspace has the GitHub integration configured, merging a PR
with `Fixes CAR-XXX` may auto-transition the issue. Check Linear first; only
manually transition issues that didn't move.

- [ ] **Step 5.2: Delete merged branches**

Run for each merged branch:
```powershell
git branch -D carloshrdezc/car-105-blescales-dead-guard
git branch -D carloshrdezc/car-106-errorchar-nullcheck
git branch -D carloshrdezc/car-101-deprecate-getlastprocess
git branch -D carloshrdezc/car-100-webui-ota-uaf
git fetch --prune
```

- [ ] **Step 5.3: Remove the local Linear states cache**

Run:
```powershell
Remove-Item -LiteralPath scripts/.linear-states.json -ErrorAction SilentlyContinue
```

---

## Self-Review (post-write)

**Spec coverage check:**

| Spec section | Plan task |
|---|---|
| CAR-100 (WebUIPlugin ota UAF) | Task 4 |
| CAR-101 (getLastProcess deprecation) | Task 3 |
| CAR-105 (BLEScales dead guards) | Task 1 |
| CAR-106 (errorChar null check) | Task 2 |
| AGENTS.md state machine (Backlog→In progress→QA→Ready for Testing→PR ready→Done) | Each task implements all transitions |
| AGENTS.md PR base = `dev-master` | All `gh pr create` calls pass `--base dev-master` |
| AGENTS.md `Fixes CAR-XXX` magic words | Each PR body contains the line |
| AGENTS.md QA = `pio run -e display` + `scripts/format.sh` | Each task has explicit build + format steps |
| Spec ordering: 105 → 106 → 101 → 100 | Task numbering matches |
| Per-issue PR (no bundling) | Each task is a distinct branch + PR |
| Linear setup once, not per-task | Task 0 caches state IDs |

All spec requirements covered.

**Placeholder scan:** No TBDs. State IDs are intentionally placeholders the
engineer fills from Task 0's table — that's a real lookup, not vague hand-waving.

**Type/name consistency:** Branch names follow `carloshrdezc/car-NNN-<slug>`.
Commit prefixes follow `<type>(<scope>): <subject> (CAR-NNN)`. State names
match AGENTS.md verbatim. Linear issue IDs (CAR-100/101/105/106) are
consistent throughout.

Plan passes self-review.
