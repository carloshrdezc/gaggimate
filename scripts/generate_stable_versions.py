"""
Generate src/stable_versions.h with the 5 most recent published, non-prerelease
GitHub releases for the configured fork. Runs as a PlatformIO pre-script so the
firmware-side STABLE_VERSIONS allow-list is always in sync with what's actually
flashable from `releases/tag/<tag>` on GitHub.

Why fetch published releases (not just `git tag`)?
  - The OTA download path is `releases/download/<tag>/...`, which 404s for
    annotated tags that were never promoted to a GitHub Release. Filtering on
    the published-releases feed guarantees every tag in the dropdown is a real
    OTA target.
  - Auto-prerelease + `nightly` + `beta` are excluded so the dropdown only ever
    offers stable, vetted tags.

Network resiliency:
  - If the API call succeeds, we (re)write the header.
  - If it fails (offline, rate-limit, 5xx) AND the header already exists from a
    previous successful run, we keep the existing file — build still succeeds.
  - If the API call fails AND no header exists yet, we emit a header with a
    single sentinel entry (the build's own git-describe version) so the
    firmware compiles and the dropdown isn't empty.

The owner/repo slug is hardcoded to match RELEASE_URL in WebUIPlugin.h
(`carloshrdezc/gaggimate`). Update both if the fork ever moves.
"""

import json
import os
import subprocess
import sys
import urllib.error
import urllib.request

try:
    Import("env")  # type: ignore[name-defined]  # PlatformIO injects this
    _PLATFORMIO_BUILD = True
except NameError:
    # Allow running standalone for testing: `python scripts/generate_stable_versions.py`
    _PLATFORMIO_BUILD = False

# --- Config -----------------------------------------------------------------

OWNER = "carloshrdezc"
REPO = "gaggimate"
HEADER_PATH = os.path.join("src", "stable_versions.h")
MAX_VERSIONS = 5
# Pull more than MAX_VERSIONS to survive a few prereleases / `nightly` entries
# at the top of the feed without dropping below MAX_VERSIONS stable tags.
API_PAGE_SIZE = 20
API_TIMEOUT_SEC = 10
USER_AGENT = "gaggimate-build/{}".format(OWNER)

# --- Helpers ----------------------------------------------------------------

def _git_describe() -> str:
    try:
        out = subprocess.run(
            ["git", "describe", "--tags", "--exclude", "nightly", "--exclude", "beta", "--abbrev=0"],
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
            check=False,
        )
        return out.stdout.strip() or "unknown"
    except Exception:
        return "unknown"


def _current_release_tag() -> str | None:
    """Return the tag at HEAD if HEAD is exactly on a tag, else None.

    During the GitHub Actions release flow, `.github/workflows/build.yml`
    builds the firmware *before* the release-publishing job creates the
    GitHub Release. So at firmware build time, the just-pushed tag is
    visible to git but not yet to the Releases API. We promote it here
    so the artifact for tag X ships with X already in STABLE_VERSIONS,
    letting users on the just-released firmware force-reflash that
    exact build via the dropdown without waiting for the next release.

    `nightly` and `beta` are filtered out — they are moving tags, not release lines.
    """
    try:
        out = subprocess.run(
            ["git", "describe", "--tags", "--exact-match", "HEAD"],
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
            check=False,
        )
        if out.returncode != 0:
            return None
        tag = out.stdout.strip()
        if not tag or tag.lower() in ("nightly", "beta"):
            return None
        return tag
    except Exception:
        return None


def _fetch_releases() -> list[dict] | None:
    """Return list of release dicts, or None on any failure."""
    url = "https://api.github.com/repos/{}/{}/releases?per_page={}".format(
        OWNER, REPO, API_PAGE_SIZE
    )
    headers = {"User-Agent": USER_AGENT, "Accept": "application/vnd.github+json"}
    # Honor a token if the build environment has one (raises rate limits CI-side).
    token = os.environ.get("GITHUB_TOKEN") or os.environ.get("GH_TOKEN")
    if token:
        headers["Authorization"] = "Bearer " + token

    req = urllib.request.Request(url, headers=headers)
    try:
        with urllib.request.urlopen(req, timeout=API_TIMEOUT_SEC) as r:
            return json.loads(r.read().decode("utf-8"))
    except (urllib.error.URLError, urllib.error.HTTPError, TimeoutError, OSError, json.JSONDecodeError) as e:
        print("[stable_versions] WARN: GitHub API fetch failed: {}".format(e))
        return None


