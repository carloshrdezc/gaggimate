# Firmware `millis()` Correctness Pair Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close two firmware `millis()` correctness bugs (CAR-103 rollover, CAR-102 timestamp persistence) as two separate Linear issues, two separate PRs into `dev-master`.

**Architecture:** Each task is one Linear issue end-to-end: Backlog → In Progress → implement → QA self-review (build + format) → Ready for Testing (PR open) → PR Ready → Done (merge). Sequenced lowest-risk-first: CAR-103 mechanical (~10 LOC firmware) ships first, then CAR-102 substantive (firmware + web + heuristic migration).

**Tech Stack:** ESP32 / PlatformIO / Arduino-ESP32 (firmware); Preact + Vite + Node 18 (web). Build envs: `display` for firmware, `npm run build` in `web/` for web. No unit-test framework — verification is build + manual on hardware.

**Spec:** `docs/superpowers/specs/2026-05-20-millis-correctness-design.md`

---

## File Structure

| Bug     | File(s) touched                                       | LOC delta |
|---------|--------------------------------------------------------|-----------|
| CAR-103 | `src/display/plugins/WebUIPlugin.h` (4 lines)          | 4 / -4    |
| CAR-103 | `src/display/plugins/WebUIPlugin.cpp` (5 lines)        | 5 / -5    |
| CAR-102 | `src/display/core/BeanManager.cpp` (`saveBean` body)   | ~13 / -5  |
| CAR-102 | `src/display/core/BeanManager.h` (`parseBean` body)    | ~10 / 0   |
| CAR-102 | `web/src/utils/beanManager.js` (3 small edits)         | ~12 / -2  |

Tasks don't overlap files between issues.

## Conventions (same as previous cluster)

- **Branch naming:** `carloshrdezc/car-<NNN>-<short-slug>`.
- **Commit format:** `<type>(<scope>): <subject> (CAR-NNN)` where type ∈ `fix|chore|refactor` and scope ∈ `webui|beans|web`.
- **PR base:** `dev-master`.
- **PR repo target:** `carloshrdezc/gaggimate` (the fork) — pass `--repo carloshrdezc/gaggimate` to `gh pr create`. Do NOT target upstream `jniebuhr/gaggimate`.
- **PR body MUST contain** `Fixes CAR-NNN` (auto-closes the Linear issue on merge).
- **Linear state IDs** (cached in Task 0):

  | State              | ID                                       |
  |--------------------|------------------------------------------|
  | Backlog            | `e3d7dd59-9ceb-4c4c-a1ed-9fb311fcec4c`  |
  | In Progress        | `3c2a264e-e4b8-42fb-b5b6-6be39c6351a4`  |
  | QA                 | `14279ed0-2591-4186-bebc-ab2664c83c9f`  |
  | Ready for Testing  | `601b0ea6-3ced-49c9-aeaa-613bb00d8b7a`  |
  | PR Ready           | `a2d8d9ef-2d9c-4966-9ab8-8d93237c472d`  |
  | Done               | `8d87d868-1461-4200-a8d9-97c42f35ab82`  |

  Workflow per issue: `In Progress` (start) → `QA` (after build+format pass) → `Ready for Testing` (after PR open) → `PR Ready` (after self-review of PR diff) → `Done` (auto on merge via `Fixes CAR-NNN`).

- **`scripts/format.sh` is skipped** because clang-format isn't installed in this environment. Visual style match against verbatim plan code is the substitute. Reviewer can run it before merge if desired.

---

## Task 0: Setup

- [ ] **Step 0.1: Verify on `dev-master` with clean tree**

Run:
```powershell
git status; git rev-parse --abbrev-ref HEAD
```
Expected: `On branch dev-master`, `nothing to commit, working tree clean` (the spec doc commit `6c5b5dad` should be on local dev-master). If the tree is dirty, stop and resolve.

- [ ] **Step 0.2: Push spec doc to `dev-master`**

Run:
```powershell
git push origin dev-master 2>&1 | Select-Object -Last 5
```
Expected: a successful push of `6c5b5dad` (the spec commit). This puts the spec on the remote so subsequent feature branches diff cleanly against `origin/dev-master` without dragging the spec into every PR.

If the push is rejected (remote diverged), run:
```powershell
git pull --ff-only origin dev-master
git push origin dev-master 2>&1 | Select-Object -Last 5
```

- [ ] **Step 0.3: Confirm baseline build passes**

Run:
```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e display 2>&1 | Select-Object -Last 10
```
Expected: `SUCCESS`. Note the flash size for comparison: should be in the ballpark of 5276157 bytes (post-CAR-100 baseline). If baseline fails, stop.

- [ ] **Step 0.4: Confirm web baseline build passes**

CAR-102 touches the web side, so verify the web baseline now to catch any pre-existing breakage.

