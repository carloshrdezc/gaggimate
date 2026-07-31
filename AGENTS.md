# AGENTS.md

This file provides guidance to agents when working with code in this repository.

> **Canonical source.** This is the single canonical guidance file for coding agents in this repo. `CLAUDE.md` is a pointer to this file — do not duplicate content there. Keep all agent guidance here.

## Project Overview

GaggiMate is an ESP32-based smart controller for Gaggia espresso machines. It consists of three components:
- **Firmware** (C++/Arduino) — display unit and controller board
- **Web UI** (Preact + Vite) — embedded directly into the ESP32 app image, served over Wi-Fi
- **Relay Server** (Node.js) — WebSocket relay for remote access, deployable to Cloudflare Workers

## Linear Workflow (Mandatory)

**Every task — feature, bug, refactor, chore, brainstorm, research, doc update, investigation — gets a Linear issue. No exceptions.** Use the Linear skill for all tooling decisions; this section defines project-specific rules.

### Defaults
- **Team**: coding work lives under team `PRO` (key `PRO`) — use it without asking. (Migration 2026-06: coding issues moved from team `CAR` to team `PRO`; the old `CAR` team now holds personal/non-coding issues only. Historical `CAR-NNN` citations below are preserved as provenance.)
- **Project**: match by name to the GitHub repo. The default project for this repo is `gaggimate`. Resolution rules:
  1. Look for an exact case-insensitive match on the GitHub repo name (`gaggimate`).
  2. If no exact match, pick the closest existing project by name (e.g. `Gaggimate`, `gaggimate-firmware`).
  3. If still no match, **automatically create** a new project named after the GitHub repo (`gaggimate`) — do not ask first, do not block on linking it to an initiative. Link to an initiative later if needed.
- **Labels**: required per Linear skill — exactly one type (`feature`/`bug`/`refactor`/`chore`/`spike`) plus 1-2 domain labels (`firmware`, `web`, `ble`, `infrastructure`, `docs`, etc.).
- **Description**: always populate with what, why, and acceptance criteria — even when the user gave only a title. Aim for: a **clear title** describing the outcome not the task (e.g. "BLE scale reconnects after display wake", not "fix ble"); a **goal/problem** statement (why it matters, what breaks today); **implementation notes** (relevant file paths, constraints, API details); and **acceptance criteria** as a checkbox list of concrete, verifiable conditions.
- **Priority**: set honestly — `Urgent` for regressions/safety, `High` for user-facing breakage, `Medium` for planned features, `Low` for polish.

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
| PR merged to main | `Done` |

**Self-review in QA** means: re-read the diff, run the build (`pio run -e display` and `cd web && npm run build`), check formatting (`scripts/format.sh`), verify the change against the issue's acceptance criteria. If anything fails or feels off, transition back to `In Progress`.

### PR ↔ Issue Linkage

**Default base branch for PRs is `dev-master`, not `master`.** All work-branch PRs target `dev-master` first; `master` is updated downstream from `dev-master` (typically by maintainers). When opening a PR with `gh pr create`, pass `--base dev-master`. If a PR was opened against `master` by mistake, retarget it with `gh pr edit <num> --base dev-master`.

Always include Linear magic words in the PR description so the link is automatic:

```
Fixes PRO-123
```

(Replace `123` with the issue number.) Use `Fixes` / `Closes` / `Resolves` for issues that should auto-close on merge; use `Ref PRO-123` for related-but-not-closed issues.

### Workflow Per Task

1. Before touching code, **create the Linear issue** with description, labels, project assignment.
2. Move issue to **In Progress** and begin work.
3. When code is complete, move to **QA** and self-review (build + format + acceptance criteria).
4. If QA fails → **In Progress** → fix → **QA** again. Loop until clean.
5. Open the PR with `Fixes <ISSUE-KEY>` in the description, move issue to **Ready for Testing**.
6. Review the PR diff yourself; if issues found → **In Progress** → fix → push → re-review → **Ready for Testing**.
7. When PR is approved and merge-ready, move issue to **PR Ready**.
8. After merge to main, move issue to **Done**.

### Brainstorming / Research / Spikes

These also get issues — use type label `spike`, leave in `Backlog` until investigation starts, then follow the same In Progress → QA → Done flow. The "deliverable" for a spike issue is the captured findings (comment on the issue or linked doc).

