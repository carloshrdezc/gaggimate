# Upstream v1.9.0 preservation matrix: Carlos-only work (PRO-652)

Epic: PRO-651. Slice branch: `feature/upstream-v1.9-integration`.
Upstream `v1.9.0` = `43d2a7ba`. Carlos tip analysed: `origin/dev-master` = `ec9f4fc4` (2026-09-29).
Merge base `0172f1f6`.

## Coverage

- Range `v1.9.0..origin/dev-master`: **1321 commits** (1124 non-merge + 197 merge).
- **Enumerated: 1321 / 1321.** The generator asserts `total == git rev-list --count`. That only
  proves every commit has a row. It does not prove the row is correct.
- **Classification verified deterministically** (review round 2 redesign; see QA). Every touched path
  maps through `PATH_RULES` to a feature (asserted: no unmapped path). Every commit whose subject
  feature is absent from its path features is flagged and resolved (asserted: `unresolved_mismatches == 0`).
  Mixed commits list every other owner in a `secondary` column. On top of that: 27 `OVERRIDES`,
  40 `SEEDS`, a secondary-owner seed for 0f67772d, and the F99 sync-only assert.
- Commit→feature map, one row per commit: `docs/upstream-v1.9-preservation-matrix.csv`
  (sha, date, merge flag, feature = primary, matched_by, secondary, path_features with lines
  changed, PRs, Linear IDs, subject). Table counts read "primary + N sec".
- Per-feature commit lists: `docs/upstream-v1.9-preservation-matrix.appendix.md`. Each feature lists its
  primary commits, then its secondary ones tagged `[secondary; primary Fnn]`.
- Regenerate with `python3 scripts/v19_commit_feature_map.py` (it needs the `v1.9.0` tag and
  `origin/dev-master` fetched).