Run:
```powershell
cd web; npm ci 2>&1 | Select-Object -Last 5
```
If `npm ci` says deps already up to date, that's fine. If it reinstalls, that's also fine.

Then:
```powershell
npm run build 2>&1 | Select-Object -Last 10
cd ..
```
Expected: build succeeds, produces files in `web/dist/`. If web build fails on baseline, stop and resolve.

---

## Task 1: CAR-103 — Rollover-safe `millis()` comparisons in `WebUIPlugin`

**Linear issue:** CAR-103 — `[F-10] millis() rollover bugs in WebUIPlugin::loop time math`

**Files:**
- Modify: `src/display/plugins/WebUIPlugin.h` (lines 92-95)
- Modify: `src/display/plugins/WebUIPlugin.cpp` (lines 155, 156, 162, 259, 263)

- [ ] **Step 1.1: Move CAR-103 to `In Progress`**

Run:
```powershell
$mut = 'mutation { issueUpdate(id: "CAR-103", input: { stateId: "3c2a264e-e4b8-42fb-b5b6-6be39c6351a4" }) { success issue { state { name } } } }'
$body = @{ query = $mut } | ConvertTo-Json
Invoke-RestMethod -Uri 'https://api.linear.app/graphql' -Method Post -Headers @{ Authorization = $env:LINEAR_API_KEY; 'Content-Type' = 'application/json' } -Body $body | ConvertTo-Json -Depth 5
```
Expected: `state.name = "In Progress"`.

- [ ] **Step 1.2: Create branch**

Run:
```powershell
git checkout -b carloshrdezc/car-103-millis-rollover
```

- [ ] **Step 1.3: Edit `WebUIPlugin.h` lines 92-95**

In `src/display/plugins/WebUIPlugin.h`, replace:
```cpp
    long lastUpdateCheck = 0;
    long lastStatus = 0;
    long lastCleanup = 0;
    long lastDns = 0;
```
With:
```cpp
    unsigned long lastUpdateCheck = 0;
    unsigned long lastStatus = 0;
    unsigned long lastCleanup = 0;
    unsigned long lastDns = 0;
```
(Adds `unsigned ` to each of the four declarations. Indentation and spacing otherwise unchanged.)

- [ ] **Step 1.4: Edit `WebUIPlugin.cpp` line 155**

In `src/display/plugins/WebUIPlugin.cpp`, change:
```cpp
    const long now = millis();
```
To:
```cpp
    const unsigned long now = millis();
```

- [ ] **Step 1.5: Edit `WebUIPlugin.cpp` line 156**

Change:
```cpp
    if ((lastUpdateCheck == 0 || now > lastUpdateCheck + UPDATE_CHECK_INTERVAL)) {
```
To:
```cpp
    if (lastUpdateCheck == 0 || now - lastUpdateCheck > UPDATE_CHECK_INTERVAL) {
```
(Note: also drops the redundant outer parens that were wrapping the entire `||` expression. Style matches the rest of the file.)

- [ ] **Step 1.6: Edit `WebUIPlugin.cpp` line 162**

Change:
```cpp
    if (now > lastStatus + STATUS_PERIOD && (!ws.getClients().empty() || relayConnected)) {
```
To:
```cpp
    if (now - lastStatus > STATUS_PERIOD && (!ws.getClients().empty() || relayConnected)) {
```

- [ ] **Step 1.7: Edit `WebUIPlugin.cpp` line 259**

Change:
```cpp
    if (now > lastCleanup + CLEANUP_PERIOD) {
```
To:
```cpp
    if (now - lastCleanup > CLEANUP_PERIOD) {
```

- [ ] **Step 1.8: Edit `WebUIPlugin.cpp` line 263**

Change:
```cpp
    if (now > lastDns + DNS_PERIOD && dnsServer != nullptr) {
```
To:
```cpp
    if (now - lastDns > DNS_PERIOD && dnsServer != nullptr) {
```

- [ ] **Step 1.9: QA self-review — build**

Run:
```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e display 2>&1 | Select-Object -Last 12
```
Expected: `SUCCESS`. Flash size near baseline (variation <1 KB). No new compiler warnings about signed/unsigned comparison.

- [ ] **Step 1.10: Re-read the diff**

Run:
```powershell
git diff
```
Acceptance criteria:
- Two files changed: `src/display/plugins/WebUIPlugin.h` (4 lines `long` → `unsigned long`) and `src/display/plugins/WebUIPlugin.cpp` (5 lines).
- All four field types now `unsigned long`.
- `now` declared as `unsigned long` at line 155.
- All four comparisons converted from `now > lastX + INTERVAL` to `now - lastX > INTERVAL` form.
- The `lastUpdateCheck == 0` first-run sentinel preserved at line 156.
- No other files changed.

