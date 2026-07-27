# GaggiMate Repository Audit — Maintenance & Improvement Opportunities

- **Issue:** PRO-590 (spike)
- **Date:** 2026-07-27
- **Base commit:** `a6892bd6` on `dev-master`
- **Scope:** Whole-repo health audit — firmware (`src/`), web UI (`web/`), relay
  server (`relay-server/`), CI/infra (`.github/`), docs (`docs/`), toolchain
  manifests. Read-only investigation; the only code change shipped with this
  audit is the doc itself plus any zero-risk one-liners noted below.

> **Methodology note.** Every finding cites a real file/command output inspected
> during this audit. Findings are deduplicated against the PRO team's Linear
> backlog (241 issues reviewed). Where a finding overlaps an existing issue it is
> marked *(overlaps PRO-NNN)* and no duplicate was filed. Priorities map roughly
> to P0/Urgent → P3/Low; per Carlos's spike policy, almost nothing here is
> Urgent — the repo is healthy and actively maintained.

---

## Executive summary

GaggiMate is a **mature, well-maintained** dual-platform project (ESP32 firmware
+ Preact web UI). The engineering hygiene is genuinely strong:

- **CI is comprehensive** — `.github/workflows/ci.yml` gates every PR on: web
  build + ~540 vitest cases, firmware `display` build (`-Wall -Wextra`),
  all-flags-off + single-flag-off + single-driver-family build matrices, native
  host tests under ASan/UBSan, **gating** cppcheck on `src/display/` and
  `src/controller/`, clang-tidy over in-scope logic, and a `display-sim` build
  with the real embedded web bundle.
- **Firmware has a real host-test layer** — 40+ `test/test_*` policy suites
  extracted as testable seams (BLE, OTA, volumetric, standby, settings
  persistence, path traversal, WS reassembly, …).
- **Dependencies are current** — `npm outdated` shows every web dependency's
  declared range already tracks the latest published version; there is no stale
  major sitting unpatched.
- **Recent security work landed** — local auth + narrow CORS (PRO-517), strict
  settings/WS validation (PRO-521), cloud-relay token transport hardening
  (PRO-518).

The findings below are therefore **incremental hardening and maintenance**, not
firefighting. The highest-value cluster is **CI coverage gaps** (lint and
relay-server tests never run in CI, and a Node-major drift in the web deploy
job) — cheap to fix, and each closes a silent-regression hole in an otherwise
well-gated pipeline.

### Top actionable findings (filed as follow-ups)

| # | Finding | Layer | Priority | Issue |
|---|---------|-------|----------|-------|
| 1 | ESLint (`lint:check`) never runs in CI | web/infra | Medium | PRO-591 |
| 2 | relay-server tests never run in CI | infra | Medium | PRO-592 |
| 3 | `deploy-web.yml` builds on Node 20 while all other jobs + `.nvmrc` use Node 22 | infra | Medium | PRO-593 |
| 4 | `web/src/components/Header.jsx` (518 lines) is dead code | web | Low | PRO-594 |
| 5 | `AGENTS.md` and `CLAUDE.md` duplicate ~150 lines of guidance that will drift | docs | Low | PRO-595 |
| 6 | `WebUIPlugin.cpp` is a 2,835-line god-object plugin | firmware | Low | PRO-596 |
| 7 | Two dead visual-only Menu-screen toggles (CAR-279 TODOs) | firmware | Low | PRO-597 |
| 8 | No dependabot/renovate — dependency updates are manual sweeps | infra | Low | PRO-598 |

---

## Firmware code health

- **[Low] `WebUIPlugin.cpp` is a 2,835-line god object.** *(filed: PRO-596)*
  - Evidence: `wc -l src/display/plugins/WebUIPlugin.cpp` → 2835 (next largest is
    `ShotHistoryPlugin.cpp` at 1832, `Controller.cpp` at 1736).
  - Impact: single file concentrates the HTTP server, WebSocket API dispatch,
    OTA orchestration, and relay bridge. High cognitive load, merge-conflict
    magnet, hard to unit-test as a whole. Prior work already extracted seams
    (PRO-11 OTA-intent state machine) — this is the natural continuation.
  - Action: continue carving host-testable modules out of `WebUIPlugin.cpp`
    (WS request router, relay bridge) behind the existing native-test pattern.
  - Note: overlaps the C++ modernization theme (PRO-66/PRO-117) but is a
    distinct decomposition concern, not a language-version bump.

- **[Low] Two dead visual-only toggles on the Menu screen (CAR-279).**
  *(filed: PRO-597)*
  - Evidence: `src/display/ui/default/lvgl/screens/ui_MenuScreen.c:116` &
    `:120` — `ui_event_MenuScreen_brightness` and `ui_event_MenuScreen_scale`
    are empty `(void)e;` handlers with `CAR-279 TODO: wire to real …` comments.
  - Impact: same silent-dead-button UX class the web audit chased in PRO-396 —
    a rendered control that does nothing when tapped.
  - Action: either wire the backlight/scale toggles to real settings/BLE state,
    or hide them until they are functional. (File is under `src/display/ui/`,
    excluded from format/clang-tidy scope, so no analysis gate catches this.)