## Build System

**Dual-platform project**: ESP32 firmware (PlatformIO) + Preact web UI (Vite)
- Firmware: `pio run -e display` (display with UI) or `pio run -e display-headless` (no UI)
- Web UI is embedded into the display/headless app image: run `scripts/build_webui.sh` (it runs `npm ci && npm run build` in `web/`, then gzips + packs the bundle into `src/display/webassets/`) before `pio run -e display`. The pre-hook stubs an empty bundle so a bare `pio run -e display` still links without a web build.
- The fresh-install LittleFS filesystem image holds only seed profiles (`data/p`); the web UI is embedded in the app image (via `scripts/build_webui.sh` → `scripts/embed_webui.py`), not shipped on the filesystem.
- Version auto-generated from git tags via `scripts/auto_firmware_version.py` into `src/version.h`

### Build / flash / monitor commands

**Firmware (PlatformIO)**

```bash
# Build
pio run -e display               # LilyGo-T-RGB (main display board)
pio run -e display-headless-8m   # Seeed XIAO ESP32-S3 (8MB)
pio run -e controller            # Controller board

# Flash
pio run -e display -t upload
pio run -e display -t uploadfs   # Write the LittleFS seed-profile filesystem image (data/p); does NOT upload web assets

# Monitor serial
pio device monitor -e display
```

**Web UI**

```bash
cd web/
npm install
npm run dev      # Dev server at http://localhost:5173/
npm run build    # Production build → dist/ (embedded into the app image by scripts/build_webui.sh)
npm run lint     # ESLint auto-fix
npm run format   # Prettier
```

**Relay Server**

```bash
cd relay-server/
npm install
npm start          # Production
npm run dev        # With auto-reload
npm test           # Node.js native test runner
npm run cf:deploy  # Deploy to Cloudflare Workers
```

**Building the embedded web UI**

```bash
bash scripts/build_webui.sh  # Builds web, gzips + packs the bundle into src/display/webassets/ for the firmware app image
```

### CI gates (PRs to dev-master) — run these locally before pushing (CAR-341)

`.github/workflows/ci.yml` runs on every PR to `dev-master`; reproduce it locally with:

```sh
cd web && npm ci && npm run build && cd ..          # web build
pio run -e display                                  # firmware (-Wall -Wextra, no -Werror)
pio test -e native                                  # host unit tests
pio test -e native-sanitize                         # host tests under ASan + UBSan (findings fail CI)
pio check -e display    --fail-on-defect=medium -f "-<*>" -f "+<src/display/>" -f "-<src/display/ui>"   # GATING cppcheck
pio check -e controller --fail-on-defect=medium -f "-<*>" -f "+<src/controller/>"                       # GATING cppcheck
pio run -e native-tidy -t compiledb                  # host analysis compile DB for clang-tidy (PRO-608)
clang-tidy -p . $(python scripts/select_tidy_sources.py compile_commands.json)
python scripts/test_select_tidy_sources.py           # selector regression tests + scope-minimum guard
```

- cppcheck is GATING (the display step lost its old `continue-on-error`).
- clang-tidy (`.clang-tidy`: `bugprone-*` + `cppcoreguidelines-*`) is scoped to
  hand-written logic via the **`[env:native-tidy]`** compile DB — an analysis-only
  host env (PRO-608) that compiles `src/display/core/**` plus the host-shimmable
  `src/display/plugins/**` against the desktop-simulator shim tree, plus
  `test/tidy/tidy_seam_headers.cpp` so the header-only `*Policy.h` seams get a
  translation unit. **Do not generate the DB from `[env:native]`** — that env is
  the unit-test runner and its `build_src_filter` allow-lists 3 files, which is
  how clang-tidy silently analysed almost nothing before PRO-608. Generated UI
  (`src/display/ui/**`) and vendored drivers (`src/display/drivers/**`) are
  excluded. The CI step asserts a minimum selected-file count so a future
  shrinkage fails loudly instead of passing quietly.
- `[env:native-sanitize]` mirrors `[env:native]` plus `-fsanitize=address,undefined`.
- C++ standard stays at **gnu++17** (CAR-340 / `docs/cpp-standard-spike.md`) — do not change it.
- See `CONTRIBUTING.md` "Continuous Integration & Local Checks" for full details.

