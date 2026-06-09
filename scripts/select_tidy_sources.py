#!/usr/bin/env python3
"""CAR-341: select in-scope firmware sources from a PlatformIO compile DB.

Reads a clang-style compile_commands.json and prints, one per line, the
absolute paths of the production firmware sources we want clang-tidy to
analyse: anything under src/ that the native build compiled, EXCLUDING the
generated LVGL UI (src/display/ui/**) and vendored drivers
(src/display/drivers/**) and the host test shims (test/native/**, not under
src/ so already excluded).

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
        f = entry.get("file", "")
        p = f.replace("\\", "/")
        if "/src/" not in p:
            continue
        if "/src/display/ui/" in p or "/src/display/drivers/" in p:
            continue
        out.add(os.path.abspath(f))
    for f in sorted(out):
        print(f)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