If the diff fails any criterion → fix and re-check.

- [ ] **Step 1.11: Move CAR-103 to `QA`**

Run:
```powershell
$mut = 'mutation { issueUpdate(id: "CAR-103", input: { stateId: "14279ed0-2591-4186-bebc-ab2664c83c9f" }) { success issue { state { name } } } }'
$body = @{ query = $mut } | ConvertTo-Json
Invoke-RestMethod -Uri 'https://api.linear.app/graphql' -Method Post -Headers @{ Authorization = $env:LINEAR_API_KEY; 'Content-Type' = 'application/json' } -Body $body | ConvertTo-Json -Depth 5
```

- [ ] **Step 1.12: Commit**

Run:
```powershell
git add src/display/plugins/WebUIPlugin.h src/display/plugins/WebUIPlugin.cpp
git commit -m "fix(webui): use rollover-safe millis() comparisons (CAR-103)"
```

- [ ] **Step 1.13: Push branch**

Run:
```powershell
git push -u origin carloshrdezc/car-103-millis-rollover 2>&1 | Select-Object -Last 5
```

- [ ] **Step 1.14: Open PR**

Run:
```powershell
$body = @'
Fixes CAR-103

`WebUIPlugin` had two compounding `millis()` defects:

1. **Signed `long` storage.** Four timing fields in `WebUIPlugin.h` were
   declared `long` (signed). The local `const long now = millis();` at
   `WebUIPlugin.cpp:155` reinterpreted `millis()` (`unsigned long`) as
   signed; at ~24.85 days uptime the high bit becomes a sign bit and `now`
   goes negative.

2. **Forward-addition comparisons.** Lines 156, 162, 259, 263 used
   `now > lastX + INTERVAL` instead of the rollover-safe
   `now - lastX > INTERVAL`. With `INTERVAL` declared `constexpr size_t`,
   the implicit conversion chain misbehaved near both signed and unsigned
   overflow boundaries.

After ~24.85 days uptime the four guarded blocks (OTA check, WS status
broadcast, WS cleanup, captive-portal DNS) either spammed every loop tick
(CPU thrash, WS clients evicted) or froze entirely until reboot. After
~49.7 days the failure mode flipped.

Fix matches the established pattern used by every other plugin in the
codebase (`BLEScalePlugin.cpp:110`, `AutoWakeupPlugin.cpp:34`,
`ShotHistoryPlugin.cpp:512`, etc.):
- Storage type → `unsigned long`.
- Comparison → `now - lastX > INTERVAL` (relies on well-defined unsigned
  modular arithmetic for rollover safety).

Verification: `pio run -e display` clean. Reproducing the bug requires 25+
days uptime on hardware, so the verification reduces to a code review
against the established codebase pattern.

Spec: `docs/superpowers/specs/2026-05-20-millis-correctness-design.md`
'@
gh pr create --repo carloshrdezc/gaggimate --base dev-master --head carloshrdezc/car-103-millis-rollover --title "fix(webui): use rollover-safe millis() comparisons (CAR-103)" --body $body
```
Expected: a printed PR URL like `https://github.com/carloshrdezc/gaggimate/pull/<N>`.

- [ ] **Step 1.15: Verify PR is clean**

Run:
```powershell
gh pr view --repo carloshrdezc/gaggimate --json files,additions,deletions,baseRefName | ConvertFrom-Json | Format-List
```
Acceptance: exactly 2 files changed (`src/display/plugins/WebUIPlugin.h` and `src/display/plugins/WebUIPlugin.cpp`), <15 total LOC, base `dev-master`. If extra files appear (e.g. spec doc), abort: that means Task 0 Step 0.2 didn't actually push.

- [ ] **Step 1.16: Move CAR-103 to `Ready for Testing`**

Run:
```powershell
$mut = 'mutation { issueUpdate(id: "CAR-103", input: { stateId: "601b0ea6-3ced-49c9-aeaa-613bb00d8b7a" }) { success issue { state { name } } } }'
$body = @{ query = $mut } | ConvertTo-Json
Invoke-RestMethod -Uri 'https://api.linear.app/graphql' -Method Post -Headers @{ Authorization = $env:LINEAR_API_KEY; 'Content-Type' = 'application/json' } -Body $body | ConvertTo-Json -Depth 5
```

- [ ] **Step 1.17: Self-review the PR diff**

Same as Step 1.10 but at the PR level — reread the diff in the GitHub UI or via `gh pr diff`. Confirm:
- Diff matches intent.
- No accidental file additions.
- PR title and `Fixes CAR-103` line present.

- [ ] **Step 1.18: Move CAR-103 to `PR Ready`**

