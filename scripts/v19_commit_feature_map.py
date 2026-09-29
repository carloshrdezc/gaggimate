#!/usr/bin/env python3
"""Map every Carlos-only commit (v1.9.0..origin/dev-master) to features (PRO-652).

Each commit gets path_features ({feature: lines changed}) from PATH_RULES over its touched paths
(merges: first-parent diff). One primary: OVERRIDES, then MISMATCH_OVERRIDES, then content-free sync
merges -> F99, then subject RULES. If the subject feature is not among path_features (a mismatch),
the top path feature wins unless the subject feature is CROSSCUT. Every other path feature is a
secondary owner. Asserts: enumeration == rev-list count, SEEDS/SECONDARY_SEEDS hold, F99 is
sync-only, no path lacks a PATH_RULES owner, and unresolved_mismatches == 0.

Outputs (beside the matrix doc):
  docs/upstream-v1.9-preservation-matrix.csv       one row per commit
  docs/upstream-v1.9-preservation-matrix.appendix.md  per-feature commit lists

Usage: python3 scripts/v19_commit_feature_map.py [--base v1.9.0] [--tip origin/dev-master]
"""
import argparse
import collections
import csv
import re
import subprocess
import sys

# (feature_id, subject_regex, path_regex). Subject is matched case-insensitively.
# Path regex matches if ANY touched path matches; used only when no subject rule hit.
RULES = [
    ("F01-deps", r"^build\(deps|dependabot|bump .* from .* to ", None),
    ("F02-platform-stack", r"arduino.?(esp32 )?3|nimble.?(-arduino )?(1\.4|2\.?x|2\.)|c\+\+ ?20|gnu\+\+|"
     r"espressif32|platform.bump|roll dev-master back|pioarduino|idf ?5|pro-29[01]|pro-27\d\b", None),
    # Memory seed items mention OTA/BLE in their subjects; claim them first (PRO-566..568).
    ("F10-memory-psram", r"psram|mbedtls|internal.dram|heap.?diag|pro-56[678]\b", None),
    # PRO-245..259 is NOT a spike range (review #686 F1): only nanopb subjects/paths land here.
    ("F03-nanopb-spike", r"nanopb", r"^(lib/NanoPbSpike|docs/spike-nanopb)"),
    ("F05-ble-scale", r"scaled?|blescale|pro-459|pro-647", r"BLEScale"),
    ("F04-ble-comms", r"nimblecomm|\bble\b.*(controller|bond|encrypt|write_enc|pair|reconnect|mtu)|"
     r"write_enc|bond|controller (link|connection)|\(ble\)", r"^(lib/NimBLEComm|lib/GaggiMateController/src/.*Comm|docs/ble-pairing)"),
    ("F06-ota", r"\bota\b|firmware update|update check|stable.versions|installedchannel|semver|"
     r"pro-(39[4-9]|400|55[4-9]|56[0-9]|599|648|649)\b", r"^(lib/OTA|lib/ble_ota_dfu|scripts/.*stable_versions)"),
    ("F07-webui-security", r"local.?auth|cors|relay token|path.traversal|secret|mask|security|"
     r"\bauth\b|pro-3[0-9]{2}.*auth", r"(LocalAuthPolicy|PathTraversalPolicy)"),
    ("F08-relay", r"relay|wrangler|cloudflare", r"^(relay-server/|src/display/plugins/Relay|web/src/.*[Rr]elay)"),
    ("F09-webui-plugin-core", r"websocket|\bws\b|webuiplugin|reassembl[a-z]*|broadcast|lifecycle.?defer|\(webui\)|\(api\)|\(ws\)",
     r"(WebUIPlugin|WsReassembly|WsBroadcast|WebUiLifecycle|docs/websocket-api|check_ws_api_spec)"),
    ("F10-memory-psram", r"psram|heap|dram|mbedtls|memory|oom|heapdiag|pro-566|leak",
     r"(GmHeapDiag|MbedtlsPsram|PsramAllocator|pro-566)"),
    ("F11-diag-log", r"diag(nostics?)?|log tee|udp log|esp_log|logging|\blog\b", r"(DiagnosticLog|EspLogTee|DiagLog)"),
    # F12a = upstream-equivalent embed pipeline (superseded by 3bc04041);
    # F12b = Carlos-only FS migration / user-data preservation / CI+sim parity (port, PRO-659).
    ("F12b-fs-migration-preserve", r"littlefs|spiffs|filesystem|\(fs\)|\(migration\)|docs\(migration|partition|"
     r"pro-21[2568]\b", r"(build_spiffs|partitions)"),
    ("F12a-embed-webui", r"embed|webassets|build_webui", r"(embed_webui|webassets|scripts/build_webui)"),
    ("F13-simulator", r"\bsim\b|simulator|display-sim|\(sim\)|windows sim|pro-207", r"^sim/"),
    ("F14-hardware", r"max31855|thermocouple|boiler.?fill|pump|adc|pressure sensor|board def|dimmer|"
     r"\bpsm\b|lilygo|amoled|waveshare|\bpid\b|autotune|heater|\bled\b",
     r"(lib/GaggiMateController|lib/NayrodPID|src/controller/|BoilerFill|src/display/drivers/|boards/)"),
    ("F15-eez-lvgl-ui", r"\(ui\)|lvgl|eez|brewscreen|status.?bar|chip.?bar|modescreen|menu.?screen|"
     r"quick settings|brightness|nothing|ndot|font|icon|\(display\).*(screen|ui)|standby screen|clock",
     r"^(src/display/ui/|assets/(fonts|gm-icons))"),
    ("F16-manual-grind", r"manual|grind(er)?\b|\(grind\)", r"(ManualProcess|GrindProcess|GrinderManager)"),
    ("F17-standby-steam-flush", r"standby|auto.?steam|steam|flush|autowakeup|auto.wake|water|preinfus[a-z]*",
     r"(AutoWakeup|SteamProcess|useStandbyOnBrew)"),
    ("F18-brew-targets", r"volumetric|weight cutoff|yield|temp(erature)? override|target temp|dose|"
     r"pro-629|brew temp", r"(Volumetric|GlobalWeightCutoff|BrewTemperatureOverride|BrewProcess)"),
    ("F19-beans", r"\bbeans?\b|beanconqueror|car-37[1-5]|roast", r"(BeanManager|beanManager|beanconqueror|pages/Beans)"),
    ("F20-shot-history-analyzer", r"shot.?history|analy[sz]er|shot notes|notes|comparison|visualizer|"
     r"history|csv|shot.to.profile|pro-631|\bshots?\b",
     r"(ShotHistory|ShotAnalyzer|ShotToProfile|comparisonShots|historyExport)"),
    ("F21-profiles", r"profile|keyframe|schema|phase|import|meticulous|decent",
     r"(ProfileManager|ProfileEdit|ProfileList|models/profile|test_profile_validation|data/p/)"),
    ("F22-dashboard-home", r"dashboard|\(home\)|\bhome\b|hero|pill|ring|card|pro-6(2[3-9]|3\d|40)\b",
     r"(pages/Home/|dashboardManager|DashboardMerged)"),
    ("F23-web-theme-shell", r"theme|accent|daisyui|style|dark|light|color|colour|navigation|nav\b|"
     r"page.?shell|a11y|contrast|pro-643|connection banner|reconnect",
     r"(themeManager|style\.css|PageShell|ConnectionBanner|components/)"),
    ("F24-settings-backup", r"settings|backup|restore|google drive|export|wifi|mdns|homekit|mqtt|"
     r"strict.?valid|event.?id",
     r"(pages/Settings|Settings\.(cpp|h)|StrictValidation|backupBundle|MQTTPlugin|HomekitPlugin|EventIds|mDNS)"),
    ("F25-ci-quality", r"\bci\b|\(ci\)|workflow|clang-tidy|tidy|cppcheck|sanitiz[a-z]*|lint|prettier|eslint|"
     r"format(ting|ted)?|vitest|test|coverage|flash script|gitignore|worktree|promotion|nightly|beta|release|car-341|pro-6(0[89]|1[01]|44)\b",
     r"^(\.github/|scripts/|test/|\.clang)"),
    ("F26-docs-agents", r"^docs|readme|agents|claude\.md|contributing|plan|spec|spike|mailmap",
     r"^(docs/|AGENTS\.md|CLAUDE\.md|README|CONTRIBUTING|\.mailmap)"),
    ("F27-web-misc", None, r"^web/"),
    ("F28-firmware-misc", None, r"^(src/|lib/|platformio\.ini)"),
]
FALLBACK = "F99-misc"
# Path ownership (path regex -> feature). First match wins. NEUTRAL = shared hub / docs /
# lockfile / build-config / stray-artifact paths that carry no single feature's ownership;
# they never create a secondary owner and never trigger a mismatch (the documented exception).
NEUTRAL = "-"
PATH_RULES = [
    # neutral: docs, lockfiles/manifests, build config, hubs, stray artifacts
    (r"^docs/|\.md$|^README|^\.mailmap$|^\.bob/|^\.claude/|^\.playwright-mcp/|^output/|"
     r"^[^/]+\.png$|^web/[^/]+\.(png|md)$|^web/\.vercel/|^\.clabot$", NEUTRAL),
    (r"(^|/)package(-lock)?\.json$|^platformio\.ini$|^\.gitignore$|^web/\.gitignore$", NEUTRAL),
    (r"^src/display/core/(Controller|PluginManager|Plugin|constants|utils)\.|^src/display/main\.cpp$|"
     r"^src/display/(config|lv_conf\.h)|^web/src/(index|routes|routeFactory|components/App)\b|"
     r"^web/src/services/ApiService\.(js|jsx|test\.js)$|^web/src/services/ApiService\.contract|"
     r"^test/native/|^test/README|^test/tidy/", NEUTRAL),
    (r"^\.github/|^\.clang|^scripts/(format|select_tidy|test_select_tidy|tidy_host|boot_smoke|"
     r"make_pcb|generate_promotion|test_generate_promotion|auto_firmware)|^flash\.sh$|^web/eslint", "F25-ci-quality"),
    (r"NanoPbSpike|nanopb|^lib/NimBLEComm/proto/|test_nanopb", "F03-nanopb-spike"),
    (r"^relay-server/|Relay|relayConfig|SslRelayStartup", "F08-relay"),
    (r"BLEScale|BLEVolumetric|pages/Scales|bleScale|remote_scales", "F05-ble-scale"),
    (r"^lib/NimBLEComm/|Comm\.(h|cpp)$|^sim/comms/|test_ble_auth|ble-pairing", "F04-ble-comms"),
    (r"^lib/OTA/|^lib/ble_ota_dfu/|Ota[A-Z]|test_ota_|GitHubOTA|semver|stable_versions|pages/OTA", "F06-ota"),
    (r"LocalAuth|localAuth|PathTraversal|authenticateLocal", "F07-webui-security"),
    (r"WebUIPlugin|WsReassembly|WsBroadcast|WebUiLifecycle|ws_reassembly|ws_broadcast|webui_lifecycle|"
     r"websocket-api|check_ws_api_spec|ESPAsyncWebServer|WebSocketsClient|ApiService\.(reconnect|validation)",
     "F09-webui-plugin-core"),
    (r"HeapDiag|Psram|psram|mbedtls|esp_heap_caps", "F10-memory-psram"),
    (r"Diag(nostic)?Log|DiagLogFormat|EspLogTee|UdpLogTee|udp_log|diag_log|esp_log|esp32-hal-log", "F11-diag-log"),
    (r"embed_webui|webassets|build_webui", "F12a-embed-webui"),
    (r"build_spiffs|check_spiffs|partition|^boards/.*\.csv$|MigrationWarning|SdReadRetry|sd_read_retry|"
     r"ProfileReadSize|profile_read_size", "F12b-fs-migration-preserve"),
    (r"^sim/|sim_run|sim_sdl", "F13-simulator"),
    (r"^lib/(GaggiMateController|NayrodPID)/|^src/controller/|BoilerFill|LedControl|^src/display/drivers/|"
     r"^boards/|pages/Autotune", "F14-hardware"),
    (r"^src/display/ui/|^ui/|^icons/|^assets/(fonts|gm-icons)/|^data/fonts/|^web/public/fonts/", "F15-eez-lvgl-ui"),
    (r"ManualProcess|GrindProcess|GrinderManager|grinderManager|SmartGrind|GrindTargetBar|useGrindSettings|"
     r"ManualGrind|notes_grind|change_mode_defer|ChangeModeDefer", "F16-manual-grind"),
    (r"AutoWakeup|SteamProcess|SteamButton|steam_button|Standby|standby|useAutoSteam|PumpProcess|"
     r"PostStopGrace", "F17-standby-steam-flush"),
    (r"Volumetric|volumetric|GlobalWeightCutoff|global_weight|BrewTemperature|brew_temperature|BrewProcess|"
     r"ShotFinalYield|shot_final_yield|useShotDoseRecorder|predictive|TemperatureControls|ActiveShotFill",
     "F18-brew-targets"),
    (r"BeanManager|beanManager|Beanconqueror|beanconqueror|BeanResolution|bean_resolution|pages/Beans|"
     r"ProfileBeanSelectors", "F19-beans"),
    (r"ShotHistory|ShotAnalyzer|ShotToProfile|ShotIndex|shot_index|ShotNotes|ExtendedRecording|"
     r"extended_recording|shot_log_format|comparisonShots|VisualizerService|VisualizerUpload|pages/Statistics|"
     r"Rating|ratings|shotMath|OverviewChart|components/Chart|download\.js", "F20-shot-history-analyzer"),
    (r"ProfileManager|ProfileEnumeration|profile_enumeration|pages/Profile|models/profile|profile_validation|"
     r"parse_profile|static_profiles|^data/p/|^schema/|ExtendedProfileChart|ProcessProfileChart|"
     r"pendingProfile|useProfileData|ImportButton|Process\.h$", "F21-profiles"),
    (r"pages/Home/|dashboardManager|homeConstants|ModeIdleDisplay|ModeTabBar|ProcessDisplay|"
     r"StatusMetricsRow|useProcessActions|useControlsVisibility|useStandbyOnBrew", "F22-dashboard-home"),
    (r"style\.css|themeManager|accentContrast|PageShell|ConnectionBanner|components/|pages/_404|"
     r"web/index\.html|web/src/__tests__/|web/vite\.config", "F23-web-theme-shell"),
    (r"pages/Settings|Settings(Persistence)?|settings_persistence|StrictValidation|strict_validation|"
     r"backupBundle|cloudBackup|googleDrive|MQTTPlugin|MqttConnect|mqtt_connect|HomekitPlugin|EventIds|"
     r"event_system|mDNS|Mdns|WiFi|wifi_fallback|Preferences|display_restart", "F24-settings-backup"),
    (r"^scripts/|^test/", "F25-ci-quality"),
    (r"^web/", "F27-web-misc"),
    (r"^src/|^lib/", "F28-firmware-misc"),
]
PATH_RULES_C = [(re.compile(p), f) for p, f in PATH_RULES]
# Concern-defined (not location-defined) features: their work lands in other features' files by
# nature (a dep bump edits .github/, a PSRAM move edits DiagnosticLogPlugin). On a subject/path
# mismatch the subject stays primary and EVERY path feature is recorded as secondary.
CROSSCUT = {"F01-deps", "F02-platform-stack", "F07-webui-security", "F10-memory-psram",
            "F12b-fs-migration-preserve"}