- **[Mitigation present] NVS static-init pitfall is correctly guarded.**
  - Evidence: `src/display/core/Controller.cpp:82-91` loads settings in
    `setup()` (after `nvs_flash_init()`), *not* in the `Settings` constructor,
    with a PRO-331 comment explaining the static-init race. `Settings.h:59-74`
    documents the same. This is the exact class of bug flagged in prior audits
    and it is handled — no action needed.
  - Residual: `TODO(PRO-494)` at `Controller.cpp:89` notes no post-boot re-init
    path calls `settings.unload()` yet. Latent-only (no current caller); leave
    the TODO, no ticket.

## Web UI code health

- **[Low] `web/src/components/Header.jsx` is dead code (518 lines).**
  *(filed: PRO-594)*
  - Evidence: `grep -rln "from.*components/Header" web/src` (excluding the file
    itself) → 0 hits. Nothing imports or renders it; the real site-wide layout
    is `AppContent` in `web/src/index.jsx` + `PageShell.jsx`. Confirmed still
    unused as of `a6892bd6`, despite surviving the PRO-137 dead-code sweep.
  - Impact: 518 lines of unreachable component + its own FontAwesome import
    graph; misleads contributors into editing the "header" that never renders.
  - Action: delete `Header.jsx` (direct precedent: PRO-224 deleted
    `HomeModeCard.jsx` for exactly this reason). Not bundled into this PR to
    keep the audit PR docs-only.

- **[Low, mitigated] comparison-set persistence TODO.**
  - Evidence: `web/src/state/comparisonShots.js:3` — `TODO: persist to
    sessionStorage … (PRO-467)`.
  - **Already tracked — no new issue.** PRO-467 (Done) filed the TODO
    deliberately; the persistence itself is intentionally deferred future work.