Run:
```powershell
$mut = 'mutation { issueUpdate(id: "CAR-103", input: { stateId: "a2d8d9ef-2d9c-4966-9ab8-8d93237c472d" }) { success issue { state { name } } } }'
$body = @{ query = $mut } | ConvertTo-Json
Invoke-RestMethod -Uri 'https://api.linear.app/graphql' -Method Post -Headers @{ Authorization = $env:LINEAR_API_KEY; 'Content-Type' = 'application/json' } -Body $body | ConvertTo-Json -Depth 5
```

- [ ] **Step 1.19: Return to `dev-master`**

Run:
```powershell
git checkout dev-master
```

(Wait for user to merge PR. Once merged, sync local dev-master before starting Task 2: `git pull --ff-only origin dev-master`. The `Fixes CAR-103` magic words auto-transition the issue to `Done`.)

---

## Task 2: CAR-102 — `BeanManager` Unix-seconds timestamps + legacy migration

**Linear issue:** CAR-102 — `[F-09] BeanManager millis() timestamps break across reboots`

**Files:**
- Modify: `src/display/core/BeanManager.cpp` (`saveBean` body, lines 78-82)
- Modify: `src/display/core/BeanManager.h` (`parseBean` body, before line 47 return)
- Modify: `web/src/utils/beanManager.js` (lines 47, 59-60, 204)

- [ ] **Step 2.1: Sync `dev-master`**

Run:
```powershell
git checkout dev-master
git pull --ff-only origin dev-master
git log --oneline -3
```
The CAR-103 merge commit should appear in the log if Task 1 was merged.

- [ ] **Step 2.2: Move CAR-102 to `In Progress`**

Run:
```powershell
$mut = 'mutation { issueUpdate(id: "CAR-102", input: { stateId: "3c2a264e-e4b8-42fb-b5b6-6be39c6351a4" }) { success issue { state { name } } } }'
$body = @{ query = $mut } | ConvertTo-Json
Invoke-RestMethod -Uri 'https://api.linear.app/graphql' -Method Post -Headers @{ Authorization = $env:LINEAR_API_KEY; 'Content-Type' = 'application/json' } -Body $body | ConvertTo-Json -Depth 5
```

- [ ] **Step 2.3: Create branch**

Run:
```powershell
git checkout -b carloshrdezc/car-102-bean-unix-seconds
```

- [ ] **Step 2.4: Verify `<ctime>` include status in `BeanManager.cpp`**

Run:
```powershell
rg -n "ctime|<time.h>|time\(" src/display/core/BeanManager.cpp src/display/core/BeanManager.h
```
Expected: no matches in either file. The fix needs `time(&now)` and `struct tm`, so we'll add `#include <ctime>` to `BeanManager.cpp` in Step 2.5.

If matches DO appear, just confirm `<ctime>` (or `<time.h>`) is already included — skip the include addition in Step 2.5.

- [ ] **Step 2.5: Edit `BeanManager.cpp` — add `<ctime>` include**

In `src/display/core/BeanManager.cpp`, the current top is:
```cpp
#include "BeanManager.h"

#include <algorithm>
#include <utility>
```
Replace with:
```cpp
#include "BeanManager.h"

#include <algorithm>
#include <ctime>
#include <utility>
```
(Adds `#include <ctime>` between `<algorithm>` and `<utility>` to maintain alphabetical order.)

- [ ] **Step 2.6: Edit `BeanManager.cpp` — `saveBean` body**

In `src/display/core/BeanManager.cpp`, the current `saveBean` body (lines 78-82) reads:
```cpp
    const unsigned long now = millis();
    if (bean.createdAt == 0) {
        bean.createdAt = now;
    }
    bean.updatedAt = now;
```

Replace with:
```cpp
    // Use NTP-derived Unix seconds (matches ShotHistoryPlugin convention).
    // If NTP has not synced yet, leave timestamps at 0; the next save will
    // backfill via the existing `bean.createdAt == 0` guard.
    time_t now;
    time(&now);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    const bool ntpValid = timeinfo.tm_year > (2020 - 1900);
    const unsigned long nowSec = ntpValid ? static_cast<unsigned long>(now) : 0UL;

    if (bean.createdAt == 0 && nowSec != 0) {
        bean.createdAt = nowSec;
    }
    if (nowSec != 0) {
        bean.updatedAt = nowSec;
    }
```

- [ ] **Step 2.7: Edit `BeanManager.h` — `parseBean` legacy migration**