## Code Formatting

**C++ formatting excludes UI/driver code**: `scripts/format.sh` uses clang-format but explicitly excludes `src/display/ui/**` and `src/display/drivers/**` directories

## Architecture (Structural)

### Communication Layers

```
Web Browser ──WebSocket──► Display ESP32 ──BLE (NimBLE)──► Controller ESP32
                            (AsyncWebServer /ws)              (pump, heater, valve)
```

- **Display ↔ Web**: JSON WebSocket messages with a `tp` field for message type. Spec at `docs/websocket-api.yaml`.
- **Display ↔ Controller**: BLE GATT via NimBLE, implemented in `lib/NimBLEComm/`.
- **External integrations**: MQTT, HomeKit, BLE scales — all as plugins.

### Firmware Structure (`src/`)

- `src/display/core/` — `Controller` (main orchestrator), `BeanManager`, `PluginManager`
- `src/display/ui/` — LVGL screens and widgets, generated from SquareLine Studio (`ui/` project file)
- `src/display/drivers/` — Display hardware abstraction (LilyGo-T-RGB, Amoled, Waveshare)
- `src/display/plugins/` — Optional feature plugins (BLEScalePlugin, MQTTPlugin, HomekitPlugin, BoilerFillPlugin)
- `src/display/models/` — Data models for settings and state
- `src/controller/` — Controller board firmware (hardware I/O only)
- `lib/GaggiMateController/` — Shared controller library (also used by display for BLE comm)

### Process Model

Brew/steam/grind operations inherit from `Process.h`. Active processes: `BrewProcess`, `SteamProcess`, `GrindProcess`, `PumpProcess`, `ManualProcess`. The `Controller` runs one active process at a time and drives the LVGL UI state machine.

### Web UI Structure (`web/src/`)

- `components/` — Preact UI components (with `__tests__/` subdirectories)
- `hooks/` — Custom hooks for WebSocket communication and state
- `pages/` — Page-level components

The web UI communicates exclusively via WebSocket. There is no REST API.

### Plugin System

Plugins implement the `Plugin.h` interface and are registered with `PluginManager`. They receive lifecycle events and can publish/subscribe to machine state. Adding a feature without modifying core code is the intended pattern.

### Key Files

| File | Purpose |
|---|---|
| `platformio.ini` | All build environments and library dependencies |
| `src/display/core/Controller.h/.cpp` | Central coordinator — owns process lifecycle, plugin manager, WebSocket handler |
| `src/display/ui/` | LVGL UI; edit via SquareLine Studio, not by hand |
| `docs/websocket-api.yaml` | AsyncAPI 2.6.0 spec for the WebSocket protocol |
| `scripts/auto_firmware_version.py` | Pre-build script that generates `version.h` from git tags |
| `data/` | LittleFS filesystem root — holds seed profiles (`data/p`) only; the web UI is embedded in the app image, not here |

### Development Notes

- The UI in `src/display/ui/` is generated by SquareLine Studio. Manual edits to generated files will be overwritten. Custom logic belongs in screen event handlers or separate files.
- Multiple PlatformIO environments share source via `build_src_filter` in `platformio.ini`. Board-specific behavior is controlled by compile-time defines.
- The relay server supports both local Node.js and Cloudflare Workers deployment from the same source.

## Architecture (Non-Obvious)

**Plugin-based event system**: Core uses `PluginManager` with string-based event IDs (e.g., `"system:dummy"`) and typed event data (`EventDataEntry` with int/float/string variants). Events support `stopPropagation` flag.

**BeanManager uses millis() timestamps**: `createdAt` and `updatedAt` are stored as `unsigned long` from `millis()`, NOT Unix timestamps. Files stored as `{uuid}.json` in configured directory.

**Profile schema has two types**: `"standard"` vs `"pro"` profiles with different phase structures. Pro profiles support complex pump control objects with `target`, `pressure`, and `flow` fields. Value `-1` means "use current value at phase start".

**WebSocket API uses `tp` field**: All messages have a `tp` (type) field like `"evt:status"`, `"req:profiles:list"`, `"res:profiles:save"`. Request/response pairs use `rid` (request ID) for correlation.

