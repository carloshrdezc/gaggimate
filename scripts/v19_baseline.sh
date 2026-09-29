#!/usr/bin/env bash
# Re-measure a GaggiMate tree the same way as docs/upstream-v1.9-baseline.md (PRO-653).
#
# Usage: scripts/v19_baseline.sh <tree> [logdir]
#   env BASELINE_JOBS=N          pio -j (default 3; keep <= 4 on the shared host)
#   env SIM_COMPAT_HEADER=<file> if display-sim fails, retry it once with `-include <file>` (out-of-tree workaround
#                                for host-compiler header drift, e.g. scripts/v19_sim_gcc15_compat.h). Off by default.
#
# Runs, in order, against <tree> (no source edits; only build outputs are written):
#   web: npm ci, npm test (if the script exists), scripts/build_webui.sh (npm ci + build + gzip + embed)
#   pio run -e display -t buildfs, then pio run for every [env:*] in platformio.ini except native, sequentially
#   pio test -e native                        (plain; the OTA load test self-skips without OTA_CHAOS_URL)
#   scripts/ota_testbench.sh                  (CI's ota-testbench.yml: chaos server + test_ota_*, 60 iterations)
#   display-sim headless smoke                (offscreen SDL, screenshot mode, curl the embedded WebUI)
# Each step's exit code goes to <logdir>/summary.tsv; sizes to <logdir>/sizes.tsv. A failing step does not stop the run.
# Never flashes, never opens a serial port.
set -uo pipefail

TREE="$(cd "${1:?usage: v19_baseline.sh <tree> [logdir]}" && pwd)"
HERE="$(cd "$(dirname "$0")" && pwd)"
LOG="${2:-$PWD/v19-baseline-logs}"
JOBS="${BASELINE_JOBS:-3}"
mkdir -p "$LOG"
LOG="$(cd "$LOG" && pwd)"
SUMMARY="$LOG/summary.tsv"
: >"$SUMMARY"

# CI never sets NODE_ENV; NODE_ENV=production makes npm ci omit devDependencies (vite).
unset NODE_ENV PLATFORMIO_BUILD_FLAGS

step() { # step <name> <cmd...>
    local name="$1"
    shift
    local start rc
    start=$(date +%s)
    echo "=== $name: $*" | tee "$LOG/$name.log"
    (cd "$TREE" && "$@") >>"$LOG/$name.log" 2>&1
    rc=$?
    printf '%s\t%s\t%ss\n' "$name" "$rc" "$(($(date +%s) - start))" | tee -a "$SUMMARY"
    return 0
}

{
    echo "tree: $TREE"
    echo "commit: $(git -C "$TREE" rev-parse HEAD) ($(git -C "$TREE" describe --tags --always 2>/dev/null))"
    echo "date: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "host: $(uname -srm), $(nproc) cores, $(free -g | awk '/Mem:/{print $2}') GB RAM"
    pio --version
    echo "python (pio): $(pio system info 2>/dev/null | awk '$1=="Python" && $2!="Executable"{print $2}')"
    echo "node: $(node --version)  npm: $(npm --version)"
    echo "host gcc: $(gcc --version | head -1)"
    echo "sdl2: $(sdl2-config --version 2>/dev/null || echo missing)"
} | tee "$LOG/toolchain.txt"

# --- Web ---
step web-npm-ci bash -c 'cd web && npm ci'
if (cd "$TREE/web" && node -e 'process.exit(require("./package.json").scripts?.test ? 0 : 1)'); then
    step web-npm-test bash -c 'cd web && npm test'
else
    echo 'web/package.json has no "test" script' | tee "$LOG/web-npm-test.log"
    printf 'web-npm-test\tNO_SCRIPT\t0s\n' | tee -a "$SUMMARY"
fi
step web-build-embed bash scripts/build_webui.sh

# --- Firmware envs, sequential (buildfs first, same order as build.yml) ---
step pio-display-buildfs pio run -e display -t buildfs
ENVS=$(sed -n 's/^\[env:\(.*\)\]$/\1/p' "$TREE/platformio.ini" | grep -vx native)
: >"$LOG/bins.tsv"
for env in $ENVS; do
    step "pio-$env" pio run -e "$env" -j "$JOBS"
    bin="$TREE/.pio/build/$env/firmware.bin"
    printf '%s\t%s\n' "$env" "$([ -f "$bin" ] && stat -c %s "$bin" || echo -)" >>"$LOG/bins.tsv"
done

# --- Host tests ---
step pio-test-native pio test -e native
step ota-testbench env OTA_LOAD_ITERATIONS=60 bash scripts/ota_testbench.sh

# --- Simulator smoke (offscreen, no window, no hardware) ---
# The compat retry runs last on purpose: PLATFORMIO_BUILD_FLAGS changes the project checksum, and PlatformIO
# then wipes every .pio/build/<env> dir (firmware.bin sizes were already captured in bins.tsv above).
SIM="$TREE/.pio/build/display-sim/program"
if [ ! -x "$SIM" ] && [ -n "${SIM_COMPAT_HEADER:-}" ]; then
    step pio-display-sim-compat env PLATFORMIO_BUILD_FLAGS="-include $(realpath "$SIM_COMPAT_HEADER")" \
        pio run -e display-sim -j "$JOBS"
fi
if [ -x "$SIM" ]; then
    (
        cd "$LOG" && rm -rf sim_data &&
            SDL_VIDEODRIVER=offscreen timeout 60 "$SIM" --screenshot "$LOG/sim-shot.bmp" 12000
    ) >"$LOG/sim-run.log" 2>&1 &
    SIM_PID=$!
    sleep 6
    curl -s -o "$LOG/sim-index.html" -w 'GET / -> %{http_code} %{size_download}B %{content_type}\n' \
        --compressed http://127.0.0.1:8080/ >"$LOG/sim-curl.log" 2>&1
    timeout 15 python3 "$HERE/v19_ws_probe.py" 127.0.0.1 8080 4 >>"$LOG/sim-curl.log" 2>&1
    wait "$SIM_PID"
    printf 'sim-smoke\t%s\t-\n' "$?" | tee -a "$SUMMARY"
    cat "$LOG/sim-curl.log"
else
    printf 'sim-smoke\tNO_BINARY\t-\n' | tee -a "$SUMMARY"
fi

# --- Size extraction (from the build logs + bins.tsv) ---
{
    printf 'env\tflash_used\tflash_max\tram_used\tram_max\tfirmware.bin\n'
    for env in $ENVS; do
        f="$LOG/pio-$env.log"
        flash=$(sed -n 's/^Flash:.*used \([0-9]*\) bytes from \([0-9]*\) bytes.*/\1 \2/p' "$f" | head -1)
        ram=$(sed -n 's/^RAM:.*used \([0-9]*\) bytes from \([0-9]*\) bytes.*/\1 \2/p' "$f" | head -1)
        binsz=$(awk -v e="$env" '$1==e{print $2}' "$LOG/bins.tsv")
        # shellcheck disable=SC2086 # word-split "used max" into two columns
        printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$env" ${flash:-- -} ${ram:-- -} "${binsz:--}"
    done
    wb="$TREE/src/display/webassets/web_ui.bin"
    echo "web_ui.bin: $([ -f "$wb" ] && stat -c %s "$wb" || echo missing) bytes"
    grep -h 'embed_webui: packed' "$LOG/web-build-embed.log"
} | tee "$LOG/sizes.tsv"
