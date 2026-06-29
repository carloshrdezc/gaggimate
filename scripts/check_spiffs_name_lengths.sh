#!/usr/bin/env bash
# scripts/check_spiffs_name_lengths.sh
#
# mkspiffs (PIO's SPIFFS image builder) hardcodes SPIFFS_OBJ_NAME_LEN = 32,
# which counts the leading slash and trailing null. Any file whose on-device
# path exceeds 31 visible chars is silently dropped — and mkspiffs gives up on
# the rest of its parent directory too. That broke the device web UI in CAR-281
# after the route code-split landed.
#
# This script walks data/w/ (the SPIFFS source tree) and fails if any file's
# on-device path (i.e. path under data/, with leading slash) would exceed 31
# chars. Run it after gzipping but before `pio run -t buildfs`.
#
# Exits 0 if all good, 1 if any path is too long.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
DATA_DIR="$ROOT_DIR/data"
LIMIT=31  # SPIFFS_OBJ_NAME_LEN - 1 (null terminator)

if [ ! -d "$DATA_DIR" ]; then
  echo "ERROR: $DATA_DIR not found — run after staging SPIFFS contents." >&2
  exit 1
fi

bad=0
while IFS= read -r f; do
  # On-device path: relative to data/, with leading slash.
  rel="${f#$DATA_DIR}"
  case "$rel" in
    /*) ;;
    *) rel="/$rel" ;;
  esac
  len=${#rel}
  if [ "$len" -gt "$LIMIT" ]; then
    printf '  too long (%d > %d): %s\n' "$len" "$LIMIT" "$rel" >&2
    bad=$((bad + 1))
  fi
done < <(find "$DATA_DIR" -type f)

if [ "$bad" -gt 0 ]; then
  echo "" >&2
  echo "ERROR: $bad SPIFFS path(s) exceed the $LIMIT-char limit." >&2
  echo "       mkspiffs would silently drop these (and likely their siblings)." >&2
  echo "       Shorten the filenames or adjust web/vite.config.js output names." >&2
  exit 1
fi

echo "SPIFFS path-length check: OK ($(find "$DATA_DIR" -type f | wc -l) files, all ≤ $LIMIT chars)"