The tip moved from `4e4ac6bd` (the epic's snapshot) to `ec9f4fc4`, so there are 1321 commits
here, not the 1315 in the epic. The extra 6 are all dependabot bumps (#680–#685).

## Method and decisions I made on my own

1. **Clustering: path-derived owners, subject-picked primary.** `OVERRIDES` run first, then
   `MISMATCH_OVERRIDES`, then content-free sync merges go to `F99-misc`, then the subject `RULES`
   pick a primary. If that primary is not among the commit's `PATH_RULES` features, the commit is a
   **mismatch** and the path feature with the most changed lines wins, unless the subject feature is
   concern-defined (`CROSSCUT`: F01 deps, F02 platform, F07 security, F10 memory, F12b FS migration),
   whose work lands in other features' files by nature. Commits with no subject hit take the
   top path feature. Every other path feature is a **secondary** owner (path_features − primary).
   **Documented exception**: `NEUTRAL` paths (docs, `*.md`, lockfiles/`package.json`,
   `platformio.ini`, shared hubs like `Controller.*`/`ApiService.js`/`routes`, stray artifacts)
   carry no ownership. A docs-only or lockfile-only commit has no path features, so it is never a
   mismatch and keeps its subject feature. Sync merges get no secondaries (they replay owned commits).
   **Slice owners read their appendix section, primary and secondary.**
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
| F01 | Dependency bumps (64) | dependabot #589–#685, `.github/dependabot.yml` | none: no `.github/dependabot.yml` in v1.9.0 | **drop** the bump commits and re-resolve versions on the integrated lockfile. **port** `dependabot.yml` | PRO-665 (lockfiles), PRO-667 (dependabot) | `cd web && npm ci && npm run build`; relay `npm test` |
| F02 | Arduino-3.x / NimBLE-2.x / C++20 stack (21) | #307 c150d3d3, #309 99295c58, PRO-290/291, rollback #356 ea232239, docs/cpp-standard-spike.md | v1.9.0 is also `espressif32@6.12.0` + `NimBLE-Arduino@^1.4.0` + `gnu++17` (platformio.ini:12,60,27) | **drop**: already reverted at tip by #356. Keep the `gnu++17` pin and the CAR-340 comment | PRO-665 | `pio run -e display` on the pinned platform |
| F03 | NanoPb comms spike: nanopb subjects/paths only (10 + 3 sec) | `lib/NanoPbSpike`, `docs/spike-nanopb-comms/`, `test/test_nanopb_comms`, 72da9327 (PRO-239), PRO-241..244, PRO-306/307/309 | upstream shipped the real thing: `lib/NanoPbComm/` (22 files), ccfe792b "Framed nanopb TX/RX comms protocol (replaces NimBLEComm)" (#726) | **superseded** (all 10 are spike/proto work). Salvage only the runtime measurements in the spike docs for the PRO-655 review. The former PRO-245..259 spill is re-homed: ef60874d/2782b14c→F05, 05c01878/b255cd0b/9a836031/ce9774dc→F25, eec9cfea→F24, 2aaad864→F23, 95a3a82b→F14, 74932051→F26, 39e73683/4a731499→F01 | PRO-655, PRO-665 | n/a (delete). PRO-655 comms tests replace it |
| F04 | Controller BLE comms (28 + 20 sec) | `lib/NimBLEComm/*` (9 files), `comms.proto`, WRITE_ENC revert e81cec5a (CAR-257), bond/reconnect fixes, `docs/ble-pairing.md` | `lib/NimBLEComm` gone in v1.9.0. `lib/NanoPbComm/src/ble/Ble{Client,Server}Transport.cpp` hold bonding + security. Startup-race recovery 4f858880 (#873) | **rework** onto NanoPbComm: carry the behaviours (bond recovery, setpoint-write regression CAR-257, reconnect), not the code | PRO-655 | new NanoPbComm host tests + PRO-667 pair/re-pair/boiler-heats on HW |
| F05 | BLE scale plugin hardening (35 + 28 sec) | PRO-459, PRO-647 teardown/mutex/UAF, `BLEScale{Scan,Connect,Measurement}Policy.h`, `BLEVolumetricOverridePolicy.h` | v1.9.0 `BLEScalePlugin.cpp` has none of the policies (`git grep ScanPolicy v1.9.0` empty). Upstream added battery 73ee4b39 (#682) | **port** the policies + mutex/teardown onto upstream's plugin, and keep upstream battery reporting | PRO-655 | `test_ble_scale_{scan,connect,measurement}_policy`, `test_ble_volumetric_override_policy` |
| F06 | OTA policies + channels (56 + 55 sec) | `Ota{AsyncResolve,ChannelSwitch,ResolveHeap,ResolveReuse,UpdateCheck}Policy.h`, `OtaIntentState.h`, 95fe4fc4, PRO-394/400/554–569/599/648/649, CAR-248 `scripts/generate_stable_versions.py` | upstream rewrote the download path: `lib/OTA/src/ResumableDownloader.cpp`, `EspHttpTransport.cpp` (5c380e08 #900, aa4e3228). OOM fix for the update check 2dd893da (#755). No `installedChannel`/STABLE_VERSIONS | **rework**: adopt the upstream downloader and port the Carlos resolve/heap/channel/intent policies on top | PRO-661 | `test_ota_*` (8 suites), `test_semver_extensions`, `test_github_ota_semver_reassign`, `scripts/test_generate_stable_versions.py`, upstream `test_ota_download` |
| F07 | WebUI security: local auth, narrowed CORS, path traversal, secret masking (19 + 5 sec) | 0f5b48fb, `LocalAuthPolicy.h`, `PathTraversalPolicy.h`, CAR-96 (#84), settings secret sentinel | none. v1.9.0 `WebUIPlugin.cpp` has no `Access-Control-Allow-Origin` and no local auth (`git grep` empty) | **port** (semantic, onto upstream WebUIPlugin + `WebSocketHandler.cpp` split) | PRO-660 | `test_local_auth_policy`, `test_path_traversal_guard`, `Settings.localAuthHandoff.test.jsx` |
| F08 | Relay server + secure relay tokens + relay policy (6 + 17 sec) | `relay-server/` (13 files, CF Workers), 67541c6f, b068f462 `RelayConnectionPolicy.h` | none: no `relay-server/` in v1.9.0 (the `relay` grep hit only SmartGrindPlugin's GPIO relay) | **port** (relay-server is standalone; the display side is semantic onto WebUIPlugin) | PRO-660 | `test_relay_connection_policy`; `cd relay-server && npm test` |
| F09 | WebUIPlugin core: WS reassembly cap, broadcast/close, lifecycle deferral, ws mutex, spec gate (76 + 123 sec) | `WsReassemblyPolicy.h`, `WsBroadcastClosePolicy.h`, `WebUiLifecycleDeferPolicy.h`, `docs/websocket-api.yaml`, `scripts/check_ws_api_spec_drift.py` (PRO-610) | upstream split WS handling into `src/display/plugins/WebSocketHandler.cpp` and PSRAM-backed rx buffers (70c54ca0 #724, f3dd9d17). `docs/websocket-api.yaml` exists upstream too | **rework**: re-home the policies into WebSocketHandler, merge both YAML specs, keep the drift gate | PRO-660 | `test_ws_reassembly_cap`, `test_ws_broadcast_close_policy`, `test_webui_lifecycle_defer_policy`, `check_ws_api_spec_drift.py` + its test, `ApiService.contract.test.js` |
| F10 | Memory: mbedTLS PSRAM, NimBLE host PSRAM, heap diag, DRAM audit (21 + 6 sec) | 96e7fdf6 `MbedtlsPsramAllocator*`, 886bbc3e (PRO-567 platformio.ini NimBLE msys), 8e3b79a2 `GmHeapDiag.h`, `docs/pro-566-internal-dram-audit.md`, env `display-heapdiag` | partial: upstream `src/display/util/PsramAllocator.h` (ArduinoJson PSRAM allocator), bccb12c2 (#723), 70c54ca0 (#724). No mbedTLS/NimBLE PSRAM routing | **port** mbedTLS/NimBLE routing + heapdiag. **superseded** where Carlos PSRAMs JsonDocuments (use upstream `PsramAllocator`) | PRO-662 | `test_mbedtls_psram_allocator_policy`; PRO-662 stress runs on PSRAM + no-PSRAM boards |
| F11 | Diagnostic log plugin / ESP log tee / queue (4 + 20 sec) | f2a8764c, `DiagnosticLogPlugin.*`, `DiagLogFormat.h`, `src/display/EspLogTee.h` | none (`esp_log_set_vprintf`/DiagnosticLog absent in v1.9.0) | **port** | PRO-662 (queue/heap), then gap G1 | `test_diag_log_tee` |
| F12 | Embedded WebUI / FS / partitions (36 = F12a 8 + F12b 28) | see the two sub-rows | see sub-rows | split per commit | PRO-659 | see sub-rows |
| F12a | Embed pipeline, upstream-equivalent (10 + 2 sec) | 010a5aa1, cc5a7e49 (#196), b6e0c957, a2716e26, CAR-287 route cache 37050df6/36c2a11f/a3bbcdc3, build_webui.sh hardening fb1e09a6 (PRO-214) | upstream 3bc04041 "Embed WebUI in application partition" (#764) ships `scripts/embed_webui.py`, `embed_webui_pre.py`, `build_webui.sh` | **superseded** (take upstream). Re-check the CAR-287 route-burst fixes against upstream serving | PRO-659 | boot + `/` served with no chunk fan-out (PRO-667) |
| F12b | Carlos-only FS migration + user-data preservation + CI/sim parity (28 + 10 sec) | 394e7e4d SPIFFS→LittleFS (PRO-212), b79fa358, 3ebd1c34 profile export/import + PRO-218 restore hardening (34121e13, a07b946f, 9d0637d5, d0e683d5), CI embed matrix b074a6b5 (PRO-217), sim parity 59a2ae57/2fb5e5da (PRO-215/216), CAR-281 SPIFFS name len be68e230/453688e3, partition provenance PRO-319/322/326 | none. Upstream has no SPIFFS→LittleFS migration path or off-device profile export/import | **port** (keep the PRO-212/218 user-data preservation) | PRO-659 | host: vitest `importProfiles` restore-orchestrator suites + a new round-trip export→import test. On-device (PRO-667): **SPIFFS-era device keeps profiles/settings after OTA to the integrated build**; LittleFS mounts with no format; the restore banner round-trip restores N/N profiles; `display-sim` serves the embedded UI in CI |
| F13 | Simulator (display-sim) + Windows sim (6 + 6 sec) | `sim/platform/*` shims, `fs_shim.cpp`, PRO-207 Windows build | upstream `sim/` (60 files) + `[env:display-sim]` in v1.9.0 platformio.ini:166. No Windows support (`git grep WIN32 v1.9.0 -- sim` empty) | **rework** onto upstream sim + NanoPbComm, and port the Windows shims | PRO-656 | `pio run -e display-sim` on Linux + Windows |
| F14 | Hardware: LED, pump-target safety, autotune UI, AMOLED layout (22 + 23 sec) | per-commit list below the table | `Max31855Thermocouple`/`BoilerFillPlugin` exist in v1.9.0, but **none of the 20 commits implements either**. Behavior diff tip vs v1.9.0 is below the table | **per-commit** (see *F14 commit audit*). No blanket supersede | PRO-657 | upstream `test_autotune_simc`, `test_puckflow_latch`; PRO-657 HW matrix; BoilerFill mode-change test |
| F15 | LVGL/SquareLine display UI: Nothing theme, status/chip bars, BrewScreen, Quick Settings brightness/restart, fonts/icons (167 + 58 sec) | `src/display/ui/default/lvgl/` (76 files), `DisplayRestartPolicy.h`, `assets/fonts`, `assets/gm-icons`, CAR-292/293 | upstream moved to **EEZ Studio**: `src/display/ui/default/eez/` (57 files), `eez-ui/gaggimate.eez-project`. No `lvgl/` dir. June UI update 3452bca6 (#792) | **rework**: reapply the Carlos UI on the EEZ project. This is the biggest-risk row | PRO-658 | `test_display_restart_policy`; PRO-667 screen-by-screen visual check |
| F16 | Manual mode + manual GRIND persistence + grinder manager (42 + 48 sec) | 9cbe1b22, 9fa9d726, `process/ManualProcess.h`, `GrinderManager.*`, CAR-371 `grinderManager.js`, `test_notes_grind_setting_policy` | none: v1.9.0 `core/process/` has no ManualProcess, and there is no GrinderManager | **port** | PRO-657 (firmware process) + PRO-664 (web) | `test_notes_grind_setting_policy`, `grinderManager.test.js` |
| F17 | Standby / auto-steam / flush / steam button / auto-wakeup (14 + 63 sec) | acf03ea3 standby post-brew, `StandbyTransitionPolicy.h`, `StandbyReassertPolicy.h`, `SteamButtonPolicy.h`, `useStandbyOnBrew.js` | partial: upstream flush as a button behaviour c9c84ba3 (#720) + button rework f6534f56 (#899), `flushDuration` in v1.9.0 `Settings.h`, steam refill on mode change 64ba0d99. No auto-steam or standby-on-brew (`git grep -i autoSteam v1.9.0` empty) | **superseded** for flush/button handling (take upstream `test_button_handler`). **port** standby-post-brew, auto-steam, standby policies | PRO-657 (firmware) + PRO-664 (web) | `test_standby_transition_policy`, `test_standby_reassert_policy`, `test_steam_button_edge_policy`, upstream `test_button_handler` |
| F18 | Brew targets: volumetric source/coalesce, global weight cutoff, yield override, per-profile temp override (31 + 56 sec) | PRO-629, `VolumetricCoalescer.h`, `VolumetricMeasurementSource.h`, `GlobalWeightCutoffPolicy.h`, `BrewTemperatureOverridePolicy.h`, `ShotFinalYieldPolicy.h` | none (all symbols absent in v1.9.0). Upstream changed water accounting a0519300 (#885) | **port** and re-validate against upstream `BrewProcess.h` | PRO-657 | `test_volumetric_{target,coalesce,source_policy}`, `test_global_weight_cutoff`, `test_brew_temperature_override`, `test_shot_final_yield_policy` |
| F19 | Beans + Beanconqueror export (48 + 50 sec) | `BeanManager.*`, `req:beans:*`, `pages/Beans`, `utils/beanconqueror/`, CAR-371..375, `docs/beanconqueror-export.md` | none: no BeanManager or Beans page in v1.9.0 | **port** | PRO-664 (web) + PRO-660 (WS handlers) | `test_bean_resolution_policy`, `beanManager.test.js`, `Beans.beanconquerorExport.test.jsx`, `ShotHistory.beanconquerorExport.test.jsx` |
| F20 | Shot history / analyzer / notes / comparison / CSV / shot→profile (120 + 93 sec) | PRO-631 notes start target temp, `ShotNotesPersistencePolicy.h`, `ShotIndexMetadataPolicy.h`, `ExtendedRecordingPolicy.h`, `historyExport.js`, `shotFilters.js`, `comparisonShots.js`, `pages/ShotToProfile` | partial: upstream `ShotNotesCard.jsx`, `ShotAnalyzer/` (puck resistance f3892ed2 #905), Visualizer upload. No CSV/filters/comparison/ShotToProfile | **rework**: merge onto upstream analyzer (take upstream puck resistance), and port notes/metadata/export/filters/comparison/ShotToProfile | PRO-664 (web) + PRO-660 (plugin) | `test_shot_index_metadata`, `test_extended_recording_policy`, `ShotNotesCard.test.jsx`, `historyExport.test.js`, `shotFilters.test.js`, `detectPhases.test.js` |
| F21 | Profiles: validation, schema, keyframes, import/export (74 + 91 sec) | `test_profile_validation`, `test_strict_validation`, `StrictValidationPolicy.h`, `ProfileKeyframeChart.jsx`, `keyframeProfileLogic.js`, `schema/profile.json`, CAR-233 profile transfer | partial: upstream `ProfileManager.cpp` + `models/profile.h` validation, startup profile fa0f9c12 (#626) | **rework**: keep upstream startup profile, port strict validation + keyframes + schema extensions | PRO-664 | `test_profile_validation`, `test_strict_validation`, `keyframeProfileLogic.test.js` |
| F22 | Dashboard / Home (PRO-623..640) (145 + 90 sec) | `pages/Home/DashboardMerged.jsx`, `dashboardLogic.js`, `dashboardManager` | upstream rewrote Home (`pages/Home/cards/*`, `DashboardSidebar.jsx`, `FlushButton.jsx`) + `pages/DashboardSettings` | **port Carlos's Home UI** (decided, PRO-664). Upstream Home is not adopted as the UI. Upstream Home capabilities get wired into Carlos's Home: flush button behavior c9c84ba3 (#720), `wp` water-pumped status 6dff0448 (#931), and any other upstream status fields the slice diff finds | PRO-664 | `DashboardMerged.a11y.test.jsx` + dashboard vitest suites + new tests for flush action and `wp` readout |
| F23 | Web theme / shell / accents / a11y / connection banner (66 + 75 sec) | PRO-643 custom accents, `themeManager.js`, `PageShell.jsx`, `ConnectionBanner.jsx`, `ApiService` reconnect | partial: upstream `web/src/style.css` restyle (June UI). No accents manager or ConnectionBanner | **port** | PRO-664 | `accentContrast.test.jsx`, `PageShell.accentBadge.test.jsx`, `ConnectionBanner.test.jsx`, `ApiService.reconnect.test.js` |
| F24 | Settings, backup/restore, Google Drive, WiFi/mDNS, MQTT/HomeKit flags, EventIds, settings transactions (82 + 95 sec) | `backupBundle.js`, `GoogleDriveBackupCard.jsx`, `SettingsPersistenceTransaction.h`, `MdnsNamePolicy.h`, `MqttConnectPolicy.h`, `EventIds.h`, `config/features.h`, PRO-365 STA recovery (#355) | partial: upstream Settings split into `tabs/*Tab.jsx`, `WifiStaWatchdogPlugin`, `NetworkWatchdogPlugin`, `mDNSPlugin` | **rework**: map Carlos settings into upstream tabs. **superseded** for STA recovery if `WifiStaWatchdogPlugin` covers the HomeKit AUTH_EXPIRE case (verify in slice). **port** the rest | PRO-664 (web) + PRO-663 (watchdog) + PRO-662 (MQTT/HomeKit flags) | `test_settings_persistence_transaction`, `test_mqtt_connect_policy`, `test_event_system`, `backupBundle.test.js`, `Settings.*.test.jsx` |
| F25 | CI / quality: ci.yml, check/pr-flash/nightly/beta, clang-tidy, cppcheck, sanitize, extra PIO envs, promotion + stable-versions scripts, flash.sh (76 + 59 sec) | `.github/workflows/{ci,check,pr-flash,build-nightly,build-beta,deploy-web}.yml`, `[env:native-sanitize]`, `[env:native-tidy]`, `display-flags-off`, `display-no-*`, `display-{lilygo,amoled,waveshare}`, `scripts/select_tidy_sources.py`, `generate_promotion_pr_body.py` (PRO-644), CAR-341, PRO-608..611 | partial: upstream `.github/workflows/{build,build-nightly,check,pr-flash,ota-testbench}.yml`. No ci.yml / tidy / sanitize / flag-off envs | **port** gates + envs. **rework** workflow files to merge with upstream's (keep `ota-testbench.yml`) | PRO-667 (+ PRO-665 for env cleanup) | CI green on the integration branch; `scripts/test_*.py` |
| F26 | Docs / AGENTS / plans / specs (22 + 4 sec) | `docs/superpowers/*`, `AGENTS.md`, `CLAUDE.md`, `CONTRIBUTING.md`, `.mailmap` | n/a | **port** AGENTS/CONTRIBUTING (update for NanoPb/EEZ). **drop** stale plans/specs | PRO-665 | doc review |
| F27 | Web misc, not otherwise clustered (9 + 3 sec) | see appendix | per-commit | **port** (triage in the slice) | PRO-664 | vitest suite |
| F28 | Firmware misc: Controller race/isActiveSafe fixes, rule-of-5, dead code (10) | a83fda2f, cfe1d418, PRO-378, PRO-380, CAR-101 | per-commit. Controller.cpp was heavily rewritten upstream | **rework**: re-check each against upstream Controller | PRO-663 | `test_change_mode_defer_policy`; native-sanitize |
| F99 | Content-free sync merges only (9) | `Merge remote-tracking branch 'origin/master'`, `Merge branch 'master'`, CAR-401 / PRO-635 merge-master PRs | n/a | **drop** (asserted: only merges matching `SYNC_MERGE_RE`). PRO-485 60481366 moved to F24 and 0ad43dbf moved to F25 | none needed | script assert |

### F14 commit audit (explicit preserve/drop)

Behavior diff tip vs v1.9.0 (`git diff v1.9.0 origin/dev-master`):
- **MAX_SAFE_TEMP location**: v1.9.0 declares it in `peripherals/TemperatureSensor.h`, and the
  interface there has a virtual dtor + `setup()`. Carlos's tree declares it in
  `peripherals/Max31855Thermocouple.h`, and `TemperatureSensor` has no dtor/`setup()`. **Take
  upstream's layout.** Keep the value (170.0) and make sure Carlos includes resolve it.
- **BoilerFill steam→refill**: v1.9.0 refills when leaving STEAM for any mode except STEAM/STANDBY
  (water/grind included). That is upstream `64ba0d99` "Trigger steam refill on change to
  water/grind". Correction to the review: `git merge-base --is-ancestor 64ba0d99 v1.9.0` succeeds
  and `git tag --contains` lists `v1.9.0`, so it **is in the tag**, not post-tag. The PRO-666
  post-tag delta is therefore not needed for this behavior. Carlos's tip refills only on
  STEAM→BREW. **Take upstream.** Keep only Carlos's `EventIds::` constants (F24/PRO-24).
- **Max31855Thermocouple.cpp / BoilerFill logic**: no Carlos implementation commits exist in range.
  The only touches are 20794eda (EventIds), ea232239 (#356), e9723b5b (reverted 3.x), and d7dbeb39.

| sha | what | decision |
|---|---|---|
| 95a3a82b | LED re-sync to controller after BLE reconnect (PRO-258) | **preserve**: re-apply onto upstream LED fixes 9e9eecb9/6833963d if they don't cover it |
| 1864c877 | rollover-safe LED loop timing + init sentinel (PRO-41/46) | **preserve** (diff vs upstream LED plugin) |
| 59981731 | `isActiveSafe()` in LED control | **preserve** (`isActiveSafe` absent in v1.9.0) |
| b6e0c713 | `isActiveSafe()` in autotune() | **preserve** |
| 54c42c21 | FINISHED-phase guard before pump methods | **preserve** |
| eaf66f9e, 1abab54f, 86501408, 39d3ae70, ffbb3372, cfa89384 | CAR-336 `hasPumpTarget` capability gate / mutex / standby pump-target readout | **preserve** (`hasPumpTarget` absent in v1.9.0) |
| 3a4df82a, a1d7d105 | manual profileId sentinel, revert pump-mode guard (net change) | **preserve** 3a4df82a with F16 ManualProcess. a1d7d105 is a revert pair, so **drop** it |
| 510b4a20, 426172e5, 2d91e277 | autotune results UI, 60s copy, Back label (PRO-26/457/458) | **preserve** in web, rebased onto upstream SIMC autotune 48802e20 result format |
| 4e8cb22d | round-AMOLED BrewScreen sub-state layout (CAR-315) | **preserve** with F15 (PRO-663) |
| b168082d | StatusScreen status_time breadcrumb doc | **drop** (doc-only breadcrumb) |
| 21ba2a6a | early bulk web revamp (beans + AMOLED styling) | **drop** as a unit. Its content is superseded by the later F19/F23 commits |
| 253833e1 | log `pb_encode` failure in `make_system_info` (PRO-310, `GaggiMateController/utilities.h`) | **drop**: v1.9.0's `make_system_info` builds JSON (no `pb_encode`); re-apply only if the nanopb transport (F03) is ported |

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
- **Re-audit after reclassification**: I re-ran the `git apply --check` partition on a clean
  `origin/dev-master` worktree and got the same **11 / 4 / 34** (49 total). The partition is
  patch-based, so reclassification cannot change it. The new feature mapping of the 49: reverted 11 =
  F02×6, F10×3, F09×1, F24×1 (fd25daf5 3.x doc); present 4 = F05, F13, F09×2; modified 34 = F02×4 (all drop),
  and 30 live across F04/F05/F06/F07/F09/F10/F12b/F21/F22/F23/F24/F25. None landed in F03/F14/F99.

## Seed-list cross-check

| Seed item | Commit/ID | Mapped feature | Row |
|---|---|---|---|
| NimBLE host PSRAM | 886bbc3e | F10 | port, PRO-662 (verify NimBLE 1.4.x / NanoPb path keeps `MEM_ALLOC_MODE_EXTERNAL`) |
| BLE scale UAF/teardown | PRO-459, PRO-647 (e946cee4) | F05 | port, PRO-655 |
| OTA flash eligibility / channels | 95fe4fc4, PRO-394 a13ef966, PRO-400/554–569/599/648 | F06 | rework, PRO-661 |
| PRO-649 | Done issue ("OTA behavior remains broken after dev-master flash"). Its investigation closed with "no code shipped", so no commit/PR cites it | none | **G2 resolved**: closed without a tagged commit/PR |
| Local auth + CORS, relay tokens, relay policy | 0f5b48fb, 67541c6f, b068f462 | F07, F07, F08 | port, PRO-660 |
| mbedTLS PSRAM, diag log queue, DRAM audit | 96e7fdf6, f2a8764c, 8e3b79a2 | F10 | port, PRO-662 |
| Manual GRIND, active-shot grind, standby post-brew | 9cbe1b22, 9fa9d726, acf03ea3 | F16, F16, F17 | port |
| Per-profile temp override | PRO-629 → f39cb9db (#626). The subject has no ID | F18 | port, PRO-657 |
| Shot Notes start temp / accents | PRO-631, PRO-643 | F18 / F22 (keyword spill, see Method 1) | port, PRO-664 |
| Windows sim | PRO-207 → 4a2380b4 (CAR-399, #191) | F13 | rework, PRO-656 |
| Max31855, BoilerFill | lib/GaggiMateController, BoilerFillPlugin | F14 | take upstream (incl. in-tag 64ba0d99). The MAX_SAFE_TEMP header move is noted; see *F14 commit audit* |
| Infra envs / CI / tidy / cppcheck / scripts | CAR-341, PRO-608..611, PRO-644 | F25 | port, PRO-667 |

## Gaps and risks

- **G1 Diagnostic log plugin (F11)**: **resolved**. Owner PRO-662 (approved), covering
  DiagnosticLogPlugin + EspLogTee + the PSRAM queue.
- **G2 PRO-649**: **resolved**. It is a real Done issue ("OTA behavior remains broken after
  dev-master flash"), closed without a tagged commit/PR. Its investigation concluded "no code
  shipped", so nothing needs porting.
- **G3 Relay server (F08)**: **resolved**. Owner PRO-660 (approved), including the `relay-server/`
  Node/Cloudflare package and its CI/deploy.
- **G4 Beans/grinder/manual-mode firmware (F16/F19)**: **resolved**. WS handler owner PRO-660 (approved).
- **G5 Dependabot config + GitHub Pages deploy (`deploy-web.yml`)**: **resolved**. Owner PRO-667 (approved).
- **R1 EEZ UI (F15, 157 commits)** is the largest and riskiest row. All LVGL work must be redone.
- **R2 Dashboard (F22)**: **resolved**. Decision: port Carlos's Home UI (PRO-664). Upstream Home
  capabilities (flush button c9c84ba3, `wp` status #931, …) get wired into it.
- **R3 Test debt (X1)**: upstream has no web unit tests, so every Carlos vitest suite must come
  across or the regressions stay silent.

## QA self-review

- **Enumerated 1321/1321**: the coverage assert passed, and the table primary counts sum to 1321.
- **Classification: deterministic consistency check (review round 2).** Random sampling kept finding
  new misfiles because (a) subject keywords disagreed with what a commit touched and (b) one
  feature per commit stranded mixed commits. Replaced with checks over every commit:
  - `PATH_RULES` owns every one of the 782 distinct paths touched in range (incl. merge diffs; asserted, 0 unmapped).
  - **Mismatches flagged: 387 / 1321** (subject feature not among path features; sync merges and
    docs/lockfile-only commits exempt by construction). Resolution counts:
    - path rule won (primary = top path feature by lines changed): **305**
    - `CROSSCUT` subject kept (deps/platform/security/memory/FS-migration, path features secondary): **60**
    - pre-existing hand-audited `OVERRIDES`: **10**
    - new `MISMATCH_OVERRIDES` with a one-line reason each: **12**: 0f67772d (relay primary, 5
      secondaries), plus 11 commits already decided commit-by-commit in the F03/F12a/F14 tables
      (2fdb8b22, a3bbcdc3, 37050df6, 36c2a11f, 39d3ae70, ffbb3372, 4e8cb22d, b168082d, a1d7d105,
      3a4df82a, 21ba2a6a), kept where audited.
    - **unresolved: 0** (asserted `unresolved_mismatches == 0`).
  - Two commits newly joined audited rows by path and were decided here: fb1e09a6
    (build_webui.sh hardening) → F12a; 253833e1 (PRO-310 pb_encode log) → F14, **drop** (v1.9.0's
    `make_system_info` is JSON).
  - **343 primaries moved** vs `00d8738e`, mostly subject-keyword → owning page (F17/F16/F18 → F22
    Home, F06 → F09 WebUIPlugin, F15 → F23 web theme).
- **Secondary owners: 356 commits** (round 3) have at least one. Per feature: F03 3, F04 13, F05 17, F06 33,
  F07 5, F08 9, F09 119, F10 6, F11 16, F12a 2, F12b 10, F13 3, F14 23, F15 32, F16 17, F17 12,
  F18 29, F19 25, F20 87, F21 65, F22 60, F23 65, F24 88, F25 53, F27 3.
- **Review round-2 findings**: 88167700 → F20 (path-wins), b3de8474 → F09 (path-wins), both seeded;
  0f67772d → F08 primary + F20/F09/F22/F23/F15 secondaries (seeded in `SEEDS` + `SECONDARY_SEEDS`).
- **Review round-3: subject keywords are tokens.** Root cause of 2aa4ba67 → F22: subject regexes
  had no word boundaries (`ring` in "declaring"/"bring"/"during", `oom` in "zoom"/"headroom", `card`
  in "discard"). Every top-level alternative is now wrapped in token boundaries (string edge,
  non-letter, or CamelCase/acronym boundary, so `getRingVisual`/`BLEScale` still hit), and a guard
  asserts every subject hit is a whole token. **16 commits reclassified** vs `f51df839` (each
  reviewed): F10→F15 e7dee658/8c5e2b82/458fb1da (zoom icons); F10→F12a 652a5ff2/432ea5ce (headroom);
  F22→F28 2aa4ba67; F22→F25 06e7f088 (empty-diff sync squash); F22→F14 91fe8f20 (sync squash adding
  the display-headless-4m env); F15→F17 3a5e735e; F15→F09 4e94f1b5 (path-wins); plus 6 from the
  neutral-only audit below. Mismatches now 367 (path-wins 292, crosscut 57,
  override 10, mismatch-override 8), unresolved 0.
- **neutral-only-subject bucket: 49 commits** whose only non-doc paths are NEUTRAL hubs
  (Controller.*, Plugin*.h, main.cpp, ApiService, routes, platformio.ini, .gitignore), where the
  mismatch check is blind. All 49 hand-verified (`NEUTRAL_VERIFIED`, asserted complete and not
  stale): 38 subject confirmed, 11 overridden, of which 6 moved here: 69c848a0 F26→F28, 944b62bc
  F17→F14, d4db7d4c F08→F09, 866aaaa3 F24→F04, 0378b7f8 F06→F25, fdf6a399 F06→F13.
- Audit replay: seed 29092026 **0 / 25** errors; seed 515152 **0 / 25** errors.
- **Reviewer's audit replayed** (`python3 scripts/v19_audit_seed.py`: `random.seed(29092026)`, 25
  rows; a row fails if any path feature from `git show --name-only` is neither primary nor secondary,
  or the primary is outside its path features without a documented resolution): **0 / 25 errors**.
  It draws the same 25 commits the reviewer drew, including all three findings.
- Earlier rounds (seeds 686/652/6520: 4 + 3 + 2 errors) remain overridden and seeded.
- The memory seeds (96e7fdf6/f2a8764c/8e3b79a2/886bbc3e) and PRO-647 misroutes from the first QA stay fixed.
- No firmware or web code changes. Only docs plus read-only generator/audit scripts.
