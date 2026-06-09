#!/usr/bin/env bash
set -euo pipefail

# Clean data
rm -rf data/w
mkdir -p data/w
mkdir -p data/p

# Build web application
cd web || exit
npm ci
npm run build

cp -R dist/* ../data/w/
# Compress assets for SPIFFS storage savings, but keep originals for browsers that don't support gzip
gzip -k ../data/w/assets/*.js
gzip -k ../data/w/assets/*.css
gzip -k ../data/w/*.html

# Verify on-device SPIFFS paths fit mkspiffs's 32-char SPIFFS_OBJ_NAME_LEN
# limit BEFORE PIO builds the image. mkspiffs silently drops oversized files
# (and bails on the rest of their dir), producing a broken filesystem that
# PIO reports as SUCCESS. Both `flash.sh` and the GitHub Actions workflows
# (build.yml, build-nightly.yml, pr-flash.yml) call this script, so wiring
# the guard here covers every SPIFFS build path. See CAR-281.
cd ..
bash scripts/check_spiffs_name_lengths.sh