- **[Info] React FontAwesome package in a Preact app.**
  - Evidence: `@fortawesome/react-fontawesome@^3` in `web/package.json:22`,
    imported by 6 components (`Footer.jsx`, `PageShell.jsx`, …).
  - It works (Preact's React-compat handles it) and tests mock it; not a defect.
    Flagged only so a future dependency sweep doesn't "fix" it blindly.

## Test coverage

- **[Medium] relay-server tests are never executed in CI.** *(filed: PRO-592)*
  - Evidence: `relay-server/src/relay-auth.test.js` and `relay.test.js` exist
    with a `"test": "node --test src/*.test.js"` script
    (`relay-server/package.json`), but `grep -rn "relay" .github/workflows/`
    returns **no** workflow that runs them.
  - Impact: the cloud relay handles auth-token transport (hardened in PRO-518);
    its tests can rot or break undetected. A regression in relay auth ships
    green.
  - Action: add a small CI job (`cd relay-server && npm ci && npm test`) on PRs
    touching `relay-server/**`.

- **[Strong, no action] Firmware + web coverage is genuinely good.**
  - 40+ `test/test_*` native policy suites; ~540 web vitest cases across 49
    `*.test.*` files. Both gate CI. This is a healthy baseline.

## Static analysis / build hardening

- **[Low] `-Werror` is deliberately deferred; no tracking issue exists.**
  *(rolled into PRO-591 scope note; not separately filed)*
  - Evidence: `platformio.ini:33-34` — `NOT -Werror on purpose … (see issue:
    "consider -Werror once clean")`. That referenced issue does not exist in the
    PRO backlog (searched).
  - Impact: `-Wall -Wextra` warnings can accumulate silently since they never
    fail the build. Low priority — a conscious tradeoff — but the "consider
    -Werror once clean" intent has no home.
  - Action: track it (folded into the lint-in-CI issue PRO-591 as a stretch
    item, since both are "make the warning/lint surface gating once clean").

- **[Mitigation present] cppcheck + clang-tidy + ASan/UBSan all gate PRs.**
  Evidence: `.github/workflows/ci.yml` `analysis` + `native` jobs. Strong.

## Dependency / toolchain currency

- **[Medium] `deploy-web.yml` pins Node 20; everything else uses Node 22.**
  *(filed: PRO-593)*
  - Evidence: `.github/workflows/deploy-web.yml:28` → `node-version: 20`, while
    `ci.yml`, `build.yml`, `build-beta.yml`, `build-nightly.yml`, `pr-flash.yml`
    all use `node-version: 22` and `.nvmrc` says `lts/jod` (= Node 22).
  - Impact: the public GitHub Pages web UI is built + shipped on a *different
    Node major* than CI validates against. A build that passes CI on Node 22
    can behave differently (or fail) on the Node-20 deploy runner.
  - Action: bump `deploy-web.yml` to `node-version: 22` to match `.nvmrc`/CI.

- **[Low] No automated dependency updates (dependabot/renovate).**
  *(filed: PRO-598)*
  - Evidence: no `.github/dependabot.yml`, `renovate.json`, or
    `.github/renovate.json`. Dependency bumps are manual sweeps (e.g. PRO-254).
  - Impact: security patches and minor bumps rely on someone remembering to run
    a sweep. Low priority given ranges currently track latest, but automating it
    removes the manual-cadence dependency.
  - Action: add a scoped `dependabot.yml` for `web/npm`, `relay-server/npm`, and
    `github-actions`. Pinned pio/platform deps stay manual (intentional).

## Documentation

- **[Low] `AGENTS.md` and `CLAUDE.md` duplicate ~150 lines that will drift.**
  *(filed: PRO-595)*
  - Evidence: `wc -l CLAUDE.md AGENTS.md` → 153 / 159 lines; both are separate
    files (not symlinked) covering overlapping build/CI/architecture guidance.
  - Impact: two sources of truth for the same rules; already a real drift class
    (the CI-gates section is maintained in AGENTS.md and can diverge from
    CLAUDE.md silently).
  - Action: make one canonical (keep `AGENTS.md`, the richer/more current file)
    and reduce the other to a one-line pointer (`See AGENTS.md`), or symlink.

- **[Info] `docs/` is broad but not stale in a harmful way.** Many spike/plan
  docs (`docs/superpowers/plans/*`, `docs/spike-*`) are historical artifacts of
  completed work — appropriate to keep as provenance, not staleness bugs.

## Developer / operational workflow

- **[Medium] ESLint is configured but never runs in CI.** *(filed: PRO-591)*
  - Evidence: `web/eslint.config.js` exists (flat config, ESLint 9) and
    `web/package.json` defines `"lint:check": "eslint src"`, but
    `grep -rn "eslint\|lint" .github/workflows/` finds no job that invokes it
    (the only hit is an unrelated "nothing to lint" echo in the clang-tidy step).
  - Impact: lint violations accumulate silently; formatting/style/`no-unused`
    drift is only caught if a contributor runs `npm run lint` locally. This is
    the same "gate exists but isn't wired to CI" gap PRO-229 fixed for web
    *tests* — the lint half was never done.
  - Action: add `npm run lint:check` to the web CI job (start non-gating /
    `continue-on-error` if the current tree isn't clean, then flip to gating —
    mirrors the `-Werror`/clang-tidy "gate once clean" pattern).

---

## Quick wins

Cheap, low-risk, high-signal — recommended to do first:

1. **PRO-593** — bump `deploy-web.yml` Node 20 → 22 (one-line workflow edit,
   removes a real toolchain-drift footgun on the public deploy).
2. **PRO-594** — delete dead `Header.jsx` (−518 lines, direct PRO-224
   precedent).
3. **PRO-595** — collapse `CLAUDE.md`/`AGENTS.md` duplication to one canonical +
   pointer.
4. **PRO-591** — wire `npm run lint:check` into web CI (non-gating first).

## Reliability / security risks

Nothing here is an active correctness or security defect — the recent security
cluster (PRO-517/518/519/521) closed the real exposures. The residual risks are
**silent-regression holes in CI**, not live bugs:

- **PRO-592** — relay-server (auth-token-handling service) tests don't run in
  CI. A regression in relay auth ships undetected. *Highest reliability value.*
- **PRO-591** — lint never gates; style/correctness lint drift is invisible.
- **PRO-593** — deploy-vs-CI Node-major mismatch can ship a differently-built
  public bundle.

## Strategic / larger investments

- **PRO-596** — decompose `WebUIPlugin.cpp` (2,835 LOC) into host-testable
  modules. Multi-PR effort; continues the PRO-11 extraction pattern. Pairs well
  with the C++ modernization umbrella (PRO-66/PRO-117).
- **PRO-598** — adopt dependabot/renovate to replace manual dependency sweeps.
- **`-Werror` once clean** (tracked under PRO-591) — flip firmware warnings and
  web lint to gating after a cleanup pass; converts "warnings allowed" into
  "warnings blocked", the stated end-state in `platformio.ini`.

---

## Deduplication ledger

Findings that overlap existing issues and were **not** re-filed:

- comparison-set sessionStorage persistence → **PRO-467** (Done; TODO filed
  deliberately).
- Relay server auth/rate-limiting → **PRO-6** (Backlog). Distinct from PRO-592
  (that's a CI-test-coverage gap, not the auth feature itself).
- Dead-component deletion pattern → precedent **PRO-224** (HomeModeCard.jsx);
  Header.jsx (PRO-594) is the same class, still present after PRO-137's sweep.
- C++ modernization / RAII / platform bump → **PRO-66 / PRO-117 / PRO-257**.
  PRO-596 (WebUIPlugin decomposition) is a distinct structural concern.
- Prior architecture/security/UX/perf audit → **PRO-513** (Done). This audit is
  the maintenance/health follow-up, not a re-run.
