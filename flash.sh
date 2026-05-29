#!/usr/bin/env bash
# Build and flash the display firmware and SPIFFS to ESP32
#
# Usage: ./flash.sh <upload_port>
# Example: ./flash.sh COM4
#
# Note: <upload_port> is required. Find it in Device Manager (Windows)
#       or via: ls /dev/tty.* (macOS) or ls /dev/ttyUSB* (Linux)

set -euo pipefail

# PlatformIO ships its own embedded Python in ~/.platformio/penv. If the host
# shell exports PYTHONHOME/UV_INTERNAL__PYTHONHOME (e.g. from a uv-managed
# project venv), pio's Python will pick those up and try to load the wrong
# stdlib, breaking the build with cryptic ModuleNotFoundError. Strip them.
unset PYTHONHOME || true
unset UV_INTERNAL__PYTHONHOME || true

if [ -z "${1:-}" ]; then
  echo "Error: upload_port required"
  echo "Usage: ./flash.sh <upload_port>"
  echo "Example: ./flash.sh COM4"
  exit 1
fi

PORT=$1
ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"

echo "=== Step 1: Build web UI ==="
cd "$ROOT_DIR/web"
npm run build
cd "$ROOT_DIR"

echo "=== Step 2: Sync web dist to data/w (SPIFFS source) ==="
rm -rf data/w
mkdir -p data/w
cp -R web/dist/* data/w/
gzip -f data/w/assets/*.js
gzip -f data/w/assets/*.css
gzip -f data/w/*.html

echo "=== Step 2b: Verify SPIFFS path lengths ==="
# Catch the mkspiffs 32-char filename limit BEFORE we try to build the image.
# mkspiffs silently drops oversized files (and sometimes their dir siblings),
# producing a broken image that the device happily flashes. See CAR-281.
#
# Invoke via `bash` so the guard runs even on hosts where the executable bit
# was lost in transit (Windows checkouts with core.filemode=false, fresh
# clones via cmd.exe, etc.).
bash "$ROOT_DIR/scripts/check_spiffs_name_lengths.sh"

echo "=== Step 3: Build firmware ==="
# Resolve pio: prefer PATH, fall back to the default PlatformIO install location on Windows
PIO=$(command -v pio 2>/dev/null || echo "$HOME/.platformio/penv/Scripts/pio.exe")
"$PIO" run -e display

echo "=== Step 4: Upload firmware ==="
"$PIO" run -e display -t upload --upload-port "$PORT"

echo "=== Step 5: Build SPIFFS ==="
# Capture output so we can detect mkspiffs errors that don't surface as
# non-zero exit codes (it happily prints "SPIFFS_write error(-10010)" then
# continues and reports SUCCESS).
SPIFFS_LOG="$ROOT_DIR/.tmp_buildfs.log"
set +e
"$PIO" run -e display -t buildfs 2>&1 | tee "$SPIFFS_LOG"
PIO_RC=${PIPESTATUS[0]}
set -e

if [ "$PIO_RC" -ne 0 ]; then
  echo "ERROR: pio buildfs returned $PIO_RC" >&2
  exit "$PIO_RC"
fi

if grep -E -q 'SPIFFS_write error|Error for adding content|error adding file' "$SPIFFS_LOG"; then
  echo "" >&2
  echo "ERROR: mkspiffs reported errors during SPIFFS image build:" >&2
  grep -E 'SPIFFS_write error|Error for adding content|error adding file' "$SPIFFS_LOG" >&2
  echo "" >&2
  echo "       Refusing to flash a broken filesystem image. See CAR-281." >&2
  exit 1
fi

echo "=== Step 6: Upload SPIFFS ==="
"$PIO" run -e display -t uploadfs --upload-port "$PORT"

echo "=== Done! ==="