In `src/display/core/BeanManager.h`, the current `parseBean` body (lines 25-48) reads:
```cpp
inline bool parseBean(const JsonObject &obj, BeanEntry &bean) {
    const String candidateId = obj["id"] | "";
    // Reject IDs containing path separators or other unsafe chars before they
    // reach any filesystem helper. Empty IDs are tolerated here because
    // saveBean() generates one when the field is missing.
    if (!candidateId.isEmpty() && !isSafeId(candidateId)) {
        return false;
    }
    bean.id = candidateId;
    bean.name = obj["name"] | "";
    bean.roaster = obj["roaster"] | "";
    bean.roastLevel = obj["roastLevel"] | "";
    bean.roastDate = obj["roastDate"] | "";
    bean.origin = obj["origin"] | "";
    bean.process = obj["process"] | "";
    bean.notes = obj["notes"] | "";
    bean.quantity = obj["quantity"].is<float>() || obj["quantity"].is<double>() || obj["quantity"].is<int>()
                        ? obj["quantity"].as<float>()
                        : -1.0f;
    bean.archived = obj["archived"] | false;
    bean.createdAt = obj["createdAt"] | 0UL;
    bean.updatedAt = obj["updatedAt"] | 0UL;
    return !bean.name.isEmpty();
}
```

Insert the legacy-migration block immediately before the `return` statement. After the edit, the last few lines should look like:
```cpp
    bean.archived = obj["archived"] | false;
    bean.createdAt = obj["createdAt"] | 0UL;
    bean.updatedAt = obj["updatedAt"] | 0UL;

    // Migrate legacy millis()-based timestamps written by pre-CAR-102 firmware.
    // Any value below 2023-11-14 (1700000000 Unix seconds) is too small to be
    // a real Unix timestamp for this project's lifetime — treat as corrupt
    // and reset to 0. The next saveBean() will backfill from NTP via the
    // existing `bean.createdAt == 0` guard. This is an in-memory correction;
    // the file on disk is rewritten on next save.
    constexpr unsigned long LEGACY_TIMESTAMP_THRESHOLD = 1700000000UL;
    if (bean.createdAt != 0 && bean.createdAt < LEGACY_TIMESTAMP_THRESHOLD) {
        bean.createdAt = 0;
    }
    if (bean.updatedAt != 0 && bean.updatedAt < LEGACY_TIMESTAMP_THRESHOLD) {
        bean.updatedAt = 0;
    }

    return !bean.name.isEmpty();
}
```

- [ ] **Step 2.8: Edit `web/src/utils/beanManager.js` — three edits**

Edit A — `normalizeBeanPayload` constructor at line 47.

Replace:
```js
function normalizeBeanPayload(beanInput = {}) {
  const now = Date.now();
```
With:
```js
function normalizeBeanPayload(beanInput = {}) {
  // Unix seconds, matching the firmware's ShotHistoryPlugin / BeanManager
  // convention (see CAR-102). Display code that needs a JS Date should do
  // `new Date(value * 1000)`, mirroring how shot timestamps are handled
  // elsewhere in this file (line ~370).
  const now = Math.floor(Date.now() / 1000);
```

Edit B — legacy localStorage migration in `normalizeBeanPayload`. The current
lines 59-60 read:
```js
    createdAt: Number(beanInput.createdAt) || now,
    updatedAt: Number(beanInput.updatedAt) || now,
```

Replace with:
```js
    createdAt: normalizeBeanTimestamp(beanInput.createdAt, now),
    updatedAt: normalizeBeanTimestamp(beanInput.updatedAt, now),
```

Then add a helper function immediately above `normalizeBeanPayload` (i.e.,
between `parseQuantity` and `normalizeBeanPayload`):

```js
// Legacy beans saved before CAR-102 used Date.now() (Unix milliseconds).
// New format is Unix seconds. Detect ms-scale values (anything > Sep-2033
// in seconds, which equals Jan-1970 in ms) and convert.
const LEGACY_BEAN_TIMESTAMP_THRESHOLD = 2000000000; // 2033-05-18 in seconds, 1970-01-24 in ms
function normalizeBeanTimestamp(value, fallback) {
  const numeric = Number(value);
  if (!Number.isFinite(numeric) || numeric <= 0) return fallback;
  // Heuristic: values above the threshold must be milliseconds (no record in
  // this project has a future Unix-seconds timestamp that large). Convert.
  if (numeric > LEGACY_BEAN_TIMESTAMP_THRESHOLD) {
    return Math.floor(numeric / 1000);
  }
  return numeric;
}
```

Edit C — `saveBean` write at line 204.

Replace:
```js
    updatedAt: Date.now(),
```
With:
```js
    updatedAt: Math.floor(Date.now() / 1000),
```

The sort comparator at line 67 (`sortBeans`) needs no change — direction is the same regardless of unit.

The `recordBeanSelection` `selectedAtMs` field (line 342) is unchanged: it's a localStorage-only event for "what bean was selected for which profile-shot pair", separate from bean timestamps. Its semantics stay as Unix ms because it's never written to or read from the device.