# Resolved mismatches where the path rule does NOT win: sha -> (primary, one-line reason).
MISMATCH_OVERRIDES = {
    "0f67772d": ("F08-relay", "relay-task teardown race is the headline fix (PRO-660); its code lives "
                              "in WebUIPlugin, the other 4 owners are secondaries"),
    # Rows audited commit-by-commit in round 1 (F03/F12a/F14 tables): keep the audited decision.
    "2fdb8b22": ("F03-nanopb-spike", "nanopb slice 5 (PRO-245) helper removal; F03 row audits it"),
    "a3bbcdc3": ("F12a-embed-webui", "CAR-287 embedded route cache; listed in the F12a row"),
    "37050df6": ("F12a-embed-webui", "CAR-287 embedded route request bursts; listed in the F12a row"),
    "36c2a11f": ("F12a-embed-webui", "CAR-287 embedded route chunk fanout; listed in the F12a row"),
    "39d3ae70": ("F14-hardware", "CAR-336 pump-target safety; in the F14 preserve/drop audit"),
    "ffbb3372": ("F14-hardware", "CAR-336 pump-target 0 readout; in the F14 preserve/drop audit"),
    "4e8cb22d": ("F14-hardware", "AMOLED BrewScreen layout; in the F14 preserve/drop audit"),
    "b168082d": ("F14-hardware", "AMOLED StatusScreen breadcrumb; in the F14 preserve/drop audit"),
    "a1d7d105": ("F14-hardware", "manual-shot pump-mode guard revert; in the F14 preserve/drop audit"),
    "3a4df82a": ("F14-hardware", "manual-shot pump limits sentinel; in the F14 preserve/drop audit"),
    "21ba2a6a": ("F14-hardware", "AMOLED web styling revamp; in the F14 preserve/drop audit"),
}
# Hand-audited overrides (review #686). sha prefix -> feature. Applied before rules.
OVERRIDES = {
    # ex-F03 (PRO-245..259 regex spill)
    "ef60874d": "F05-ble-scale", "2782b14c": "F05-ble-scale",          # PRO-248 steam scale/drip recording
    "05c01878": "F25-ci-quality", "b255cd0b": "F25-ci-quality",        # PRO-250 gh-pages CI
    "eec9cfea": "F24-settings-backup",                                 # PRO-252 settings import auto-wakeup
    "2aaad864": "F23-web-theme-shell",                                 # PRO-253 lazy-route nav
    "95a3a82b": "F14-hardware",                                        # PRO-258 LED re-sync on reconnect
    "74932051": "F26-docs-agents",                                     # PRO-259 uploadfs docs
    "39e73683": "F01-deps", "4a731499": "F01-deps",                    # PRO-254/256 dep bumps
    "9a836031": "F25-ci-quality", "ce9774dc": "F25-ci-quality",        # PRO-246/247 dev-master<->master sync squashes
    # review sample misclusters
    "e73a01e8": "F19-beans", "883e4bdf": "F19-beans",
    "d90a77ae": "F23-web-theme-shell", "0ad43dbf": "F25-ci-quality",
    "60481366": "F24-settings-backup",                                 # PRO-485 HA topic call-site (MQTT)
    # re-audit seed 652 (40 rows) errors
    "5c0393d3": "F09-webui-plugin-core", "010a5aa1": "F12a-embed-webui", "21d86108": "F15-eez-lvgl-ui",
    # re-audit seed 6520 (40 rows) errors
    "6c24a5d1": "F25-ci-quality", "2cf2e37a": "F24-settings-backup",
    # F12 split: the three CI/sim embed-parity commits are Carlos-only (port)
    "b074a6b5": "F12b-fs-migration-preserve", "b30bc7b7": "F12b-fs-migration-preserve",
    "59a2ae57": "F12b-fs-migration-preserve", "01843ca2": "F12b-fs-migration-preserve",
    "2fb5e5da": "F12b-fs-migration-preserve",
    # review #686 round 3: squash-style "chore: sync master into dev-master" (not merges, so not F99).
    "06e7f088": "F25-ci-quality",      # PRO-318: empty diff (tag carry-forward), same class as 9a836031
    "91fe8f20": "F14-hardware",        # PRO-315: real content = new display-headless-4m board env
    "2aa4ba67": "F28-firmware-misc",   # PRO-384: Plugin rule-of-5 invariant doc (core, not F22 "declaring")
    # PRO-211 embed-webui headroom doc + its merge: keep together (were F10 via "headroom" -> `oom`)
    "652a5ff2": "F12a-embed-webui", "432ea5ce": "F12a-embed-webui",
    # neutral-only-subject audit (round 3): subject keyword chose the wrong feature on a hub-only diff
    "69c848a0": "F28-firmware-misc",   # Controller::getProcess() deprecation is code, not docs
    "944b62bc": "F14-hardware",        # CAR-336 pump-target family (siblings eaf66f9e/86501408 in F14)
    "d4db7d4c": "F09-webui-plugin-core",  # GAGGIMATE_ENABLE_WEBUI gate: WebUIPlugin, not relay
    "866aaaa3": "F04-ble-comms",       # WiFi modem-sleep before BLE init = BLE coexistence, not settings
    "0378b7f8": "F25-ci-quality",      # [env:native] lib_ignore doc = host-test env, not OTA
    "fdf6a399": "F13-simulator",       # display-sim env build fix, not stable-versions/OTA
}
# neutral-only-subject bucket (round 3): commits whose only non-doc paths are NEUTRAL hubs, so the
# subject alone decided and the mismatch check is blind. Every one is hand-verified here (or above in
# OVERRIDES); the run fails on an unverified or stale entry.
NEUTRAL_VERIFIED = set("""
886bbc3e 5bcd651b d974d618 7ce001a1 5c0393d3 2aa4ba67 f8ecec73 efbf01c6 7df4ccb3 e5445ca6 12cdbc00
866aaaa3 e6ca71c6 39e73683 0378b7f8 2aaad864 2fb5e5da 72627a02 fdf6a399 d4db7d4c 5553e27e 95e3de58
2b579771 01c5fd1c 0f14b2aa 0f03488b e9e5b802 e56c7616 270ef3b2 350163af eaf66f9e 1abab54f 86501408
cfa89384 944b62bc 3e327bb9 e49d7efe 3a5e735e 40ce2e95 669e654a a2a6adf4 5685668c e07fdadc b8d317e6
b2e3834b 5818f13a b6e0c713 54c42c21 69c848a0
""".split())
# Non-doc NEUTRAL paths (hubs + build config); docs/lockfiles alone don't qualify.
HUB_RE = re.compile(PATH_RULES_C[2][0].pattern + r"|^platformio\.ini$|(^|/)\.gitignore$")
# Seeds that MUST land where stated, or the script fails (guards against rule regressions).
SEEDS = dict(OVERRIDES, **{
    "72da9327": "F03-nanopb-spike", "33751012": "F03-nanopb-spike", "5a91167c": "F03-nanopb-spike",
    "394e7e4d": "F12b-fs-migration-preserve", "3ebd1c34": "F12b-fs-migration-preserve",
    "b79fa358": "F12b-fs-migration-preserve", "be68e230": "F12b-fs-migration-preserve",
    "ea232239": "F02-platform-stack", "96e7fdf6": "F10-memory-psram", "f2a8764c": "F10-memory-psram",
    # review #686 round 2 (seed 29092026): path-derived primaries
    "88167700": "F20-shot-history-analyzer", "b3de8474": "F09-webui-plugin-core", "0f67772d": "F08-relay",
})
# Secondary owners that MUST be recorded (review #686 round 2, finding 3).
SECONDARY_SEEDS = {"0f67772d": {"F20-shot-history-analyzer", "F09-webui-plugin-core", "F15-eez-lvgl-ui",
                                "F22-dashboard-home", "F23-web-theme-shell"}}