**Controller waits for BLE connection**: `waitingForController` state with 10-second timeout (`CONTROLLER_WAITING_TIMEOUT_MS`). Display can run headless without controller via `GAGGIMATE_HEADLESS` flag.

**Volumetric measurement has grace period**: Bluetooth scale measurements have 1.5-second grace period (`BLUETOOTH_GRACE_PERIOD_MS`) before switching sources between flow estimation and BLE.

## Web UI Specifics

**Preact with signals**: Uses `@preact/signals` for state management, NOT React hooks for global state
- **Signals convention — `computed()` vs `useComputed()`**:
  - **Module-level `computed(...)`**: for shared derived state used by multiple components (created once at module scope, lives outside component lifecycle). Use this for global/cross-component derived signals.
  - **`useComputed(...)` inside a component body**: for per-component reactive subscriptions — the correct pattern when a signal needs to be read inside a component *and* the component should re-render on signal change. Example: `web/src/pages/ShotHistory/HistoryCard.jsx` uses `useComputed` for per-shot comparison state.
- `ApiService` manages WebSocket with exponential backoff (1s to 30s max delay)
- WebSocket auto-reconnects on close/error with `_scheduleReconnect()`

**Shot Analyzer uses IndexedDB**: `IndexedDBService` stores shot data locally. `AnalyzerService` has predictive window of 4 seconds (`PREDICTIVE_WINDOW_MS`) for phase exit detection.

**Extended profiles use adaptive transitions**: Phase transitions can be `"instant"`, `"linear"`, `"ease-in"`, `"ease-out"`, `"ease-in-out"` with optional `adaptive` flag (`true` or `false`).

**When to make persistence firmware-authoritative (CAR-371 / CAR-372)**: Several
web stores (`beanManager.js`, `grinderManager.js`) persist to the device over the
WebSocket *and* mirror into `localStorage` for instant offline availability. The
recurring bug class is the **client trying to predict the device's merge** (dedup,
sort, cap/eviction) and trying to decide what is/isn't already synced with a
one-shot global flag. Rule of thumb for any device-shared store:

- **Make the firmware authoritative** when the data is **shared across clients**
  (and the device's own display), the device is the **natural source of truth**,
  and/or the merge is **non-trivial** (capped list, dedup, server-assigned
  ids/timestamps). The client should *send writes and trust the canonical list the
  device returns* — never model eviction/sort/dedup itself. This is what
  `grinderManager.js` does post-CAR-371: a `*-pending` set of genuinely-unsynced
  offline writes, drained in one batch on the next connected call; the device
  returns the canonical list and the pending set is cleared on success, retried on
  failure. `req:beans:save` / `req:grinders:save` already echo the saved record and
  the device owns the sort (`BeanManager::listBeans` sort, `GrinderManager` cap).
- **Keep it client-local** when the data is **per-browser UX history** with no
  cross-client meaning: theme (`themeManager.js`), dashboard layout
  (`dashboardManager.js`), the IndexedDB shot archive, and the bean
  **selection-event log** (`gaggimate-bean-selection-events` /
  `gaggimate-active-bean-selection`). These intentionally never sync to the device.
- **Anti-pattern to avoid**: a one-shot migration flag gated on
  `deviceList.length === 0` (the bean migration's original shape). It strands
  localStorage records when the device already has *some* data and never retries
  offline writes. Prefer a per-record pending set drained every connected call.

See `grinderManager.js` for the reference implementation; CAR-373 tracks bringing
beans onto the same model.

## Testing

**Unity host tests on the `native` env**: `test/` holds Unity test suites for pure/host-testable logic (e.g. `test_shot_index_metadata`, `test_extended_recording_policy`, `test_profile_validation`, `test_event_system`, `test_volumetric_target`, `test_ble_scale_scan_policy`, `test_diag_log_tee`). Run them with `pio test -e native` (and `pio test -e native-sanitize` under ASan/UBSan — both are gating CI legs). FreeRTOS-level concurrency and on-device behavior are not unit-tested; prefer extracting pure policy logic into a header so it can be host-tested.

## Local Libraries

**Custom libraries in lib/**: `GaggiMateController`, `NimBLEComm`, `NayrodPID`, `ble_ota_dfu` are project-specific libraries with `library.json` manifests. Controller lib depends on PSM library from GitHub.