The `resolveSelectionEventForShot` line 368 (`Number(shot?.timestamp || 0) * 1000`) is unchanged: shots already use Unix seconds, this converts to ms for `Date` consumption — same direction we want for beans.

- [ ] **Step 2.9: QA self-review — firmware build**

Run:
```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e display 2>&1 | Select-Object -Last 12
```
Expected: `SUCCESS`. Flash size will be slightly larger (~+200 bytes for the new `time()` + `localtime_r` linkage if not already pulled in).

- [ ] **Step 2.10: QA self-review — web build**

Run:
```powershell
cd web; npm run build 2>&1 | Select-Object -Last 10; cd ..
```
Expected: build succeeds. No new warnings about the `normalizeBeanTimestamp` helper.

- [ ] **Step 2.11: Re-read the diff**

Run:
```powershell
git diff
```
Acceptance criteria:
- Three files changed:
  - `src/display/core/BeanManager.cpp`: `<ctime>` include + `saveBean` body rewritten with NTP-aware logic.
  - `src/display/core/BeanManager.h`: `parseBean` gains the legacy-migration block before `return`.
  - `web/src/utils/beanManager.js`: `normalizeBeanTimestamp` helper added, `normalizeBeanPayload` uses it for `createdAt`/`updatedAt`, `now` switched to seconds, `saveBean` `updatedAt` switched to seconds.
- No other files changed.
- No accidental edits to `BeanManager.cpp::listBeans` or `loadBean` (the migration goes through `parseBean`).
- No edits to web sort comparator at line 67.
- No edits to `recordBeanSelection` at line 342 (`selectedAtMs` stays as ms).

- [ ] **Step 2.12: Move CAR-102 to `QA`**

Run:
```powershell
$mut = 'mutation { issueUpdate(id: "CAR-102", input: { stateId: "14279ed0-2591-4186-bebc-ab2664c83c9f" }) { success issue { state { name } } } }'
$body = @{ query = $mut } | ConvertTo-Json
Invoke-RestMethod -Uri 'https://api.linear.app/graphql' -Method Post -Headers @{ Authorization = $env:LINEAR_API_KEY; 'Content-Type' = 'application/json' } -Body $body | ConvertTo-Json -Depth 5
```

- [ ] **Step 2.13: Manual verification (best-effort)**

If a flashable display is on hand:
1. Flash: `pio run -e display -t upload`.
2. Reboot fresh, ensure Wi-Fi connects and NTP syncs (give it ~30 s).
3. Add a test bean via the web UI. Note the wall-clock time.
4. Reboot the device.
5. Reload the bean list. Confirm the test bean is still present and its sort
   order is consistent with the wall-clock time you noted.
6. Inspect the JSON file via the file system (if accessible):
   `cat /spiffs/data/<beanId>.json`. The `createdAt`/`updatedAt` should be
   a value > 1700000000 (Unix seconds in the 1.7e9 range, not a small
   `millis()` value).
7. **Legacy data path:** if any existing bean files predate this fix, load
   the device, list beans, and confirm the parser silently zeroes out the
   stale timestamps. After editing the bean, save and re-inspect the file —
   the new value should be a real Unix seconds timestamp.
8. **Web localStorage path:** open the web UI in a browser that has legacy
   localStorage beans (visible in DevTools → Application → Local Storage →
   `gaggimate-beans`). Reload. Confirm the entries are normalized to seconds
   on read (e.g. `1747756800000` → `1747756800`).

If hardware/legacy data isn't available, skip the relevant subset and note
the gap in the PR description.

- [ ] **Step 2.14: Commit**

Run:
```powershell
git add src/display/core/BeanManager.cpp src/display/core/BeanManager.h web/src/utils/beanManager.js
git commit -m "fix(beans): persist Unix-seconds timestamps; migrate legacy millis values (CAR-102)"
```

- [ ] **Step 2.15: Push branch**

Run:
```powershell
git push -u origin carloshrdezc/car-102-bean-unix-seconds 2>&1 | Select-Object -Last 5
```

- [ ] **Step 2.16: Open PR**