# F99 may hold only content-free sync merges: merge commits whose subject is a plain branch sync.
SYNC_MERGE_RE = re.compile(r"^Merge (remote-tracking )?branch '(origin/)?(master|dev-master)'|"
                           r"^Merge pull request #\d+ from \S+/\S*merge-master", re.I)
# Subject keywords are TOKENS, never substrings (review #686 round 3: `ring` hit "declaring",
# `oom` hit "zoom", `card` hit "discard"). Every top-level alternative of a subject regex is
# wrapped in token boundaries: it must start at a string edge / non-letter / CamelCase or acronym
# boundary (`getRingVisual`, `useShotDoseRecorder`, `BLEScale`) and end at one, after an optional
# inflection (`rings`, `tests`, `logging`). Stems meant as prefixes spell their tail (`[a-z]*`).
# Case-sensitive classes are scoped with (?-i:...) because subjects are matched with re.I.
TOK_L = r"(?-i:(?![A-Za-z])|(?<![A-Za-z])|(?<=[a-z])(?=[A-Z])|(?<=[A-Z])(?=[A-Z][a-z]))"
TOK_R = r"(?:s|es|ed|ing|ings|er|ers)?(?-i:(?<![A-Za-z])|(?![a-z]))"


def split_alts(rx):
    """Top-level `|` alternatives of a regex (respects (), [] and escapes)."""
    alts, depth, cls, cur, i = [], 0, False, "", 0
    while i < len(rx):
        ch = rx[i]
        if ch == "\\":
            cur += rx[i:i + 2]
            i += 2
            continue
        if cls:
            cls = ch != "]"
        elif ch == "[":
            cls = True
        elif ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
        elif ch == "|" and depth == 0:
            alts.append(cur)
            cur, i = "", i + 1
            continue
        cur += ch
        i += 1
    return alts + [cur]


