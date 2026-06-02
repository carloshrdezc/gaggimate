# AGENTS.md

This file provides guidance to agents when working with code in this repository.

## Linear Workflow (Mandatory)

**Every task — feature, bug, refactor, chore, brainstorm, research, doc update, investigation — gets a Linear issue. No exceptions.** Use the Linear skill for all tooling decisions; this section defines project-specific rules.

### Defaults
- **Team**: the workspace's only team (key `CAR`) — use it without asking.
- **Project**: match by name to the GitHub repo. The default project for this repo is `gaggimate`. Resolution rules:
  1. Look for an exact case-insensitive match on the GitHub repo name (`gaggimate`).
  2. If no exact match, pick the closest existing project by name (e.g. `Gaggimate`, `gaggimate-firmware`).
  3. If still no match, **automatically create** a new project named after the GitHub repo (`gaggimate`) — do not ask first, do not block on linking it to an initiative. Link to an initiative later if needed.
- **Labels**: required per Linear skill — exactly one type (`feature`/`bug`/`refactor`/`chore`/`spike`) plus 1-2 domain labels (`firmware`, `web`, `ble`, `infrastructure`, `docs`, etc.).
- **Description**: always populate with what, why, and acceptance criteria — even when the user gave only a title.

### Required State Transitions

The agent must drive the issue through these Linear states as work progresses. Never skip states.