Run:
```powershell
$body = @'
Fixes CAR-102

## The bug

`BeanManager::saveBean()` wrote `millis()` (uptime since boot) into JSON
files persisted on SPIFFS. Three concrete failure modes:

1. **Sort order corrupted every reboot.** Both firmware (`BeanManager.cpp:44`)
   and web (`beanManager.js:67`) sort by `updatedAt DESC`. After reboot, beans
   saved post-reboot had small `millis()` values; pre-reboot beans had huge
   values → fresh beans sorted to the bottom.
2. **"Last updated" rendered as 1970.** Web normalizer treated values as
   `Date.now()`-style ms; `new Date(12345678)` = 1970-01-01.
3. **Inconsistent units within a single record.** Web `saveBean` wrote
   `Date.now()` (Unix ms) into `updatedAt` while device `createdAt` returned
   `millis()`-since-some-past-boot. Same JSON object, different units.

## The fix

Wire format and disk format both become **Unix seconds** stored as unsigned
32-bit integers. Matches `ShotHistoryPlugin::getTime()` (the canonical
"persisted timestamp" pattern in this codebase).

### Firmware

- `BeanManager::saveBean()` calls `time(&now)`; uses NTP-aware logic. If
  NTP hasn''t synced (`tm_year <= 2020`), timestamps stay at sentinel `0`
  and are backfilled on next save once NTP is up.
- `parseBean()` migrates legacy values: any timestamp below 1700000000
  (2023-11-14 — well after the project''s first release) is treated as a
  corrupt `millis()` artifact and reset to `0`. Sort puts these at the
  bottom for one boot until the user edits the bean and the new save writes
  a real Unix-seconds value.
- Adds `#include <ctime>` to `BeanManager.cpp`.

### Web

- `normalizeBeanPayload` now constructs `now` as `Math.floor(Date.now() / 1000)`.
- New `normalizeBeanTimestamp` helper handles the localStorage migration: any
  value above `2000000000` (2033 in seconds, 1970+24d in ms) is treated as
  legacy ms and divided by 1000.
- `saveBean` writes `updatedAt: Math.floor(Date.now() / 1000)`.
- Sort comparator (`beanManager.js:67`) is unchanged — direction is the same
  regardless of unit.
- `recordBeanSelection.selectedAtMs` (line 342) is intentionally unchanged:
  it''s a localStorage-only profile/shot-selection event with its own
  semantics, never written to or read from the device.

### Future date-display code (out of scope here)

Any future UI that formats `bean.updatedAt` for display must do
`new Date(bean.updatedAt * 1000)`, matching the existing pattern at
`beanManager.js:368` for shot timestamps.

## Verification

- `pio run -e display` clean.
- `cd web && npm run build` clean.
- Manual on hardware (best-effort, see PR description above for steps).
- Legacy data: existing devices'' bean sort scrambles for one boot post-update
  for beans whose values get reset, then becomes correct on next user edit.
  Acceptable per the migration design.

## Risk notes

- 2106 problem in `unsigned long` (32-bit). Pre-existing in
  ShotHistoryPlugin; no regression.
- No-NTP devices (no Wi-Fi ever) cannot timestamp beans. Existing sentinel-0
  sort behavior makes this a soft degradation.