def tokenize(rx):
    return "|".join(f"{TOK_L}(?:{a}){TOK_R}" for a in split_alts(rx))


def is_token_span(s, a, b):
    """Guard: s[a:b] is a whole token (edge/non-letter/CamelCase/acronym boundary both sides)."""
    def boundary(i):
        if i <= 0 or i >= len(s) or not (s[i - 1].isalpha() and s[i].isalpha()):
            return True
        p, n = s[i - 1], s[i]
        return (p.islower() and n.isupper()) or (p.isupper() and n.isupper()
                                                 and i + 1 < len(s) and s[i + 1].islower())
    return boundary(a) and boundary(b)


COMPILED = [(f, re.compile(tokenize(s), re.I) if s else None, re.compile(p) if p else None)
            for f, s, p in RULES]
PR_RE = re.compile(r"\(#(\d+)\)")
ID_RE = re.compile(r"\b((?:PRO|CAR|GM)-\d+)\b", re.I)


def git(*args):
    return subprocess.run(["git", *args], check=True, capture_output=True, text=True).stdout


def _numstat(text):
    """{path: lines changed} from `--numstat` output (binary files count as 1)."""
    out = {}
    for line in text.splitlines():
        parts = line.split("\t")
        if len(parts) == 3:
            a, d, p = parts
            out[p] = out.get(p, 0) + (int(a) + int(d) if a != "-" else 1)
    return out


