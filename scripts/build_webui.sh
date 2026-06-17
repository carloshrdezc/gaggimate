#!/usr/bin/env bash
set -euo pipefail

# Build the web UI and embed it into the display firmware (GM-106).
#
# The bundle no longer ships in the LittleFS image (/w). It is gzipped and packed
# into a single blob that scripts/embed_webui.py turns into firmware-embedded,
# memory-mapped flash. LittleFS now holds only profiles (/p) and shot history
# (/h), so OTA never touches user data. data/p (seed profiles) is still staged
# for the fresh-install filesystem image.

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# Resolve a working Python interpreter. We can't just check PATH: on Windows the
# `python3` name is often a Microsoft Store stub that is on PATH but errors when
# run ("no se encontro Python"), while `python` is the real one. So actually
# invoke each candidate and pick the first that runs. Matches embed_webui_pre.py
# (which uses sys.executable, already a working interpreter).
PYTHON=""
for candidate in python3 python; do
    if command -v "$candidate" >/dev/null 2>&1 && "$candidate" -c "import sys" >/dev/null 2>&1; then
        PYTHON="$candidate"
        break
    fi
done
if [ -z "$PYTHON" ]; then
    echo "build_webui.sh: no working python3/python interpreter found on PATH" >&2
    exit 1
fi

# Seed profiles still go into the filesystem image used for fresh USB installs.
mkdir -p "$ROOT/data/p"

# Build the web application.
cd "$ROOT/web"
npm ci
npm run build

# Gzip the compressible assets in place (served with Content-Encoding: gzip).
# nullglob so a pattern matching zero files expands to nothing (not the literal
# glob) — otherwise `gzip <literal-pattern>` fails and `set -e` aborts the build
# if a future bundle happens to emit no top-level .html / no CSS / no JS.
shopt -s nullglob
gzip_files=(dist/assets/*.js dist/assets/*.css dist/*.html)
if [ ${#gzip_files[@]} -gt 0 ]; then
    gzip -f "${gzip_files[@]}"
fi
shopt -u nullglob

# Pack the built bundle into firmware-embeddable flash artifacts.
"$PYTHON" "$ROOT/scripts/embed_webui.py" --src "$ROOT/web/dist" --out "$ROOT/src/display/webassets"
