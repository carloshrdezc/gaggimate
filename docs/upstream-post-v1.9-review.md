# Upstream post-v1.9.0 commit adoption review (PRO-666)

Epic: PRO-651. Integration branch: `feature/upstream-v1.9-integration`.
Range: `v1.9.0..upstream/master` = `43d2a7ba..e258f330`. That is **45 commits: 27 non-merge + 18 merges**.
Inputs: `docs/upstream-v1.9-preservation-matrix.md` (feature rows F01..F28) and `docs/upstream-v1.9-baseline.md`.

**Coverage: 45/45 commits decided** (27 non-merge rows + 18 merges in section 4).

## 1. Key finding: this range is a long-lived side branch, not 45 new master commits

`upstream/master` descends from `v1.9.0` (`git merge-base --is-ancestor` succeeds), but the
first-parent history of the range is the tip of **`feature/gaggimate-max`**, merged in on
2026-09-29. Its commits date from **2026-02-15**. Master was merged back into the branch 17 times
(the merges in section 4). So:

- Several post-tag SHAs are **re-authored duplicates of commits already in v1.9.0**. They have the
  same patch-id (`git patch-id --stable`) or the same content:
  `a044f229` ≡ `d7c968d7` (Google fonts removal), `87825119` ≡ `10e59159` (breakpoint),
  `4ba389e3` ~ `0d2562be` (per-button programming; `Settings::getButtonBehavior` is already in the tag),
  `f6663cff` → `NtcThermistor.*` is already in the tag via `ee9fe9dc` (#427 gear pump).
- Some items were **added and then removed inside the range**. At `upstream/master`:
  `d47fa11b` CORS middleware is gone (`WebUIPlugin.{h,cpp}` show a net-zero diff vs the tag), and
  the OTA `tag/db` detour (`387f6c98`/`a927f1a7`) was reverted by `ba087be0`/`e258f330`.
  `git apply -R` of those two against the tag tree succeeds, so they are already in the tag.
- **The real net delta** (`git diff --stat v1.9.0 upstream/master`) is **46 files, +1158/-293**:
  the dual-boiler + GaggiMate Max controller support, ADS per-channel rates / 50-60 Hz pressure
  averaging, NanoPb `Capabilities.dual_boiler = 6` + `PROTOCOL_VERSION 5→6`, dual-boiler display
  logic (steam temp, refill, hot-water valve), 6 small web files, `scripts/release.sh`, and the
  `git describe --match "v*"` version fix.

Method, per commit: `git show --stat` plus the key hunks. Then I classified each commit
mechanically against a `v1.9.0` worktree and an `upstream/master` worktree (`git apply --check [-R]`):
IN-TAG / APPLIES / PARTIAL, and whether it SURVIVES at master. I also ran feature greps
(`git grep` of `GM_MAX`, `dualBoiler`, `AsyncCorsMiddleware`, `fonts.googleapis`, `AdcSchedule`,
and others) on both trees.

## 2. Upstream tags and the rebase question

`git ls-remote --tags upstream` ends at `v1.9.0` (`43d2a7ba`). **Nothing is tagged past v1.9.0.**
The side branch had its own `scripts/release.sh` one-off tags, and none of them are pushed.

**Recommendation: do NOT rebase the integration onto `upstream/master`.** Stay on `v1.9.0`.
- The delta is unreleased and was merged the same day as the tag. `ec7f941a` "Fix compat issues"
  and `c741e1b1` "Fix build" show it is still settling.
- It changes pressure-control timing for **every** board, including Carlos's single-boiler Gaggia.
  `DimmedPump` goes from a 30 ms loop to a `PressureControlRate` schedule; `PressureSensor`
  switches to averaged samples; `PressureController::update(mode, fresh, sampleTime, elapsed)`.
  That is a behavior change to validate on hardware, not to absorb mid-port.
- Re-evaluate when upstream tags `v1.9.1`/`v1.10.0`. If that tag contains the block, rebase the
  integration base onto it (or merge it) in one step, owned by PRO-657. It is cheaper then:
  the net diff is concentrated in `lib/GaggiMateController`, which Carlos barely touches (F14 row).

**Exception, adopt now:** the wire-contract part (the NanoPb `dual_boiler` capability + protocol
version 6, section 5). PRO-655 is porting comms right now and must not burn field 6 / version 6.

## 3. Flash impact (measured)

Each env was built at both refs from clean worktrees (`pio run`, same toolchain). The web bundle was
stubbed by the pre-hook, so read these as **deltas**, not as the baseline's absolute 78.6% figure.

| env | v1.9.0 flash | upstream/master flash | delta |
|---|---|---|---|
| `display-headless-8m` | 2,109,001 B | 2,110,533 B | **+1,532 B (+0.05% of 3,342,336 slot)** |
| `controller` | 701,489 B | 713,361 B | **+11,872 B (+0.36%)** |

No post-tag commit grows flash significantly. The headless-8m env stays at about the baseline 78.6%
(+0.05 pp). The largest single contributor is the controller-side dual-boiler/NTC/ADS block
(`b6b34b63`, `46ea5e44`), and that only affects the `controller` image.

## 4. Decision table (27 non-merge commits), grouped by feature

Decision legend:
- **adopt now**: cherry-pick (or port the equivalent hunk) onto the integration branch before or inside the owning slice.
- **adopt after release**: take it when upstream tags it (see section 2). Owned by PRO-657 unless noted.
- **reject**: already in v1.9.0 (duplicate or net-zero), reverted inside the range, or conflicts with Carlos policy.

"State" is the mechanical check. `in-tag` means the content is already present in v1.9.0.
`net-0` means it was added and then removed inside the range. `live` means it contributes to the master delta.

### 4.1 Comms protocol (F03/F04 → PRO-655)

| sha(s) | subject | area | files | state | decision | reason | conflict with Carlos code | owner |
|---|---|---|---|---|---|---|---|---|
| `2b089d64` | fix: Fix protocol | NanoPb schema | `lib/NanoPbComm/proto/gaggimate.proto`, `src/Protocol.h` | live (final shape) | **adopt now** | Moves `dual_boiler` from field 5 to **field 6** and puts `repeated Addon addons` back at **5**. Field 5 was the v1.9.0 wire slot for `addons`, so this fix restores compatibility with v1.9.0 controllers' addons, and upstream master ends at exactly this layout. Upstream bumps `PROTOCOL_VERSION` to 6 (the 3→4 in the raw hunk is the stale branch base; the merges resolved it to 5→6). **Pick into PRO-655**: reserve `Capabilities.dual_boiler = 6` and `PROTOCOL_VERSION = 6`. Otherwise any field Carlos adds to `Capabilities` (F04: PRO-603/629 grind/temp-override extras) could take field 6 and fork the wire format from upstream | F03/F04: Carlos's `lib/NimBLEComm/*` + `comms.proto` are being dropped for NanoPbComm in PRO-655. Any Carlos capability bits must use fields ≥7. The display-side mismatch gate (`Controller::onSystemInfo`, `protocolMismatch`) is upstream code in the tag and needs no change | PRO-655 |
| `346dfec0` | fix: Add dualboiler capability code | NanoPb server + controller | `proto/gaggimate.proto`, `GaggiMateServer.{h,cpp}`, `GaggiMateController.cpp` | partial (proto hunk superseded by 2b089d64) | **adopt now** (signature only) | Adds `bool dualBoiler` to `GaggiMateServer::setSystemInfo`/ctor and sets `capabilities.dual_boiler`. It is needed so that the field from 2b089d64 is populated. Takes a constant `false` until PRO-657 lands the board | F04: Carlos comms port adds args to the same `setSystemInfo` signature (PRO-655). Merge the arg lists | PRO-655 |
| `c741e1b1` | fix: Fix build | NanoPb client | `GaggiMateClient.h` | live | **adopt now** | Adds `bool dualBoiler` to the client `SystemInfoCallback`, the display half of 346dfec0 | F04: display `Controller::onSystemInfo` callback. Carlos's wrapper in PRO-655 must forward the extra arg | PRO-655 |
| `e3e5a6c9` | fix: Add dual boiler to print | display log + boiler2 | `src/display/core/Controller.cpp` | live | **adopt now** (log hunk) / after release (boiler2 hunk) | The log hunk adds `db=%d` to the system-info log line and goes with 2b089d64. The `boiler2` rename belongs with the dual-boiler control block (4.2) | F28: Controller.cpp was heavily rewritten upstream. Log line only, so no conflict | PRO-655 (log) / PRO-657 |

### 4.2 Hardware: Max board, dual boiler, NTC, ADS, LED, valves (F14 → PRO-657)

The whole group is live, unreleased, and changes controller peripherals. Carlos has **no** implementation
commits in `lib/GaggiMateController` peripherals (matrix F14 audit: "no Carlos implementation
commits exist in range" for Max31855/BoilerFill). So these are adopt-after-release with low conflict.
The F14 conflicts are in Carlos's *display-side* safety gates.

| sha(s) | subject | area | files | state | decision | reason | conflict with Carlos code | owner |
|---|---|---|---|---|---|---|---|---|
| `328e0022`, `c4d4bc66` | feat: Add GaggiMate Max board definition; fix: Rename | controller board config | `ControllerConfig.h`, `GaggiMateController.cpp` | live (`GM_MAX_REV10` "GaggiMate Max Rev 1.x", autodetect 5) | **adopt after release** | New board with autodetect value 5 plus new config fields (`refillPin`, `auxPin`, `waterSensePin`, `ledPin`, `waterButtonPin`, `adcRates`, `ntcTiming`). The rename `scaleSclPin/SdaPin/Sda1Pin`→`scaleClkPin/Dat0Pin/Dat1Pin` touches every board. Carlos has no Max hardware (HW matrix in PRO-657) | F14: none in `ControllerConfig.h` (Carlos never edited it). The rename breaks any Carlos scale-pin reference. `git grep scaleSdaPin` on the integration tip must be re-checked when this is picked | PRO-657 |
| `f6663cff` | feat: Add NTC sensor implementation | controller sensors | `NtcThermistor.*`, `ADSAdc.*`, `Heater.*`, `PressureSensor.*`, `SimplePID`, `NimBLEClient/ServerController.cpp`, `predictive.h` | partial: `NtcThermistor.*` + `ADSAdc` already **in-tag** via `ee9fe9dc` (#427). NimBLE hunks target the removed lib | **reject** (as a commit) | A Feb-2026 original, superseded. The NTC driver shipped in v1.9.0 already. The `lib/NimBLEComm` hunks target a library that v1.9.0 deleted. The remaining live NTC bits (dual-channel NTC in `NtcThermistor` + `NtcTiming.h`) arrive via `b6b34b63`/`46ea5e44` | F04: NimBLEComm hunks are dead (F04 row: "`lib/NimBLEComm` gone in v1.9.0") | PRO-657 |
| `b6b34b63`, `b91efd5b`, `ec7f941a` | feat: Add changes for dual boiler machines; fix: Finish update for dual boiler; fix: Fix compat issues | controller + display dual-boiler control | `GaggiMateController.{h,cpp}`, `DigitalInput.*`, `NtcThermistor.*`, `NimBLEComm/*`, display `Controller.{h,cpp}`, `WebUIPlugin.cpp`, `MQTTPlugin.h`, `eez/actions.cpp`, `sim/comms/*`, web Home/dashboard | live (final shape = `ec7f941a`) | **adopt after release** | Second heater on `altPin` (steam), NTC on ADS ch2/3, refill/aux relays (index 2/3), water-sense input (button 3), `boilers[1]` in sensor frames, display `getTargetSteamTemp`/`raise/lowerSteamTemp`, `evt:status` `cst/tst/db`. `ec7f941a` restructures `Controller::updateControl` to send only changed components (`controlStateSent` + `gm::Payload batch[8]`), which also changes single-boiler behavior | F14/F28: display `Controller.cpp` is where Carlos's `isActiveSafe()` (59981731), `hasPumpTarget` CAR-336 gate, and FINISHED-phase guard (54c42c21) are re-applied in PRO-657/PRO-663. The `updateControl` rewrite is a direct textual conflict. F17: `MODE_STEAM` target-temp case (Carlos standby/auto-steam, `SteamButtonPolicy.h`). F22/PRO-664: web `useDashboardState.js`/`dashboardManager.js` hunks target upstream Home, **which Carlos is not adopting**, so re-express `cst/tst/db` in Carlos's dashboard. F13: `sim/comms/MockController.*` (PRO-656) | PRO-657 (+PRO-664 web, PRO-656 sim) |
| `346dfec0` (controller hunk) | (see 4.1) | | `GaggiMateController.cpp` | live | **adopt after release** | Passes `_config.capabilites.dualBoiler` to the server. It is `false` until Max/dual-boiler boards land | none | PRO-657 |
| `005e4751` | fix: Turn on pump during refill | display refill control | `src/display/core/Controller.cpp` | live (moved into dual-boiler block by `ec7f941a`) | **adopt after release** | When `steamBoilerLow` and idle it opens the refill relay and runs the pump at 100% power. **Dual-boiler only** in the final shape | F14: Carlos's CAR-336 `hasPumpTarget` gate / pump-target safety. The refill path sets `pump.power = 100` outside any Process. Verify it cannot bypass Carlos's pump guards. F17 BoilerFill: the matrix says take upstream's steam→refill (`64ba0d99`, **in the tag**, pre-tag). This is a different, dual-boiler refill | PRO-657 |
| `5e044f89` | feat: Add water valve | display hot-water valve | `Controller.{h,cpp}`, `GaggiMateController.cpp` | live | **adopt after release** | `waterValveActive` drives relay index 3 on dual-boiler machines. Hot water comes from steam pressure without a PumpProcess | F17/F16: Carlos's water/flush mode handling in `Controller::activate*`. Dual-boiler-gated, so no single-boiler behavior change | PRO-657 |
| `691ab6e4`, `387f6c98` | feat: Add led control; fix: Fix OTA, lights, UI | controller "lights" relay + display dual-boiler UI | `GaggiMateController.{h,cpp}`, `Controller.{h,cpp}`, `DefaultUI.{h,cpp}`, `lvgl/ui_events.cpp`, `WebUIPlugin.cpp` | live except OTA hunk (net-0) | **adopt after release** | The "LED control" here is a **power-indicator relay** (`lights = SimpleRelay(ledPin)`, on when the brew setpoint > 0), dual-boiler/Max only (gated in `ec7f941a`). It is *not* the Alba LED controller. The UI hunks show the steam temp / water button on dual boiler | F14: Carlos LED work (95a3a82b re-sync, 1864c877 timing, 59981731 `isActiveSafe`) is in the `LedControlPlugin` path, a separate code path, so no conflict. F15/PRO-658: `DefaultUI.cpp`/`ui_events.cpp` are LVGL-generated territory. Carlos's Nothing theme may conflict | PRO-657 (+PRO-658 UI) |
| `699c97f4` | fix: Fix ADC | controller ADC init | `GaggiMateController.cpp` | partial (superseded by `adcRates` in `46ea5e44`) | **reject** (as a commit) | Interim change of the ADS channel count 3→4 plus removal of duplicate `setup()` calls. Both were overwritten by the final `setup()` in `b6b34b63`/`ec7f941a` | none | PRO-657 |
| `46ea5e44`, `65bb5f6e` | Configure ADS channel rates and average pressure at 50 or 60 Hz; fix: Cleanup | ADC scheduling + pressure control loop | `ADSAdc.*`, `AdcSchedule.h`, `NtcTiming.h`, `PressureTiming.h`, `PressureSensor.*`, `DimmedPump.*`, `PressureController.*`, `TwoStateKalmanFilter.*`, `ControllerConfig.h`, docs (removed by `65bb5f6e`) | live | **adopt after release** (hardware validation required) | Per-channel SPS (`adcRates`, total ≤570), pressure averaged over mains half-cycles (`PressureControlRate::Hz50/Hz60`). The `DimmedPump` loop changes from a fixed 30 ms to a scheduled interval, `PressureController::update(mode, fresh, sampleTime, elapsed)`, and the Kalman process noise now scales with the sample time. **Affects all pressure boards, including Carlos's Gaggia** | F14: `DimmedPump.h` (CAR-336 `hasPumpTarget` lives display-side; `DimmedPump.h` `getPowerTarget` is used by the new log). F18 volumetric (`VolumetricCoalescer.h`) consumes `water_pumped`, which is now integrated with the measured `elapsed` instead of `0.03f`, so pumped-volume numbers shift slightly. Needs a before/after shot comparison on hardware (HW matrix) | PRO-657 |
| `45a86e25` | fix: Fix sim | simulator | `sim/comms/MockController.cpp` | live | **adopt after release** | The mock only applies temperature commands for boiler index 0. It goes with dual-boiler support | F13: Carlos's display-sim/Windows sim (PRO-656) changes `sim/comms/*` | PRO-656 |

### 4.3 WebUI security / CORS (F07 → PRO-660)

| sha(s) | subject | area | files | state | decision | reason | conflict with Carlos code | owner |
|---|---|---|---|---|---|---|---|---|
| `d47fa11b` | feat: Add cors to web server | WebUIPlugin | `WebUIPlugin.{h,cpp}` | **net-0**: an `AsyncCorsMiddleware` member plus `server.addMiddleware(&cors)` (origin `http://localhost:5173`, `HEAD, GET`, header `Upgrade`). It is **absent at upstream/master** (`git grep AsyncCorsMiddleware upstream/master` is empty; the merges dropped it) | **reject** | Upstream no longer ships it. It would also be an unconditional, always-on CORS allowance in production builds | **F07 direct conflict**: Carlos narrowed CORS (0f5b48fb). `WebUIPlugin::addCorsHeaders` only emits headers under `GAGGIMATE_DEVELOPMENT_CORS` and `localAuthShouldEmitCors(apMode, …)` (`src/display/plugins/LocalAuthPolicy.h`). Keep Carlos's policy in PRO-660 | PRO-660 |

### 4.4 OTA / update channel (F06 → PRO-661)

| sha(s) | subject | area | files | state | decision | reason | conflict with Carlos code | owner |
|---|---|---|---|---|---|---|---|---|
| `387f6c98` (OTA hunk), `a927f1a7` | fix: Fix OTA, lights, UI; fix: Fix OTA | `WebUIPlugin.cpp` release URL | `WebUIPlugin.cpp` | net-0 | **reject** | Points the non-`latest` channel at `tag/db`, a feature-branch prerelease tag for dual-boiler testers | F06: Carlos channels are `OtaIntentState.h` (`beta`/`nightly`/stable allow-list) + `OtaChannelSwitchPolicy.h`. `tag/db` must never reach Carlos | PRO-661 |
| `ba087be0`, `e258f330` | fix: Fix update channel; fix: Fix OTA channel | `WebUIPlugin.cpp` release URL | `WebUIPlugin.cpp` | **in-tag** (reverse-applies cleanly to v1.9.0; they revert the `tag/db` detour back to `tag/nightly`) | **reject** (no-op) | Nothing to pick. v1.9.0 already has `tag/nightly` | F06: none. Carlos's `resolveReleaseUrl()` replaces this line in PRO-661 anyway | PRO-661 |

### 4.5 Web UI / fonts / theme (F22/F23/F27 → PRO-664)

| sha(s) | subject | area | files | state | decision | reason | conflict with Carlos code | owner |
|---|---|---|---|---|---|---|---|---|
| `a044f229` | feat: Remove google fonts dep | web shell | `web/index.html`, `public/logo.svg`, `public/gm.png`, `Navigation.jsx` | **in-tag**: same patch-id as `d7c968d7` (in v1.9.0) | **reject** (already in base) | No pick needed. PRO-664 must *not* re-add Carlos's Google Fonts links while porting the shell | **F23 conflict**: Carlos's `web/index.html` still loads Montserrat + Doto + Space Mono from `fonts.googleapis.com` (lines 19-26), and Carlos ships local NType/Ndot fonts in `web/public/fonts/`. Decision for PRO-664: self-host any of Montserrat/Doto/Space Mono still used (offline AP mode cannot reach Google anyway), and take upstream's `logo.svg` / `Navigation.jsx` | PRO-664 |
| `87825119` | fix: Fix breakpoint | Home layout | `web/src/pages/Home/index.jsx` | **in-tag**: same patch-id as `10e59159` | **reject** (already in base) | Upstream Home only | F22: Carlos keeps his Home UI (PRO-664 decision) | PRO-664 |
| `919dea01` | fix: Fix duplicate | Settings page | `web/src/pages/Settings/index.jsx` | **in-tag** (the duplicate `connected` computed is absent in v1.9.0) | **reject** (already in base) | No-op | F24 Settings: none | PRO-664 |
| `4ba389e3` | feat: Allow buttons to be programmed individually | settings + controller + web | `Controller.{h,cpp}`, `Settings.{h,cpp}`, `WebUIPlugin.cpp`, `Settings/index.jsx` | **in-tag** via `0d2562be` (same subject; `Settings::getButtonBehavior`/`buttonBehavior` present in v1.9.0, now on `Property<>`) | **reject** (already in base) | Nothing to pick. The feature is part of the v1.9.0 base | F17: Carlos `SteamButtonPolicy.h` / momentary buttons meet upstream `buttonBehavior` in PRO-657/PRO-664 (already on their radar from the tag, not from this range) | PRO-657 / PRO-664 |
| (web hunks of `b91efd5b`) | dual-boiler metrics | web dashboard | `useDashboardState.js`, `dashboardManager.js`, `metricDefinitions.js`, `panelDefinitions.js`, `ApiService.js` | live | **adopt after release** (re-express) | `cst`/`tst`/`db` status fields and steam-temp metric | F22: these files belong to upstream Home. Carlos's `DashboardMerged.jsx`/`dashboardLogic.js` need their own equivalent. `ApiService.js` mapping (`cst`→`currentSteamTemperature`) is directly portable | PRO-664 |

### 4.6 Tooling / CI / release (F25 → PRO-667)

| sha(s) | subject | area | files | state | decision | reason | conflict with Carlos code | owner |
|---|---|---|---|---|---|---|---|---|
| `00dcf47b` | chore: Add release script for feature branches | CI + version script | `.github/workflows/{build,build-nightly,pr-flash}.yml`, `scripts/auto_firmware_version.py`, `scripts/release.sh`, `scripts/README.md`, `.gitignore` | live, applies cleanly to v1.9.0 | **adopt after release**; the `--match "v*"` part is optional **now** | `git describe --match "v*"` stops one-off/nightly tags leaking into `version.h`; `build.yml` only triggers on `v*` tags. `release.sh` is for upstream's feature-branch testers | **F25/F06 conflict**: Carlos's `scripts/auto_firmware_version.py` uses `--exclude nightly --exclude beta`, and Carlos has a `beta` channel + CAR-248 `generate_stable_versions.py` (F06). `--match "v*"` is compatible and stricter, so PRO-667 may adopt it. Carlos's workflows (`ci.yml`, `check`, `beta`) differ, so hand-port and don't cherry-pick. `scripts/README.md` also documents Carlos's promotion script (PRO-644), so merge text | PRO-667 |

### 4.7 The 18 merge commits (no own content)

All 18 are `Merge branch 'master' into feature/gaggimate-max` (or into `feature/rework-ads`), plus
the ADS PR merge. Their conflict resolutions only reconcile the side branch with master, which is
already in v1.9.0. The net effect is covered by the rows above. **Decision: reject (do not
cherry-pick merges)**, owner n/a (F99 row: content-free sync merges → drop).

`61189800`, `5a7855d8`, `2c9e730d`, `20b5520d`, `8f23d50b`, `0f481137`, `3c604034`, `5ca8f8a0`,
`bd09e6ef`, `1101fd2a`, `18b2df6e`, `ebb8ac5c`, `f2040ee7`, `fc3b9c51`, `c32f2ebc`, `2e5eea3f`,
`7e15f3b3`, `782a746e` (Merge PR #1 `gaggimate/feature/ads-rates-pressure-control`, the merge of `46ea5e44`).

The merges with a non-trivial `--cc` resolution are `1101fd2a` (946 lines), `c32f2ebc` (752),
`fc3b9c51` (521), and `18b2df6e` (107). That is where `PROTOCOL_VERSION` became 6 and the CORS
middleware was dropped. It is the reason to adopt the **final tree state** of a feature rather than the
original commits.

## 5. `2b089d64` (protocol fix) and PRO-655

**Yes, pick it up in PRO-655, as the final `upstream/master` shape rather than the raw commit.**
The raw hunk bumps `PROTOCOL_VERSION` 3→4 against the stale Feb branch base, so it won't apply to the
tag (5). Port this exact end state:

```proto
message Capabilities {
    bool dimming = 1; bool pressure = 2; bool led_control = 3; bool tof = 4;
    repeated Addon addons = 5;   // unchanged from v1.9.0: wire-compatible
    bool dual_boiler = 6;        // new
}
```
`lib/NanoPbComm/src/Protocol.h`: `PROTOCOL_VERSION = 6`.

Plus the plumbing: `GaggiMateServer::setSystemInfo(..., bool dualBoiler)` (346dfec0), the client
`SystemInfoCallback(..., bool dualBoiler, addons)` (c741e1b1), `SystemInfo.h` `bool dualBoiler`, and
the `db=%d` log (e3e5a6c9). Regenerate the nanopb `.pb.{c,h}`.

Why now:
1. **Field-number collision.** PRO-655 re-homes Carlos's comms extras onto NanoPbComm. If a Carlos
   field takes `Capabilities` 6 (or protocol version 6), a later upstream controller build becomes
   wire-incompatible with Carlos's display, and the fix is a breaking renumber. Reserve 6 now and
   start Carlos's fields at 7 or later.
2. **Version gate.** Upstream controllers built from master report protocol 6. A v1.9.0-based Carlos
   display (5) marks them `SYSTEM_PROTOCOL_MISMATCH` → "update display", which inhibits control.
   Matching 6 keeps Carlos displays working with upstream-master controllers.
3. It is tiny (<20 lines) and has no behavior change for single boilers (`dual_boiler=false`).

Caveat: after bumping to 6, a Carlos controller built at 5 no longer talks to the display. Flash
controller and display together, which is already the plan for the NanoPbComm cut-over.

## 6. Summary

Counts (each of the 45 commits is counted once, by its primary decision):

| decision | count | commits |
|---|---|---|
| **adopt now** | **4** | `2b089d64`, `346dfec0` (server/client signature), `c741e1b1`, `e3e5a6c9` (log hunk). All go to **PRO-655** |
| **adopt after release** | **13** | `328e0022`, `c4d4bc66`, `b6b34b63`, `b91efd5b`, `ec7f941a`, `005e4751`, `5e044f89`, `691ab6e4`, `387f6c98`, `46ea5e44`, `65bb5f6e` → PRO-657. `45a86e25` → PRO-656. `00dcf47b` → PRO-667 (optional `--match "v*"` now). The web part of `b91efd5b` → PRO-664 |
| **reject** | **28** | 10 non-merge: `f6663cff`, `699c97f4` (superseded); `4ba389e3`, `a044f229`, `87825119`, `919dea01` (already in v1.9.0); `d47fa11b` (net-0, conflicts with F07); `a927f1a7`, `ba087be0`, `e258f330` (OTA net-0 / in tag). Plus the 18 merges |
| **total** | **45/45** | 27 non-merge + 18 merge |

Guidance for other slices (this is not a pick):
- **PRO-660**: keep Carlos's narrowed CORS. Do not port `d47fa11b`.
- **PRO-661**: `tag/db` must never appear. The upstream channel lines are superseded by `resolveReleaseUrl()`.
- **PRO-664**: Google Fonts are already removed in the base. Don't reintroduce them (self-host).
- **PRO-657**: plan the "after release" block as one unit. It needs a HW before/after pressure +
  volumetric comparison on the Gaggia (single boiler), because `46ea5e44` changes the control loop timing.

Rebase: **no rebase now.** Upstream has no tag past `v1.9.0`. Re-evaluate on the next upstream tag.

## 7. QA record

- Coverage re-verified mechanically: each of `git rev-list v1.9.0..upstream/master` (45 SHAs) appears
  in this doc → **45/45**.
- Spot-checks against the trees/diffs (all pass): (1) `2b089d64`: master proto has `addons = 5`,
  `dual_boiler = 6`. (2) `d47fa11b`: `AsyncCorsMiddleware` count is 0 at master. (3) `005e4751`:
  refill is inside `if (systemInfo.capabilities.dualBoiler)`. (4) `691ab6e4`: `lights` is only
  constructed in the dual-boiler branch. (5) `a044f229` ≡ `d7c968d7` (patch-id `10acaf25`), and
  there is no `fonts.googleapis` in the tag. (6) `ba087be0`/`e258f330`: the tag has `tag/nightly`
  and no `tag/db`. (7) `4ba389e3`: `getButtonBehavior` is present in the v1.9.0 `Settings.h`.
  (8) `46ea5e44`: the master `DimmedPump` loop uses `pressureControlIntervalMs(...)`, not 30 ms.
  Also `00dcf47b`: master `auto_firmware_version.py` uses `--match v*`.