def load_commits(base, tip):
    """All commits incl. merges: sha, date, subject, {path: lines} (merges: first-parent diff)."""
    out = git("log", "--format=%x1e%H%x1f%P%x1f%ad%x1f%s", "--date=short", "--numstat",
              "--no-renames", f"{base}..{tip}")
    commits = []
    for block in out.split("\x1e")[1:]:
        head, _, rest = block.partition("\n")
        sha, parents, date, subject = head.split("\x1f")
        commits.append({"sha": sha, "merge": len(parents.split()) > 1, "date": date,
                        "subject": subject, "lines": _numstat(rest)})
    for c in commits:  # git log prints no diff for merges; use the first-parent diff
        if c["merge"]:
            c["lines"] = _numstat(git("diff", "--numstat", "--no-renames", f"{c['sha']}^1", c["sha"]))
        c["paths"] = sorted(c["lines"])
        c["pf"] = path_features(c["lines"])
    return commits


def path_feature(path):
    """Feature owning a path: a feature id, NEUTRAL, or None (unmapped -> caller decides)."""
    for rx, f in PATH_RULES_C:
        if rx.search(path):
            return f
    return None


def path_features(lines):
    """{feature: lines changed} over all touched paths, excluding NEUTRAL paths."""
    out = collections.Counter()
    for p, n in lines.items():
        f = path_feature(p)
        if f and f != NEUTRAL:
            out[f] += n
    return out


