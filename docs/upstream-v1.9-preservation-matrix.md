# Upstream v1.9.0 preservation matrix: Carlos-only work (PRO-652)

Epic: PRO-651. Slice branch: `feature/upstream-v1.9-integration`.
Upstream `v1.9.0` = `43d2a7ba`. Carlos tip analysed: `origin/dev-master` = `ec9f4fc4` (2026-09-29).
Merge base `0172f1f6`.

## Coverage

- Range `v1.9.0..origin/dev-master`: **1321 commits** (1124 non-merge + 197 merge).
- Mapped to a feature: **1321 / 1321 (100%)**. The generator asserts this
  (`total == git rev-list --count`).
- Commit→feature map, one row per commit: `docs/upstream-v1.9-preservation-matrix.csv`
  (sha, date, merge flag, feature, matched_by, PRs, Linear IDs, subject).
- Per-feature commit lists: `docs/upstream-v1.9-preservation-matrix.appendix.md`.
- Regenerate with `python3 scripts/v19_commit_feature_map.py` (it needs the `v1.9.0` tag and
  `origin/dev-master` fetched).

The tip moved from `4e4ac6bd` (the epic's snapshot) to `ec9f4fc4`, so there are 1321 commits
here, not the 1315 in the epic. The extra 6 are all dependabot bumps (#680–#685).

## Method and decisions I made on my own

1. **Clustering is rule-based and deterministic.** The rules live in
   `RULES` in the script, and the first match wins. Subject rules (PR `(#NNN)`, `PRO-/CAR-` IDs,
   conventional scope, keywords) run before path rules. Commits that matched nothing fall into
   `F99-misc`. Those are merge-from-master commits, which carry no feature of their own.
   Keyword clustering is approximate at the edges. Example: a "fix(web): dashboard
   dose" commit can land in F18 rather than F22. Each **slice owner reads the appendix list for
   their feature(s) and the neighbouring features**. The row decision applies to the feature, not
   to each commit.
2. **Merges are included** (the AC says "every commit"). A merge maps to the feature named in its
   subject/branch, or to its first-parent diff paths.
3. **Upstream equivalents were verified** against the `v1.9.0` tag with `git ls-tree` / `git grep` /
   `git merge-base --is-ancestor`. The pristine tree at `~/work/gaggimate-v1.9.0-pristine` was not
   touched. "none" means no equivalent file or symbol exists in `v1.9.0`.
4. **The Arduino-3.x / NimBLE-2.x / C++20 stack (#307–#355) is DROP.** PR #356 (`ea232239`) rolled
   dev-master back. The tip is `espressif32@6.12.0`, `NimBLE-Arduino@1.4.3`, `gnu++17`, which
   matches the v1.9.0 platform (`espressif32@6.12.0`, `NimBLE-Arduino@^1.4.0`). Only the fixes
   that #356's explicit restore list re-applied survive. Those appear under their own feature rows
   because they sit at the tip. They are not listed under F02.
5. **Tests**: a "test" cell names an existing host test (`test/test_*`, `pio test -e native`), a
   vitest file (`web/src/**/*.test.js[x]`), or a PRO-667 on-device check where logic is not
   host-testable. v1.9.0 ships only 7 test suites
   (`test_autotune_simc, test_button_handler, test_ota_download, test_ota_loadtest,
   test_puckflow_latch, ota_common`). Carlos has 49, and **all Carlos host tests must be carried
   into the integrated tree**. This is tracked as a cross-cutting row (X1).

Decision legend: **port** = carry as-is/adapted. **rework** = re-implement on upstream's new
architecture. **superseded** = superseded-by-upstream (evidence cited), so drop Carlos's version.
**drop** = obsolete or reverted.

## Feature matrix

Commit counts come from the generator (appendix). "Up v1.9.0" cites a path in the tag or an
upstream commit reachable from it.

| # | Feature (commits) | Key sources | Up v1.9.0 equivalent (evidence) | Decision | Slice | Proving test |
|---|---|---|---|---|---|---|
| F01 | Dependency bumps (62) | dependabot #589–#685, `.github/dependabot.yml` | none: no `.github/dependabot.yml` in v1.9.0 | **drop** the bump commits and re-resolve versions on the integrated lockfile. **port** `dependabot.yml` | PRO-665 (lockfiles), PRO-667 (dependabot) | `cd web && npm ci && npm run build`; relay `npm test` |
| F02 | Arduino-3.x / NimBLE-2.x / C++20 stack (21) | #307 c150d3d3, #309 99295c58, PRO-290/291, rollback #356 ea232239, docs/cpp-standard-spike.md | v1.9.0 is also `espressif32@6.12.0` + `NimBLE-Arduino@^1.4.0` + `gnu++17` (platformio.ini:12,60,27) | **drop**: already reverted at tip by #356. Keep the `gnu++17` pin and the CAR-340 comment | PRO-665 | `pio run -e display` on the pinned platform |
| F03 | NanoPb comms spike (22) | `lib/NanoPbSpike`, `docs/spike-nanopb-comms/`, `test/test_nanopb_comms`, PRO-245..259 | upstream shipped the real thing: `lib/NanoPbComm/` (22 files), ccfe792b "Framed nanopb TX/RX comms protocol (replaces NimBLEComm)" (#726) | **superseded**. Salvage only the runtime measurements in the spike docs for the PRO-655 review | PRO-655, PRO-665 | n/a (delete). PRO-655 comms tests replace it |
| F04 | Controller BLE comms (35) | `lib/NimBLEComm/*` (9 files), `comms.proto`, WRITE_ENC revert e81cec5a (CAR-257), bond/reconnect fixes, `docs/ble-pairing.md` | `lib/NimBLEComm` gone in v1.9.0. `lib/NanoPbComm/src/ble/Ble{Client,Server}Transport.cpp` hold bonding + security. Startup-race recovery 4f858880 (#873) | **rework** onto NanoPbComm: carry the behaviours (bond recovery, setpoint-write regression CAR-257, reconnect), not the code | PRO-655 | new NanoPbComm host tests + PRO-667 pair/re-pair/boiler-heats on HW |
| F05 | BLE scale plugin hardening (43) | PRO-459, PRO-647 teardown/mutex/UAF, `BLEScale{Scan,Connect,Measurement}Policy.h`, `BLEVolumetricOverridePolicy.h` | v1.9.0 `BLEScalePlugin.cpp` has none of the policies (`git grep ScanPolicy v1.9.0` empty). Upstream added battery 73ee4b39 (#682) | **port** the policies + mutex/teardown onto upstream's plugin, and keep upstream battery reporting | PRO-655 | `test_ble_scale_{scan,connect,measurement}_policy`, `test_ble_volumetric_override_policy` |
| F06 | OTA policies + channels (77) | `Ota{AsyncResolve,ChannelSwitch,ResolveHeap,ResolveReuse,UpdateCheck}Policy.h`, `OtaIntentState.h`, 95fe4fc4, PRO-394/400/554–569/599/648/649, CAR-248 `scripts/generate_stable_versions.py` | upstream rewrote the download path: `lib/OTA/src/ResumableDownloader.cpp`, `EspHttpTransport.cpp` (5c380e08 #900, aa4e3228). OOM fix for the update check 2dd893da (#755). No `installedChannel`/STABLE_VERSIONS | **rework**: adopt the upstream downloader and port the Carlos resolve/heap/channel/intent policies on top | PRO-661 | `test_ota_*` (8 suites), `test_semver_extensions`, `test_github_ota_semver_reassign`, `scripts/test_generate_stable_versions.py`, upstream `test_ota_download` |
| F07 | WebUI security: local auth, narrowed CORS, path traversal, secret masking (19) | 0f5b48fb, `LocalAuthPolicy.h`, `PathTraversalPolicy.h`, CAR-96 (#84), settings secret sentinel | none. v1.9.0 `WebUIPlugin.cpp` has no `Access-Control-Allow-Origin` and no local auth (`git grep` empty) | **port** (semantic, onto upstream WebUIPlugin + `WebSocketHandler.cpp` split) | PRO-660 | `test_local_auth_policy`, `test_path_traversal_guard`, `Settings.localAuthHandoff.test.jsx` |
| F08 | Relay server + secure relay tokens + relay policy (15) | `relay-server/` (13 files, CF Workers), 67541c6f, b068f462 `RelayConnectionPolicy.h` | none: no `relay-server/` in v1.9.0 (the `relay` grep hit only SmartGrindPlugin's GPIO relay) | **port** (relay-server is standalone; the display side is semantic onto WebUIPlugin) | PRO-660 | `test_relay_connection_policy`; `cd relay-server && npm test` |
| F09 | WebUIPlugin core: WS reassembly cap, broadcast/close, lifecycle deferral, ws mutex, spec gate (49) | `WsReassemblyPolicy.h`, `WsBroadcastClosePolicy.h`, `WebUiLifecycleDeferPolicy.h`, `docs/websocket-api.yaml`, `scripts/check_ws_api_spec_drift.py` (PRO-610) | upstream split WS handling into `src/display/plugins/WebSocketHandler.cpp` and PSRAM-backed rx buffers (70c54ca0 #724, f3dd9d17). `docs/websocket-api.yaml` exists upstream too | **rework**: re-home the policies into WebSocketHandler, merge both YAML specs, keep the drift gate | PRO-660 | `test_ws_reassembly_cap`, `test_ws_broadcast_close_policy`, `test_webui_lifecycle_defer_policy`, `check_ws_api_spec_drift.py` + its test, `ApiService.contract.test.js` |
| F10 | Memory: mbedTLS PSRAM, NimBLE host PSRAM, heap diag, DRAM audit (25) | 96e7fdf6 `MbedtlsPsramAllocator*`, 886bbc3e (PRO-567 platformio.ini NimBLE msys), 8e3b79a2 `GmHeapDiag.h`, `docs/pro-566-internal-dram-audit.md`, env `display-heapdiag` | partial: upstream `src/display/util/PsramAllocator.h` (ArduinoJson PSRAM allocator), bccb12c2 (#723), 70c54ca0 (#724). No mbedTLS/NimBLE PSRAM routing | **port** mbedTLS/NimBLE routing + heapdiag. **superseded** where Carlos PSRAMs JsonDocuments (use upstream `PsramAllocator`) | PRO-662 | `test_mbedtls_psram_allocator_policy`; PRO-662 stress runs on PSRAM + no-PSRAM boards |
| F11 | Diagnostic log plugin / ESP log tee / queue (8) | f2a8764c, `DiagnosticLogPlugin.*`, `DiagLogFormat.h`, `src/display/EspLogTee.h` | none (`esp_log_set_vprintf`/DiagnosticLog absent in v1.9.0) | **port** | PRO-662 (queue/heap), then gap G1 | `test_diag_log_tee` |
| F12 | Embedded WebUI / FS / partitions (26) | `scripts/embed_webui*.py`, `build_webui.sh`, `docs/embed-webui-partition-headroom.md`, CAR-281 SPIFFS name len (#128), `build_spiffs.sh` | **superseded** core: upstream 3bc04041 "Embed WebUI in application partition" (#764) ships `scripts/embed_webui.py`, `embed_webui_pre.py`, `build_webui.sh`. Partition tables are unchanged (both `boards/LilyGo-T-RGB.json` → `default_16MB.csv`) | **superseded** for the embed pipeline. **port** Carlos headroom checks + CAR-281 fix if still relevant | PRO-659 | `pio run -e display` size report vs headroom doc; boot + `/` served (PRO-667) |
| F13 | Simulator (display-sim) + Windows sim (8) | `sim/platform/*` shims, `fs_shim.cpp`, PRO-207 Windows build | upstream `sim/` (60 files) + `[env:display-sim]` in v1.9.0 platformio.ini:166. No Windows support (`git grep WIN32 v1.9.0 -- sim` empty) | **rework** onto upstream sim + NanoPbComm, and port the Windows shims | PRO-656 | `pio run -e display-sim` on Linux + Windows |
| F14 | Hardware: controller lib, PID/autotune, drivers, boards, LED (18) | `lib/GaggiMateController` (32-file diff, −1243 lines vs tag), drivers, `BoilerFillPlugin` (7-line diff) | `Max31855Thermocouple` and `BoilerFillPlugin` **already exist upstream** (v1.9.0 `GaggiMateController.h:10`, `plugins/BoilerFillPlugin.cpp`). Upstream added Alba I2C ad8a1ace (#915), precise water a0519300 (#885), LED fixes 9e9eecb9/6833963d, SIMC autotune 48802e20 (#683), Waveshare 1.43 f7c186b2 (#617) | **superseded** by default (take upstream controller lib/drivers). **port** only the Carlos deltas the slice diff proves are fixes | PRO-657 | upstream `test_autotune_simc`, `test_puckflow_latch`; PRO-657 HW matrix |
| F15 | LVGL/SquareLine display UI: Nothing theme, status/chip bars, BrewScreen, Quick Settings brightness/restart, fonts/icons (156) | `src/display/ui/default/lvgl/` (76 files), `DisplayRestartPolicy.h`, `assets/fonts`, `assets/gm-icons`, CAR-292/293 | upstream moved to **EEZ Studio**: `src/display/ui/default/eez/` (57 files), `eez-ui/gaggimate.eez-project`. No `lvgl/` dir. June UI update 3452bca6 (#792) | **rework**: reapply the Carlos UI on the EEZ project. This is the biggest-risk row | PRO-658 | `test_display_restart_policy`; PRO-667 screen-by-screen visual check |
| F16 | Manual mode + manual GRIND persistence + grinder manager (60) | 9cbe1b22, 9fa9d726, `process/ManualProcess.h`, `GrinderManager.*`, CAR-371 `grinderManager.js`, `test_notes_grind_setting_policy` | none: v1.9.0 `core/process/` has no ManualProcess, and there is no GrinderManager | **port** | PRO-657 (firmware process) + PRO-664 (web) | `test_notes_grind_setting_policy`, `grinderManager.test.js` |
| F17 | Standby / auto-steam / flush / steam button / auto-wakeup (66) | acf03ea3 standby post-brew, `StandbyTransitionPolicy.h`, `StandbyReassertPolicy.h`, `SteamButtonPolicy.h`, `useStandbyOnBrew.js` | partial: upstream flush as a button behaviour c9c84ba3 (#720) + button rework f6534f56 (#899), `flushDuration` in v1.9.0 `Settings.h`, steam refill on mode change 64ba0d99. No auto-steam or standby-on-brew (`git grep -i autoSteam v1.9.0` empty) | **superseded** for flush/button handling (take upstream `test_button_handler`). **port** standby-post-brew, auto-steam, standby policies | PRO-657 (firmware) + PRO-664 (web) | `test_standby_transition_policy`, `test_standby_reassert_policy`, `test_steam_button_edge_policy`, upstream `test_button_handler` |
| F18 | Brew targets: volumetric source/coalesce, global weight cutoff, yield override, per-profile temp override (52) | PRO-629, `VolumetricCoalescer.h`, `VolumetricMeasurementSource.h`, `GlobalWeightCutoffPolicy.h`, `BrewTemperatureOverridePolicy.h`, `ShotFinalYieldPolicy.h` | none (all symbols absent in v1.9.0). Upstream changed water accounting a0519300 (#885) | **port** and re-validate against upstream `BrewProcess.h` | PRO-657 | `test_volumetric_{target,coalesce,source_policy}`, `test_global_weight_cutoff`, `test_brew_temperature_override`, `test_shot_final_yield_policy` |
| F19 | Beans + Beanconqueror export (66) | `BeanManager.*`, `req:beans:*`, `pages/Beans`, `utils/beanconqueror/`, CAR-371..375, `docs/beanconqueror-export.md` | none: no BeanManager or Beans page in v1.9.0 | **port** | PRO-664 (web) + PRO-660 (WS handlers) | `test_bean_resolution_policy`, `beanManager.test.js`, `Beans.beanconquerorExport.test.jsx`, `ShotHistory.beanconquerorExport.test.jsx` |
| F20 | Shot history / analyzer / notes / comparison / CSV / shot→profile (91) | PRO-631 notes start target temp, `ShotNotesPersistencePolicy.h`, `ShotIndexMetadataPolicy.h`, `ExtendedRecordingPolicy.h`, `historyExport.js`, `shotFilters.js`, `comparisonShots.js`, `pages/ShotToProfile` | partial: upstream `ShotNotesCard.jsx`, `ShotAnalyzer/` (puck resistance f3892ed2 #905), Visualizer upload. No CSV/filters/comparison/ShotToProfile | **rework**: merge onto upstream analyzer (take upstream puck resistance), and port notes/metadata/export/filters/comparison/ShotToProfile | PRO-664 (web) + PRO-660 (plugin) | `test_shot_index_metadata`, `test_extended_recording_policy`, `ShotNotesCard.test.jsx`, `historyExport.test.js`, `shotFilters.test.js`, `detectPhases.test.js` |
| F21 | Profiles: validation, schema, keyframes, import/export (93) | `test_profile_validation`, `test_strict_validation`, `StrictValidationPolicy.h`, `ProfileKeyframeChart.jsx`, `keyframeProfileLogic.js`, `schema/profile.json`, CAR-233 profile transfer | partial: upstream `ProfileManager.cpp` + `models/profile.h` validation, startup profile fa0f9c12 (#626) | **rework**: keep upstream startup profile, port strict validation + keyframes + schema extensions | PRO-664 | `test_profile_validation`, `test_strict_validation`, `keyframeProfileLogic.test.js` |
| F22 | Dashboard / Home (PRO-623..640) (91) | `pages/Home/DashboardMerged.jsx`, `dashboardLogic.js`, `dashboardManager` | upstream rewrote Home (`pages/Home/cards/*`, `DashboardSidebar.jsx`, `FlushButton.jsx`) + `pages/DashboardSettings` | **rework**: decide on one dashboard. Default: Carlos's DashboardMerged on top of upstream data hooks (`useDashboardState.js`) | PRO-664 | `DashboardMerged.a11y.test.jsx` + dashboard vitest suites |
| F23 | Web theme / shell / accents / a11y / connection banner (48) | PRO-643 custom accents, `themeManager.js`, `PageShell.jsx`, `ConnectionBanner.jsx`, `ApiService` reconnect | partial: upstream `web/src/style.css` restyle (June UI). No accents manager or ConnectionBanner | **port** | PRO-664 | `accentContrast.test.jsx`, `PageShell.accentBadge.test.jsx`, `ConnectionBanner.test.jsx`, `ApiService.reconnect.test.js` |
| F24 | Settings, backup/restore, Google Drive, WiFi/mDNS, MQTT/HomeKit flags, EventIds, settings transactions (47) | `backupBundle.js`, `GoogleDriveBackupCard.jsx`, `SettingsPersistenceTransaction.h`, `MdnsNamePolicy.h`, `MqttConnectPolicy.h`, `EventIds.h`, `config/features.h`, PRO-365 STA recovery (#355) | partial: upstream Settings split into `tabs/*Tab.jsx`, `WifiStaWatchdogPlugin`, `NetworkWatchdogPlugin`, `mDNSPlugin` | **rework**: map Carlos settings into upstream tabs. **superseded** for STA recovery if `WifiStaWatchdogPlugin` covers the HomeKit AUTH_EXPIRE case (verify in slice). **port** the rest | PRO-664 (web) + PRO-663 (watchdog) + PRO-662 (MQTT/HomeKit flags) | `test_settings_persistence_transaction`, `test_mqtt_connect_policy`, `test_event_system`, `backupBundle.test.js`, `Settings.*.test.jsx` |
| F25 | CI / quality: ci.yml, check/pr-flash/nightly/beta, clang-tidy, cppcheck, sanitize, extra PIO envs, promotion + stable-versions scripts, flash.sh (67) | `.github/workflows/{ci,check,pr-flash,build-nightly,build-beta,deploy-web}.yml`, `[env:native-sanitize]`, `[env:native-tidy]`, `display-flags-off`, `display-no-*`, `display-{lilygo,amoled,waveshare}`, `scripts/select_tidy_sources.py`, `generate_promotion_pr_body.py` (PRO-644), CAR-341, PRO-608..611 | partial: upstream `.github/workflows/{build,build-nightly,check,pr-flash,ota-testbench}.yml`. No ci.yml / tidy / sanitize / flag-off envs | **port** gates + envs. **rework** workflow files to merge with upstream's (keep `ota-testbench.yml`) | PRO-667 (+ PRO-665 for env cleanup) | CI green on the integration branch; `scripts/test_*.py` |
| F26 | Docs / AGENTS / plans / specs (26) | `docs/superpowers/*`, `AGENTS.md`, `CLAUDE.md`, `CONTRIBUTING.md`, `.mailmap` | n/a | **port** AGENTS/CONTRIBUTING (update for NanoPb/EEZ). **drop** stale plans/specs | PRO-665 | doc review |
| F27 | Web misc, not otherwise clustered (13) | see appendix | per-commit | **port** (triage in the slice) | PRO-664 | vitest suite |
| F28 | Firmware misc: Controller race/isActiveSafe fixes, rule-of-5, dead code (10) | a83fda2f, cfe1d418, PRO-378, PRO-380, CAR-101 | per-commit. Controller.cpp was heavily rewritten upstream | **rework**: re-check each against upstream Controller | PRO-663 | `test_change_mode_defer_policy`; native-sanitize |
| F99 | Merge-from-master / sync merges, stray chores (7) | `Merge remote-tracking branch 'origin/master'`, CAR-401, PRO-485 | n/a | **drop** (no content of their own) | none needed | n/a |

### Cross-cutting rows

| # | Item | Decision | Slice | Proving test |
|---|---|---|---|---|
| X1 | 49 Carlos host test suites (`test/test_*`, `test/native` shims, `test/tidy`) + 73 web vitest files. v1.9.0 has 7 host suites and 0 web `*.test.js[x]` | **port** every suite whose feature row is port/rework. Delete a suite only together with its superseded feature | owner of each feature row. Aggregate gate in PRO-667 | `pio test -e native`, `pio test -e native-sanitize`, `cd web && npx vitest run` |
| X2 | Upstream reliability fixes Carlos lacks (243 upstream-only commits, `upstream.txt`) | adopt, per the PRO-663 checklist | PRO-663 | per-fix |
| X3 | Post-tag upstream (`v1.9.0..upstream/master`, 45 commits) | review separately | PRO-666 | n/a |
| X4 | NVS static-init pitfall (PRO-331, 18dfb6d3: settings read in `setup()`, not the global ctor) | **port**, and re-check upstream `Settings` construction order | PRO-663 | on-device reboot persistence (PRO-667) |

## PR #356 rollback audit (#307–#355)

Of the 49 non-merge commits with PR numbers #307–#355, I checked each one's patch against the tip
(`git apply -R --check` = still present, forward `--check` = cleanly reverted, neither = later
edited):

- **reverted (11), drop**: c150d3d3 NimBLE 2.x (#307), 99295c58 TFT_eSPI 3.x (#309), 5d7b05d0 IDF5
  ADC oneshot, 9ed5f358 NimBLE-PSRAM for 2.x, 3ae47bdf, a3d007b6, 2d3fceb2, and the 3.x docs cc757e76,
  5453c759, ef4e10b1, fd25daf5.
- **present (4)**: 434744df, dcf0c673, eb0a9237 (`req:raise/lower-brew-target`), 657bada6.
- **modified (34)**: restored by #356, then edited later. These include PRO-331 NVS-in-setup,
  PRO-333 single WiFi owner, PRO-358 DRAM reclaim, PRO-365 STA recovery, WS reassembly cap,
  BLE-scale teardown handshake, UDP log tee bound, and MQTT connect guard. Their policy headers and
  `test_*` suites exist at the tip, so they are counted as live features in F05/F09/F10/F11/F24/X4.
  The 3.x-only items in this group (e9723b5b pioarduino, e6ca71c6 gnu++20, 12cdbc00 NimBLE 2.5,
  866aaaa3) are diff-noise against the rollback and stay **drop** (the tip platformio.ini is 6.12.0 /
  1.4.3 / gnu++17).

## Seed-list cross-check

| Seed item | Commit/ID | Mapped feature | Row |
|---|---|---|---|
| NimBLE host PSRAM | 886bbc3e | F10 | port, PRO-662 (verify NimBLE 1.4.x / NanoPb path keeps `MEM_ALLOC_MODE_EXTERNAL`) |
| BLE scale UAF/teardown | PRO-459, PRO-647 (e946cee4) | F05 | port, PRO-655 |
| OTA flash eligibility / channels | 95fe4fc4, PRO-394 a13ef966, PRO-400/554–569/599/648 | F06 | rework, PRO-661 |
| PRO-649 | no commit in range or in any ref (`git log --all --grep PRO-649` empty) | none | gap **G2** |
| Local auth + CORS, relay tokens, relay policy | 0f5b48fb, 67541c6f, b068f462 | F07, F07, F08 | port, PRO-660 |
| mbedTLS PSRAM, diag log queue, DRAM audit | 96e7fdf6, f2a8764c, 8e3b79a2 | F10 | port, PRO-662 |
| Manual GRIND, active-shot grind, standby post-brew | 9cbe1b22, 9fa9d726, acf03ea3 | F16, F16, F17 | port |
| Per-profile temp override | PRO-629 → f39cb9db (#626). The subject has no ID | F18 | port, PRO-657 |
| Shot Notes start temp / accents | PRO-631, PRO-643 | F18 / F22 (keyword spill, see Method 1) | port, PRO-664 |
| Windows sim | PRO-207 → 4a2380b4 (CAR-399, #191) | F13 | rework, PRO-656 |
| Max31855, BoilerFill | lib/GaggiMateController | F14 | superseded (both exist in v1.9.0) |
| Infra envs / CI / tidy / cppcheck / scripts | CAR-341, PRO-608..611, PRO-644 | F25 | port, PRO-667 |

## Gaps and risks

- **G1 Diagnostic log plugin (F11) has no natural slice.** PRO-662 covers only its PSRAM queue.
  Proposal: add "port DiagnosticLogPlugin + EspLogTee" to PRO-662's scope (added in the Linear
  comment).
- **G2 PRO-649** is in the seed list, but no commit references it. It is either unmerged or cited
  wrongly. Carlos to confirm.
- **G3 Relay server (F08)**: PRO-660 names relay tokens and relay policy but not the
  `relay-server/` Node/Cloudflare package or its CI/deploy. I assigned it to PRO-660 and flagged it.
- **G4 Beans/grinder/manual-mode firmware (F16/F19)** have no upstream counterpart and span
  PRO-657/660/664. Those slices must agree on the WS handler owner (proposal: PRO-660).
- **G5 Dependabot config + GitHub Pages deploy (`deploy-web.yml`)** are not named in any slice.
  I assigned them to PRO-667.
- **R1 EEZ UI (F15, 156 commits)** is the largest and riskiest row. All LVGL work must be redone.
- **R2 Dashboard (F22)**: two parallel rewrites. A product decision is needed before PRO-664 starts.
- **R3 Test debt (X1)**: upstream has no web unit tests, so every Carlos vitest suite must come
  across or the regressions stay silent.

## QA self-review

- The coverage assert passed: 1321 mapped = 1321 in `git rev-list --count v1.9.0..origin/dev-master`.
  The table row counts also sum to 1321.
- Spot-check: 18 random commits (`random.seed(652)` over the CSV). All 18 mapped to the right
  feature, e.g. a13ef966 → F06, PRO-266 UDP log tee → F11, CAR-371 grinder → F16, CAR-373 beans → F19,
  CAR-307 icons → F15, #677 wrangler → F01, PRO-547 dose recorder → F18. Two misroutes I found
  earlier in QA are fixed in the rules: the memory seeds (96e7fdf6/f2a8764c/8e3b79a2/886bbc3e) had
  landed in OTA/BLE, and PRO-647 had landed in BLE comms.
- No firmware or web code changes. Only docs plus a read-only generator script.
