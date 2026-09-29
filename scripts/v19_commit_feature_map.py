#!/usr/bin/env python3
"""Map every Carlos-only commit (v1.9.0..origin/dev-master) to a feature (PRO-652).

Deterministic clustering: audited OVERRIDES (sha -> feature) first, then content-free
sync merges -> F99, then RULES on subject (PR number, PRO/CAR IDs, scope, keywords) and
touched paths; first match wins. Asserts: enumeration == rev-list count, every SEEDS entry
lands in its expected feature, and F99 holds only sync merges. Enumeration is NOT proof of
classification accuracy; see the matrix doc's QA re-audit.

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
    ("F05-ble-scale", r"scale|blescale|pro-459|pro-647", r"BLEScale"),
    ("F04-ble-comms", r"nimblecomm|\bble\b.*(controller|bond|encrypt|write_enc|pair|reconnect|mtu)|"
     r"write_enc|bond|controller (link|connection)|\(ble\)", r"^(lib/NimBLEComm|lib/GaggiMateController/src/.*Comm|docs/ble-pairing)"),
    ("F06-ota", r"\bota\b|firmware update|update check|stable.versions|installedchannel|semver|"
     r"pro-(39[4-9]|400|55[4-9]|56[0-9]|599|648|649)\b", r"^(lib/OTA|lib/ble_ota_dfu|scripts/.*stable_versions)"),
    ("F07-webui-security", r"local.?auth|cors|relay token|path.traversal|secret|mask|security|"
     r"\bauth\b|pro-3[0-9]{2}.*auth", r"(LocalAuthPolicy|PathTraversalPolicy)"),
    ("F08-relay", r"relay|wrangler|cloudflare", r"^(relay-server/|src/display/plugins/Relay|web/src/.*[Rr]elay)"),
    ("F09-webui-plugin-core", r"websocket|\bws\b|webuiplugin|reassembl|broadcast|lifecycle.?defer|\(webui\)|\(api\)|\(ws\)",
     r"(WebUIPlugin|WsReassembly|WsBroadcast|WebUiLifecycle|docs/websocket-api|check_ws_api_spec)"),
    ("F10-memory-psram", r"psram|heap|dram|mbedtls|memory|oom|heapdiag|pro-566|leak",
     r"(GmHeapDiag|MbedtlsPsram|PsramAllocator|pro-566)"),
    ("F11-diag-log", r"diag|log tee|udp log|esp_log|logging|\blog\b", r"(DiagnosticLog|EspLogTee|DiagLog)"),
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
    ("F17-standby-steam-flush", r"standby|auto.?steam|steam|flush|autowakeup|auto.wake|water|preinfus",
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
    ("F25-ci-quality", r"\bci\b|\(ci\)|workflow|clang-tidy|tidy|cppcheck|sanitiz|lint|prettier|eslint|"
     r"format|vitest|test|coverage|flash script|gitignore|worktree|promotion|nightly|beta|release|car-341|pro-6(0[89]|1[01]|44)\b",
     r"^(\.github/|scripts/|test/|\.clang)"),
    ("F26-docs-agents", r"^docs|readme|agents|claude\.md|contributing|plan|spec|spike|mailmap",
     r"^(docs/|AGENTS\.md|CLAUDE\.md|README|CONTRIBUTING|\.mailmap)"),
    ("F27-web-misc", None, r"^web/"),
    ("F28-firmware-misc", None, r"^(src/|lib/|platformio\.ini)"),
]
FALLBACK = "F99-misc"
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
}
# Seeds that MUST land where stated, or the script fails (guards against rule regressions).
SEEDS = dict(OVERRIDES, **{
    "72da9327": "F03-nanopb-spike", "33751012": "F03-nanopb-spike", "5a91167c": "F03-nanopb-spike",
    "394e7e4d": "F12b-fs-migration-preserve", "3ebd1c34": "F12b-fs-migration-preserve",
    "b79fa358": "F12b-fs-migration-preserve", "be68e230": "F12b-fs-migration-preserve",
    "ea232239": "F02-platform-stack", "96e7fdf6": "F10-memory-psram", "f2a8764c": "F10-memory-psram",
})
# F99 may hold only content-free sync merges: merge commits whose subject is a plain branch sync.
SYNC_MERGE_RE = re.compile(r"^Merge (remote-tracking )?branch '(origin/)?(master|dev-master)'|"
                           r"^Merge pull request #\d+ from \S+/\S*merge-master", re.I)
COMPILED = [(f, re.compile(s, re.I) if s else None, re.compile(p) if p else None) for f, s, p in RULES]
PR_RE = re.compile(r"\(#(\d+)\)")
ID_RE = re.compile(r"\b((?:PRO|CAR|GM)-\d+)\b", re.I)


def git(*args):
    return subprocess.run(["git", *args], check=True, capture_output=True, text=True).stdout


def load_commits(base, tip):
    """All commits incl. merges: sha, parents, date, subject, touched paths (vs first parent)."""
    out = git("log", "--format=%x1e%H%x1f%P%x1f%ad%x1f%s", "--date=short", "--name-only",
              "--first-parent" if False else "--no-renames", f"{base}..{tip}")
    commits = []
    for block in out.split("\x1e")[1:]:
        head, _, rest = block.partition("\n")
        sha, parents, date, subject = head.split("\x1f")
        paths = [p for p in rest.splitlines() if p.strip()]
        commits.append({"sha": sha, "merge": len(parents.split()) > 1, "date": date,
                        "subject": subject, "paths": paths})
    for c in commits:  # git log --name-only prints nothing for merges; diff vs first parent
        if c["merge"]:
            c["paths"] = git("diff", "--name-only", f"{c['sha']}^1", c["sha"]).split()
    return commits


def classify(c):
    subj = c["subject"]
    if c["sha"][:8] in OVERRIDES:
        return OVERRIDES[c["sha"][:8]], "override"
    if c["merge"] and SYNC_MERGE_RE.search(subj):
        return FALLBACK, "sync-merge"
    for f, s, _ in COMPILED:
        if s and s.search(subj):
            return f, "subject"
    for f, _, p in COMPILED:
        if p and any(p.search(x) for x in c["paths"]):
            return f, "path"
    return FALLBACK, "fallback"


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
    with open(a.out + ".csv", "w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(["sha", "date", "merge", "feature", "matched_by", "prs", "linear_ids", "subject"])
        for c in commits:
            f, how = classify(c)
            prs = " ".join("#" + n for n in PR_RE.findall(c["subject"]))
            ids = " ".join(sorted({i.upper() for i in ID_RE.findall(c["subject"])}))
            w.writerow([c["sha"][:10], c["date"], int(c["merge"]), f, how, prs, ids, c["subject"]])
            groups[f].append((c, prs, ids))
    total = sum(len(v) for v in groups.values())
    assert total == len(commits) == expected, (total, len(commits), expected)
    placed = {c["sha"][:8]: f for f, rows in groups.items() for c, _, _ in rows}
    missing = sorted(set(OVERRIDES) - set(placed))
    assert not missing, f"override shas not in range: {missing}"
    bad = {s: (placed.get(s), f) for s, f in SEEDS.items() if placed.get(s) != f}
    assert not bad, f"seed misclassified (got, want): {bad}"
    junk = [c["sha"][:8] for c, _, _ in groups[FALLBACK] if not (c["merge"] and SYNC_MERGE_RE.search(c["subject"]))]
    assert not junk, f"content-bearing commits in {FALLBACK}: {junk}"
    print(f"asserts ok: {len(SEEDS)} seeds, {len(OVERRIDES)} overrides, F99 sync-only")
    tip = git("rev-parse", "--short=10", a.tip).strip()
    with open(a.out + ".appendix.md", "w") as fh:
        fh.write(f"# Appendix: commit -> feature map ({a.base}..{a.tip} @ {tip})\n\n")
        fh.write("Generated by `scripts/v19_commit_feature_map.py`; do not hand-edit.\n\n")
        fh.write(f"Coverage: **{total} / {expected}** commits mapped "
                 f"({sum(c['merge'] for c in commits)} merges, "
                 f"{sum(not c['merge'] for c in commits)} non-merge).\n\n")
        fh.write("| Feature | Commits | Merges | PRs | Linear IDs |\n|---|---|---|---|---|\n")
        for f, rows in groups.items():
            prs = {p for _, ps, _ in rows for p in ps.split()}
            ids = {i for _, _, s in rows for i in s.split()}
            fh.write(f"| {f} | {len(rows)} | {sum(c['merge'] for c, _, _ in rows)} | {len(prs)} | {len(ids)} |\n")
        for f, rows in groups.items():
            fh.write(f"\n## {f} ({len(rows)})\n\n")
            ids = sorted({i for _, _, s in rows for i in s.split()}, key=lambda x: (x[:3], int(x[4:])))
            if ids:
                fh.write("Linear IDs: " + ", ".join(ids) + "\n\n")
            for c, prs, _ in rows:
                m = " [merge]" if c["merge"] else ""
                fh.write(f"- `{c['sha'][:8]}` {c['date']} {c['subject'].replace('|', '/')}{m}\n")
    print(f"coverage {total}/{expected}")
    for f, rows in groups.items():
        print(f"{len(rows):5d} {f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
