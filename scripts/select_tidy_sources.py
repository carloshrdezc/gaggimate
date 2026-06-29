#!/usr/bin/env python3
"""CAR-341: select in-scope firmware sources from a PlatformIO compile DB.

Reads a clang-style compile_commands.json and prints, one per line, the
paths of the production firmware sources we want clang-tidy to analyse:
anything under src/ that the native build compiled, EXCLUDING the generated
LVGL UI (src/display/ui/**), vendored drivers (src/display/drivers/**), and the
host test shims (test/native/**, not under src/ so already excluded).

Compile DB `file` entries may be absolute or relative and may use either path
separator depending on platform, so we normalise before matching.

Usage: select_tidy_sources.py path/to/compile_commands.json
"""
import json
import os
import sys


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: select_tidy_sources.py compile_commands.json", file=sys.stderr)
        return 2
    with open(sys.argv[1], encoding="utf-8") as fh:
        db = json.load(fh)
    out = set()
    for entry in db:
        raw = entry.get("file", "")
        if not raw:
            continue
        # Normalise separators and make relative paths resolvable against the
        # compile DB's "directory" (clang's documented base for relative files).
        norm = raw.replace("\\", "/")
        # Match the src/ tree whether the path is absolute (".../src/...") or
        # relative to the project root ("src/...").
        if not (norm.startswith("src/") or "/src/" in norm):
            continue
        if "/src/display/ui/" in norm or norm.startswith("src/display/ui/"):
            continue
        if "/src/display/drivers/" in norm or norm.startswith("src/display/drivers/"):
            continue
        base = entry.get("directory", "")
        path = raw if os.path.isabs(raw) else os.path.join(base, raw) if base else raw
        out.add(os.path.normpath(path))
    for f in sorted(out):
        print(f)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
