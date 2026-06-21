#!/usr/bin/env bash
set -euo pipefail

# Resolve the repo root from this script's location so the script works
# regardless of the caller's CWD (scripts/ lives one level below the root).
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$ROOT_DIR"

cd pcb
pcbdraw plot --side front --components -l KiCAD-base,custom -m remap.json -s set-black-hasl Gaggimate.kicad_pcb ../docs/assets/pcb_top.png
# sips -r 180 ../docs/assets/pcb_bottom.png
cd "$ROOT_DIR"
rm -f docs/assets/pinout.png
python3 -m pinout.manager --export scripts/pinout_diagram.py docs/assets/pinout.png
