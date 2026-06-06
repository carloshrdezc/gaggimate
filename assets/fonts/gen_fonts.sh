#!/usr/bin/env bash
# Generate the GaggiMate "Nothing" theme LVGL fonts (v8) with lv_font_conv.
# Commands mirror "GaggiMate LVGL — Build Spec" §02; --bpp 4 keeps dot-matrix
# edges clean, ranges are subset to only the glyphs each font uses so the big
# numerals stay a few KB instead of ~120 KB.
#
# Sources:
#   Ndot      -> web/public/fonts/Ndot57Caps-Regular.otf  (same as ndot_18/24/34)
#   Space Mono Bold / Space Grotesk Medium -> assets/fonts/*.ttf (OFL, vendored)
#
# Usage:  bash assets/fonts/gen_fonts.sh
# Requires: node/npx (pulls lv_font_conv@1.5.3).
set -euo pipefail
cd "$(dirname "$0")/../.."   # repo root

NDOT="web/public/fonts/Ndot57Caps-Regular.otf"
SMONO="assets/fonts/SpaceMono-Bold.ttf"
SGRO="assets/fonts/SpaceGrotesk-Medium.ttf"
OUT="src/display/ui/default/lvgl/fonts"
# --no-compress: this repo builds with LV_USE_FONT_COMPRESSED 0, so fonts must be
# plain bitmaps (bitmap_format 0); compressed glyphs would render blank.
CONV=(npx --yes lv_font_conv@1.5.3 --bpp 4 --no-compress --format lvgl --lv-include lvgl.h --force-fast-kern-format)

# Ndot numerals (digits, colon, period, degree, g/s/C units)
"${CONV[@]}" --font "$NDOT" --size 150 --range 0x2E,0x30-0x3A,0x67,0x73,0xB0 -o "$OUT/ndot_150.c"
"${CONV[@]}" --font "$NDOT" --size 120 --range 0x30-0x3A                      -o "$OUT/ndot_120.c"
"${CONV[@]}" --font "$NDOT" --size 60  --range 0xB0,0x67,0x73,0x43            -o "$OUT/ndot_60.c"
# ndot_34: live dial temp readout ("%d°C") — needs digits, colon, period, 'C',
# and the degree (0xB0). Previously subset to ASCII-only (0x20-0x7E) which made
# the degree render as a placeholder box on the live temp; include 0xB0 + 0x43.
"${CONV[@]}" --font "$NDOT" --size 34  --range 0x2E,0x30-0x3A,0x43,0xB0       -o "$OUT/ndot_34.c"
"${CONV[@]}" --font "$NDOT" --size 28  --range 0x2E,0x30-0x3A                 -o "$OUT/ndot_28.c"

# Space Mono Bold — kickers / metric labels (space, punctuation, A-Z, middot)
"${CONV[@]}" --font "$SMONO" --size 14 --range 0x20,0x2E-0x3A,0x41-0x5A,0xB7  -o "$OUT/spacemono_14.c"
"${CONV[@]}" --font "$SMONO" --size 11 --range 0x20,0x41-0x5A                 -o "$OUT/spacemono_11.c"

# Space Grotesk Medium — body / settings rows (full printable ASCII + degree)
"${CONV[@]}" --font "$SGRO"  --size 16 --range 0x20-0x7F,0xB0                 -o "$OUT/grotesk_16.c"
# grotesk_28: +/- stepper glyphs only (subset 0x2B plus 0x2D) — big & crisp in
# the 40px BrewScreen steppers without the flash cost of the full ASCII range.
"${CONV[@]}" --font "$SGRO"  --size 28 --range 0x2B,0x2D                      -o "$OUT/grotesk_28.c"

echo "Generated 7 fonts in $OUT"