Spec: `docs/superpowers/specs/2026-05-20-millis-correctness-design.md`
'@
gh pr create --repo carloshrdezc/gaggimate --base dev-master --head carloshrdezc/car-102-bean-unix-seconds --title "fix(beans): persist Unix-seconds timestamps; migrate legacy millis values (CAR-102)" --body $body
```
Capture the printed PR URL.

- [ ] **Step 2.17: Verify PR is clean**

Run:
```powershell
gh pr view --repo carloshrdezc/gaggimate --json files,additions,deletions,baseRefName | ConvertFrom-Json | Format-List
```
Acceptance: 3 files changed, base `dev-master`. Files should be `BeanManager.cpp`, `BeanManager.h`, `web/src/utils/beanManager.js`. If extras appear, abort.

- [ ] **Step 2.18: Move CAR-102 to `Ready for Testing`**

Run:
```powershell
$mut = 'mutation { issueUpdate(id: "CAR-102", input: { stateId: "601b0ea6-3ced-49c9-aeaa-613bb00d8b7a" }) { success issue { state { name } } } }'
$body = @{ query = $mut } | ConvertTo-Json
Invoke-RestMethod -Uri 'https://api.linear.app/graphql' -Method Post -Headers @{ Authorization = $env:LINEAR_API_KEY; 'Content-Type' = 'application/json' } -Body $body | ConvertTo-Json -Depth 5
```

- [ ] **Step 2.19: Self-review the PR diff**

Run:
```powershell
gh pr diff --repo carloshrdezc/gaggimate
```
Confirm:
- All four edits (firmware include, firmware saveBean body, firmware parseBean migration, web normalizer + helper + saveBean) are present and match the plan.
- No accidental edits to `recordBeanSelection`, `sortBeans`, or unrelated functions.
- `Fixes CAR-102` in PR body.

- [ ] **Step 2.20: Move CAR-102 to `PR Ready`**

Run:
```powershell
$mut = 'mutation { issueUpdate(id: "CAR-102", input: { stateId: "a2d8d9ef-2d9c-4966-9ab8-8d93237c472d" }) { success issue { state { name } } } }'
$body = @{ query = $mut } | ConvertTo-Json
Invoke-RestMethod -Uri 'https://api.linear.app/graphql' -Method Post -Headers @{ Authorization = $env:LINEAR_API_KEY; 'Content-Type' = 'application/json' } -Body $body | ConvertTo-Json -Depth 5
```

- [ ] **Step 2.21: Return to `dev-master`**

Run:
```powershell
git checkout dev-master
```

(Wait for user to merge. `Fixes CAR-102` magic words auto-transition the issue to `Done` on merge.)

---

## Task 3: Cleanup

- [ ] **Step 3.1: After both PRs merge, sync `dev-master`**

Run:
```powershell
git checkout dev-master
git fetch origin --prune
git pull --ff-only origin dev-master
```

- [ ] **Step 3.2: Delete merged branches**

Run:
```powershell
git branch -D carloshrdezc/car-103-millis-rollover 2>&1
git branch -D carloshrdezc/car-102-bean-unix-seconds 2>&1
```

- [ ] **Step 3.3: Verify Linear states**

Run:
```powershell
$ids = @("CAR-102", "CAR-103")
foreach ($id in $ids) {
  $query = 'query { issue(id: "' + $id + '") { identifier state { name } } }'
  $body = @{ query = $query } | ConvertTo-Json
  $response = Invoke-RestMethod -Uri 'https://api.linear.app/graphql' -Method Post -Headers @{ Authorization = $env:LINEAR_API_KEY; 'Content-Type' = 'application/json' } -Body $body
  "$($response.data.issue.identifier): $($response.data.issue.state.name)"
}
```
Expected: both `Done`. If either is still in `PR Ready` (i.e. the GitHub
integration didn't pick up the magic words), manually transition with:
```powershell
$mut = 'mutation { issueUpdate(id: "CAR-XXX", input: { stateId: "8d87d868-1461-4200-a8d9-97c42f35ab82" }) { success } }'
```

---

## Self-Review (post-write)

**Spec coverage:**

| Spec section                                 | Plan task        |
|----------------------------------------------|-------------------|
| CAR-103 root cause + fix                     | Task 1 Steps 1.3-1.8 |
| CAR-103 verification                         | Task 1 Steps 1.9-1.10 |
| CAR-102 firmware `saveBean` rewrite          | Task 2 Steps 2.5-2.6 |
| CAR-102 NTP-not-synced sentinel              | Task 2 Step 2.6 (`ntpValid` branch) |
| CAR-102 firmware legacy migration in parseBean | Task 2 Step 2.7 |
| CAR-102 web normalizer → seconds             | Task 2 Step 2.8 Edit A |
| CAR-102 web localStorage migration           | Task 2 Step 2.8 Edit B (helper added — this is BEYOND the spec, see note below) |
| CAR-102 web saveBean → seconds               | Task 2 Step 2.8 Edit C |
| CAR-102 verification                         | Task 2 Steps 2.9-2.13 |
| AGENTS.md state machine                      | Each task implements all transitions |
| AGENTS.md PR base = `dev-master`             | All `gh pr create` calls pass `--base dev-master` |
| AGENTS.md PR fork target                     | All `gh pr create` calls pass `--repo carloshrdezc/gaggimate` |
| AGENTS.md `Fixes CAR-XXX` magic words        | Each PR body contains the line |
| Spec ordering: CAR-103 → CAR-102             | Task numbering matches |

**Spec-coverage divergence noted:** the web localStorage migration helper
(`normalizeBeanTimestamp`) is added in this plan but was not explicitly
called out in the spec. The spec covers the *firmware* legacy migration in
`parseBean`. During plan-writing I read `web/src/utils/beanManager.js`
end-to-end and realized the same migration concern applies to localStorage:
existing users have legacy-format `Date.now()` ms values cached locally.
Without the web-side helper, those would silently degrade after this patch
(values like `1747756800000` would feed `new Date(1747756800000 * 1000)` =
year 57334 in any future date-display UI). The helper closes that gap with
the same threshold-based heuristic as the firmware. This is a tighter
fix than the spec called for and is in the same spirit.

**Placeholder scan:** no TBDs. The "if a flashable display is on hand"
caveat in Step 2.13 is honest about hardware availability, not a placeholder.

**Type/name consistency:**
- Branch names: `carloshrdezc/car-NNN-<slug>` — matches.
- Commit prefixes: `<type>(<scope>): <subject> (CAR-NNN)` — matches.
- Linear state names match the IDs in the table at the top.
- `LEGACY_TIMESTAMP_THRESHOLD` is `1700000000UL` in firmware (seconds, 2023-11-14).
- `LEGACY_BEAN_TIMESTAMP_THRESHOLD` is `2000000000` in web (different threshold:
  used to detect ms-scale values, not seconds-scale). Names are deliberately
  different to reflect the different semantics. Both are documented inline.
- `nowSec` (firmware) and `now` (web) are both Unix seconds at point of use.

Plan passes self-review.