def _filter_stable(releases: list[dict]) -> list[str]:
    out: list[str] = []
    for rel in releases:
        if rel.get("prerelease") or rel.get("draft"):
            continue
        tag = (rel.get("tag_name") or "").strip()
        if not tag or tag.lower() in ("nightly", "beta"):
            continue
        out.append(tag)
        if len(out) >= MAX_VERSIONS:
            break
    return out


def _format_header(versions: list[str], source: str) -> str:
    """Render the C++ header. `source` is a short note (e.g. "github-api" / "fallback")."""
    lines = [
        "// Auto-generated by scripts/generate_stable_versions.py — DO NOT EDIT.",
        "// Source: {}".format(source),
        "// Regenerated on every PlatformIO build of `display` / `controller`.",
        "#pragma once",
        "",
        "#include <cstddef>  // size_t",
        "",
        "// Last {} stable releases offered in the OTA dropdown.".format(MAX_VERSIONS),
        "constexpr const char *const STABLE_VERSIONS[] = {",
    ]
    for v in versions:
        # C++ string-literal escape for the few characters that could appear in a
        # tag name. Tag names from GitHub are safe ASCII in practice but quote
        # to be defensive.
        escaped = v.replace("\\", "\\\\").replace("\"", "\\\"")
        lines.append("    \"{}\",".format(escaped))
    lines.append("};")
    lines.append(
        "constexpr size_t STABLE_VERSIONS_COUNT = "
        "sizeof(STABLE_VERSIONS) / sizeof(STABLE_VERSIONS[0]);"
    )
    lines.append("")
    return "\n".join(lines)


# --- Main -------------------------------------------------------------------

def main() -> int:
    releases = _fetch_releases()
    current_tag = _current_release_tag()
    if releases is not None:
        versions = _filter_stable(releases)
        # If this build is on a tag, prepend it so the artifact for tag X
        # ships with X already listed (the Releases API hasn't seen it yet
        # — release publish runs *after* build in our workflow).
        if current_tag and current_tag not in versions:
            versions.insert(0, current_tag)
            versions = versions[:MAX_VERSIONS]
        if versions:
            source = "github-api ({}/{})".format(OWNER, REPO)
            if current_tag:
                source += " + current-tag({})".format(current_tag)
            content = _format_header(versions, source)
            with open(HEADER_PATH, "w", encoding="utf-8", newline="\n") as f:
                f.write(content)
            print("[stable_versions] Wrote {} with {} versions: {}".format(
                HEADER_PATH, len(versions), ", ".join(versions)
            ))
            return 0
        print("[stable_versions] WARN: API returned no stable releases.")

    # Fetch failed or returned nothing. Keep existing header if present.
    if os.path.exists(HEADER_PATH):
        print("[stable_versions] Keeping existing {} (offline / no fresh data).".format(HEADER_PATH))
        return 0

    # Last-resort fallback: emit a single-entry array so firmware still compiles.
    # Prefer the exact-match tag (if HEAD is on one) over the descended-from tag.
    fallback_version = current_tag or _git_describe()
    content = _format_header([fallback_version], "fallback (git-describe, build was offline)")
    with open(HEADER_PATH, "w", encoding="utf-8", newline="\n") as f:
        f.write(content)
    print("[stable_versions] WARN: emitted fallback {} with {!r}".format(HEADER_PATH, fallback_version))
    return 0


if __name__ == "__main__":
    sys.exit(main())
elif _PLATFORMIO_BUILD:
    # Imported by PlatformIO via `pre:scripts/generate_stable_versions.py`.
    # Gated on _PLATFORMIO_BUILD (i.e. SCons injected `env`) rather than a bare
    # `else`, so a plain `import generate_stable_versions` — which is what
    # scripts/test_generate_stable_versions.py does — neither hits the GitHub API
    # nor rewrites src/stable_versions.h as an import side effect. (PRO-648)
    main()