def top_path_feature(pf):
    return max(sorted(pf), key=lambda f: pf[f]) if pf else None


def classify(c):
    """-> (primary, matched_by). matched_by in override|sync-merge|subject|path-wins|mismatch-override|path|fallback."""
    subj, pf, key = c["subject"], c["pf"], c["sha"][:8]
    if key in OVERRIDES:
        return OVERRIDES[key], "override"
    if key in MISMATCH_OVERRIDES:
        return MISMATCH_OVERRIDES[key][0], "mismatch-override"
    if c["merge"] and SYNC_MERGE_RE.search(subj):
        return FALLBACK, "sync-merge"
    for f, s, _ in COMPILED:
        if s and s.search(subj):
            if pf and f not in pf:  # subject disagrees with every touched path
                if f in CROSSCUT:
                    return f, "crosscut-subject"
                return top_path_feature(pf), "path-wins"
            return f, "subject"
    if pf:
        return top_path_feature(pf), "path"
    for f, _, p in COMPILED:
        if p and any(p.search(x) for x in c["paths"]):
            return f, "path"
    return FALLBACK, "fallback"


def subject_feature(c):
    for f, s, _ in COMPILED:
        if s and s.search(c["subject"]):
            return f
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--base", default="v1.9.0")
    ap.add_argument("--tip", default="origin/dev-master")
    ap.add_argument("--out", default="docs/upstream-v1.9-preservation-matrix")
    a = ap.parse_args()
    commits = load_commits(a.base, a.tip)
    expected = int(git("rev-list", "--count", f"{a.base}..{a.tip}"))
    groups = collections.OrderedDict((f, []) for f, _, _ in RULES)
    groups[FALLBACK] = []
    sec_groups = collections.OrderedDict((f, []) for f in groups)
    mism = collections.Counter()  # resolution type -> count (subject feature not in path features)
    unresolved = []
    with open(a.out + ".csv", "w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(["sha", "date", "merge", "feature", "matched_by", "secondary", "path_features",
                    "prs", "linear_ids", "subject"])
        for c in commits:
            f, how = classify(c)
            sync = how == "sync-merge"
            pf = {} if sync else c["pf"]  # sync merges replay already-owned commits: no secondaries
            c["secondary"] = [s for s in sorted(pf, key=lambda x: (-pf[x], x)) if s != f]
            sf = subject_feature(c)
            if not sync and pf and sf and sf not in pf:
                mism[how] += 1
            if not sync and pf and f not in pf and how not in ("override", "mismatch-override",
                                                               "crosscut-subject"):
                unresolved.append(c["sha"][:8])
            prs = " ".join("#" + n for n in PR_RE.findall(c["subject"]))
            ids = " ".join(sorted({i.upper() for i in ID_RE.findall(c["subject"])}))
            pfs = " ".join(f"{k}:{pf[k]}" for k in sorted(pf, key=lambda x: (-pf[x], x)))
            w.writerow([c["sha"][:10], c["date"], int(c["merge"]), f, how, " ".join(c["secondary"]),
                        pfs, prs, ids, c["subject"]])
            groups[f].append((c, prs, ids))
            for s in c["secondary"]:
                sec_groups[s].append((c, f))
    total = sum(len(v) for v in groups.values())
    assert total == len(commits) == expected, (total, len(commits), expected)
    placed = {c["sha"][:8]: f for f, rows in groups.items() for c, _, _ in rows}
    missing = sorted(set(OVERRIDES) - set(placed))
    assert not missing, f"override shas not in range: {missing}"
    bad = {s: (placed.get(s), f) for s, f in SEEDS.items() if placed.get(s) != f}
    assert not bad, f"seed misclassified (got, want): {bad}"
    junk = [c["sha"][:8] for c, _, _ in groups[FALLBACK] if not (c["merge"] and SYNC_MERGE_RE.search(c["subject"]))]
    assert not junk, f"content-bearing commits in {FALLBACK}: {junk}"
    assert not unresolved, f"unresolved subject/path mismatches: {unresolved}"
    stale = sorted(set(MISMATCH_OVERRIDES) - set(placed))
    assert not stale, f"mismatch-override shas not in range: {stale}"
    unmapped = sorted({p for c in commits for p in c["paths"] if path_feature(p) is None})
    assert not unmapped, f"paths with no PATH_RULES owner: {unmapped}"
    by8 = {c["sha"][:8]: c for c in commits}
    badsec = {s: sorted(want - set(by8[s]["secondary"])) for s, want in SECONDARY_SEEDS.items()
              if not want <= set(by8[s]["secondary"])}
    assert not badsec, f"secondary seed missing owners: {badsec}"
    # Token guard: every subject rule hit must be a whole token, never inside a lowercase word.
    subword = []
    for c in commits:
        for f, s, _ in COMPILED:
            mm = s.search(c["subject"]) if s else None
            if mm:
                if not is_token_span(c["subject"], mm.start(), mm.end()):
                    subword.append((c["sha"][:8], f, mm.group(0)))
                break
    assert not subword, f"subject keyword matched inside a word: {subword}"
    # neutral-only-subject: only non-doc paths are NEUTRAL hubs, so the subject alone decided.
    neutral_only = [c for c in commits if not c["pf"] and subject_feature(c)
                    and any(HUB_RE.search(p) for p in c["paths"])]
    nkeys = {c["sha"][:8] for c in neutral_only}
    unverified = sorted(nkeys - NEUTRAL_VERIFIED)
    assert not unverified, f"neutral-only-subject commits not hand-verified: {unverified}"
    stale_nv = sorted(NEUTRAL_VERIFIED - nkeys)
    assert not stale_nv, f"NEUTRAL_VERIFIED entries no longer in bucket: {stale_nv}"
    n_nov = len(nkeys & set(OVERRIDES))
    print(f"neutral-only-subject: {len(neutral_only)} commits, all hand-verified "
          f"({n_nov} overridden, {len(neutral_only) - n_nov} subject confirmed); token guard ok")
    n_sec = sum(1 for c in commits if c["secondary"])
    print(f"asserts ok: {len(SEEDS)} seeds, {len(OVERRIDES)} overrides, F99 sync-only, "
          f"unresolved_mismatches == 0")
    print(f"mismatches {sum(mism.values())}: " + ", ".join(f"{k}={v}" for k, v in sorted(mism.items())))
    print(f"commits with secondary owner: {n_sec}")
    print("secondary per feature: " + ", ".join(f"{f.split('-')[0]}={len(v)}" for f, v in sec_groups.items() if v))
    tip = git("rev-parse", "--short=10", a.tip).strip()
    with open(a.out + ".appendix.md", "w") as fh:
        fh.write(f"# Appendix: commit -> feature map ({a.base}..{a.tip} @ {tip})\n\n")
        fh.write("Generated by `scripts/v19_commit_feature_map.py`; do not hand-edit.\n\n")
        fh.write(f"Coverage: **{total} / {expected}** commits mapped "
                 f"({sum(c['merge'] for c in commits)} merges, "
                 f"{sum(not c['merge'] for c in commits)} non-merge). "
                 f"{n_sec} commits also have secondary owners, listed under each owner as "
                 f"`[secondary; primary Fnn]`.\n\n")
        fh.write("| Feature | Primary | Secondary | Merges | PRs | Linear IDs |\n|---|---|---|---|---|---|\n")
        for f, rows in groups.items():
            prs = {p for _, ps, _ in rows for p in ps.split()}
            ids = {i for _, _, s in rows for i in s.split()}
            fh.write(f"| {f} | {len(rows)} | {len(sec_groups[f])} | {sum(c['merge'] for c, _, _ in rows)} "
                     f"| {len(prs)} | {len(ids)} |\n")
        for f, rows in groups.items():
            fh.write(f"\n## {f} ({len(rows)} primary, {len(sec_groups[f])} secondary)\n\n")
            ids = sorted({i for _, _, s in rows for i in s.split()}, key=lambda x: (x[:3], int(x[4:])))
            if ids:
                fh.write("Linear IDs: " + ", ".join(ids) + "\n\n")
            for c, prs, _ in rows:
                m = " [merge]" if c["merge"] else ""
                fh.write(f"- `{c['sha'][:8]}` {c['date']} {c['subject'].replace('|', '/')}{m}\n")
            for c, pri in sec_groups[f]:
                m = " [merge]" if c["merge"] else ""
                fh.write(f"- `{c['sha'][:8]}` {c['date']} {c['subject'].replace('|', '/')}{m} "
                         f"[secondary; primary {pri.split('-')[0]}]\n")
    print(f"coverage {total}/{expected}")
    for f, rows in groups.items():
        print(f"{len(rows):5d} {f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
