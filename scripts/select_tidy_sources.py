#!/usr/bin/env python3
"""Select in-scope firmware sources from a PlatformIO compile DB (CAR-341, PRO-608).

Reads a clang-style compile_commands.json and prints, one per line, the paths of
the translation units we want clang-tidy to analyse.

IN SCOPE (see ``INCLUDE_PREFIXES``)
    * ``src/`` — hand-written firmware sources, EXCEPT the generated LVGL UI
      (``src/display/ui/**``), the vendored panel drivers
      (``src/display/drivers/**``) and the generated embedded-web-UI blob
      (``src/display/webassets/**``).
    * ``test/`` — the host unit-test suites and their shims. These are the ONLY
      translation units that ``#include`` the ~31 header-only ``*Policy.h`` seam
      headers under ``src/display/{core,plugins}``, so without them
      ``.clang-tidy``'s ``HeaderFilterRegex: 'src/display/(core|plugins|models)/'``
      never gets a TU to fire on and those seams go unanalysed (PRO-608).
    * ``lib/OTA/src/`` — the OTA semver sources. Vendored-ish, but they were
      already being linted before PRO-608 (matched by the old ``"/src/" in
      path`` heuristic), and dropping them would be a silent *reduction* in
      coverage, which is exactly what this script now guards against. Kept
      deliberately, and now explicitly rather than by accident.

OUT OF SCOPE — not our code, never analysed
    * Anything under ``.pio/`` — PlatformIO's build dir and ``libdeps``. This
      matters concretely: a vendored library can itself have a ``src/``
      directory (``.pio/libdeps/<env>/lvgl/src/...``), which the old
      ``"/src/" in path`` test happily matched. That was latent before PRO-608
      because ``[env:native]``'s compile DB contained no such library; the
      ``native-tidy`` env pulls in lvgl, which would otherwise add ~190
      vendored LVGL translation units to the lint set.
    * Every other ``lib/`` subtree (NimBLEComm, GaggiMateController, ...).

Compile DB ``file`` entries may be absolute or relative and may use either path
separator depending on platform, so we normalise before matching.

Usage: select_tidy_sources.py path/to/compile_commands.json
"""
import json
import os
import sys

# Project-relative directory prefixes whose contents are ours to analyse.
INCLUDE_PREFIXES = (
    "src/",
    "test/",
    "lib/OTA/src/",
)

# Sub-trees under an included prefix that are generated or vendored.
EXCLUDE_SUBTREES = (
    "src/display/ui/",         # generated LVGL UI
    "src/display/drivers/",    # vendored panel drivers
    "src/display/webassets/",  # generated embedded web-UI blob (embed_webui_pre.py)
)

# Path segments that mark a file as not-our-code regardless of what it looks
# like. `.pio/` covers both the build dir and libdeps (vendored libraries, which
# may themselves contain a `src/` directory).
EXCLUDE_ANYWHERE = (".pio/",)


def project_relative(norm: str, base: str) -> str:
    """Return `norm` as a project-relative path, or '' if it isn't under one.

    Anchoring matters. A naive "does the path contain one of the include
    prefixes" test is WRONG: `lib/NimBLEComm/src/NimBLEComm.cpp` contains
    `/src/` and would be treated as firmware source. So we relativise against
    the compile DB's `directory` (clang's documented base for `file` entries,
    and the project root in every PlatformIO-generated DB) and require the
    result to *start* with an include prefix.

    The marker-search fallback only runs when there is no `directory` to anchor
    against, which a PlatformIO DB never produces; it exists so a hand-written
    or trimmed DB still behaves sensibly.
    """
    if base:
        anchor = base.replace("\\", "/").rstrip("/")
        if norm.startswith(anchor + "/"):
            return norm[len(anchor) + 1 :]
        if not os.path.isabs(norm):
            return norm  # already relative to `directory`
        return ""  # absolute, but outside the project — not ours
    if not os.path.isabs(norm):
        return norm
    for prefix in INCLUDE_PREFIXES:
        idx = norm.find("/" + prefix)
        if idx != -1:
            return norm[idx + 1 :]
    return ""


def is_in_scope(raw: str, base: str = "") -> bool:
    norm = raw.replace("\\", "/")
    if any(seg in norm for seg in EXCLUDE_ANYWHERE):
        return False
    rel = project_relative(norm, base)
    if not rel or not rel.startswith(INCLUDE_PREFIXES):
        return False
    return not rel.startswith(EXCLUDE_SUBTREES)


def select(db) -> list:
    """Return the sorted, de-duplicated list of in-scope paths from a parsed DB."""
    out = set()
    for entry in db:
        raw = entry.get("file", "")
        base = entry.get("directory", "")
        if not raw or not is_in_scope(raw, base):
            continue
        # Make relative paths resolvable against the compile DB's "directory"
        # (clang's documented base for relative `file` entries).
        path = raw if os.path.isabs(raw) else os.path.join(base, raw) if base else raw
        out.add(os.path.normpath(path))
    return sorted(out)


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: select_tidy_sources.py compile_commands.json", file=sys.stderr)
        return 2
    with open(sys.argv[1], encoding="utf-8") as fh:
        db = json.load(fh)
    for f in select(db):
        print(f)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