| Trigger | Move issue to |
|---------|---------------|
| Issue created (work not started) | `Backlog` (unassigned) or `Ready` (assigned to me / queued for work) |
| Starting work on the task | `In progress` |
| Implementation finished, self-review begins | `QA` |
| Bugs/regressions found during QA self-review | back to `In progress` (then re-enter `QA` when fixed) |
| QA passes, PR opened | `Ready for Testing` |
| Bugs found during PR review (mine or reviewer's) | back to `In progress` |
| PR approved and ready to merge | `PR ready` |
| PR merged to main | `Done` (then delete the merged branch — see Post-Merge Cleanup) |

**Self-review in QA** means: re-read the diff, run the build (`pio run -e display` and `cd web && npm run build`), check formatting (`scripts/format.sh`), verify the change against the issue's acceptance criteria. If anything fails or feels off, transition back to `In Progress`.

### PR ↔ Issue Linkage

**Default base branch for PRs is `dev-master`, not `master`.** All work-branch PRs target `dev-master` first; `master` is updated downstream from `dev-master` (typically by maintainers). When opening a PR with `gh pr create`, pass `--base dev-master`. If a PR was opened against `master` by mistake, retarget it with `gh pr edit <num> --base dev-master`.

Always include Linear magic words in the PR description so the link is automatic:

```
Fixes CAR-123
```

(Replace `123` with the issue number.) Use `Fixes` / `Closes` / `Resolves` for issues that should auto-close on merge; use `Ref CAR-123` for related-but-not-closed issues.

### Workflow Per Task

1. Before touching code, **create the Linear issue** with description, labels, project assignment.
2. Move issue to **In Progress** and begin work.
3. When code is complete, move to **QA** and self-review (build + format + acceptance criteria).
4. If QA fails → **In Progress** → fix → **QA** again. Loop until clean.
5. Open the PR with `Fixes <ISSUE-KEY>` in the description, move issue to **Ready for Testing**.
6. Review the PR diff yourself; if issues found → **In Progress** → fix → push → re-review → **Ready for Testing**.
7. When PR is approved and merge-ready, move issue to **PR Ready**.
8. After merge to main, move issue to **Done**.
9. **Clean up the merged branch** — see Post-Merge Cleanup below.

### Post-Merge Cleanup (Mandatory)

Once a PR is merged, the work branch is dead weight. Always clean up immediately so the local repo doesn't accumulate stale branches and worktrees.

**Required steps after merge:**

1. **Switch off the merged branch** if you're still on it: `git checkout dev-master`.
2. **Pull latest dev-master**: `git pull --ff-only origin dev-master` (picks up the squash-merge commit).
3. **Remove any worktree** tied to the branch first — Git refuses to delete a branch that is still checked out in a worktree (`fatal: cannot delete branch '<name>' used by worktree at '<path>'`). Run `git worktree list` to find them, then `git worktree remove --force <path>` for each, followed by `git worktree prune` to clean up admin entries.
4. **Delete the local branch**: `git branch -D <branch-name>` (use `-D` because squash-merges look "unmerged" to git ancestry checks).
5. **Prune the remote-tracking ref**: `git fetch --prune origin` (removes `origin/<branch-name>` if GitHub auto-deleted it).

**One-shot bulk cleanup** (safe to run periodically): `git fetch --prune origin` first, then `git worktree list` + `git worktree remove --force <path>` for any worktrees on stale branches, then `git branch -vv | awk '/: gone]/ {print $1}' | xargs -r git branch -D` to delete every local branch whose upstream has been deleted on the remote. Order matters — worktrees first, branch deletes second, otherwise the bulk delete chokes on the worktree-bound ones.

**Rule of thumb:** if the Linear issue is `Done`, the local branch and any associated worktree should not exist. The only long-lived branches are `dev-master` and `master`.

### Brainstorming / Research / Spikes

These also get issues — use type label `spike`, leave in `Backlog` until investigation starts, then follow the same In Progress → QA → Done flow. The "deliverable" for a spike issue is the captured findings (comment on the issue or linked doc).

## Build System

**Dual-platform project**: ESP32 firmware (PlatformIO) + Preact web UI (Vite)
- Firmware: `pio run -e display` (display with UI) or `pio run -e display-headless` (no UI)
- Web UI must be built BEFORE firmware SPIFFS: `cd web && npm ci && npm run build` then run `scripts/build_spiffs.sh`
- Web assets are gzipped and placed in `data/w/` directory for SPIFFS filesystem
- Version auto-generated from git tags via `scripts/auto_firmware_version.py` into `src/version.h`

## Code Formatting

**C++ formatting excludes UI/driver code**: `scripts/format.sh` uses clang-format but explicitly excludes `src/display/ui/**` and `src/display/drivers/**` directories

## Architecture (Non-Obvious)

**Plugin-based event system**: Core uses `PluginManager` with string-based event IDs (e.g., `"system:dummy"`) and typed event data (`EventDataEntry` with int/float/string variants). Events support `stopPropagation` flag.

**BeanManager uses millis() timestamps**: `createdAt` and `updatedAt` are stored as `unsigned long` from `millis()`, NOT Unix timestamps. Files stored as `{uuid}.json` in configured directory.

**Profile schema has two types**: `"standard"` vs `"pro"` profiles with different phase structures. Pro profiles support complex pump control objects with `target`, `pressure`, and `flow` fields. Value `-1` means "use current value at phase start".

**WebSocket API uses `tp` field**: All messages have a `tp` (type) field like `"evt:status"`, `"req:profiles:list"`, `"res:profiles:save"`. Request/response pairs use `rid` (request ID) for correlation.

**Controller waits for BLE connection**: `waitingForController` state with 10-second timeout (`CONTROLLER_WAITING_TIMEOUT_MS`). Display can run headless without controller via `GAGGIMATE_HEADLESS` flag.

**Volumetric measurement has grace period**: Bluetooth scale measurements have 1.5-second grace period (`BLUETOOTH_GRACE_PERIOD_MS`) before switching sources between flow estimation and BLE.

## Web UI Specifics

**Preact with signals**: Uses `@preact/signals` for state management, NOT React hooks for global state
- `ApiService` manages WebSocket with exponential backoff (1s to 30s max delay)
- WebSocket auto-reconnects on close/error with `_scheduleReconnect()`

**Shot Analyzer uses IndexedDB**: `IndexedDBService` stores shot data locally. `AnalyzerService` has predictive window of 4 seconds (`PREDICTIVE_WINDOW_MS`) for phase exit detection.

**Extended profiles use adaptive transitions**: Phase transitions can be `"instant"`, `"linear"`, `"ease-in"`, `"ease-out"`, `"ease-in-out"` with optional `adaptive` flag (0 or 1).

## Testing

**No test framework configured**: `test/` directory exists but contains only PlatformIO boilerplate. No unit tests currently implemented.

## Local Libraries

**Custom libraries in lib/**: `GaggiMateController`, `NimBLEComm`, `NayrodPID`, `ble_ota_dfu` are project-specific libraries with `library.json` manifests. Controller lib depends on PSM library from GitHub.