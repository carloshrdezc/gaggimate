#!/usr/bin/env python3
"""
Pre-nightly on-device boot smoke-test (HIL) for the GaggiMate display firmware.

This is approach (B) from docs/boot-smoke-test-spike.md (PRO-332): a documented
manual boot-gate a maintainer runs on the reference board (a LilyGo-T-RGB on
/dev/ttyACM0) BEFORE tagging a nightly, and on any firmware / platform / lib-pin
PR. It exists because CI is compile-only and the desktop simulator
([env:display-sim], platform=native) stubs the hardware/IDF/NVS layers, so
neither sees boot/runtime/NVS regressions -- only a real boot does. Three such
regressions shipped to nightly while building perfectly clean:

    PRO-329  legacy ADC vs driver_ng conflict -> abort() at C++ static init
    PRO-330  NimBLE 2.x esp_bt_controller_init LoadProhibited panic
    PRO-331  Preferences::begin(readOnly=true) races NVS init -> all settings
             (incl. WiFi) silently reset to default every boot

What this script does (each step maps to a bug -- see docs/boot-smoke-test-spike.md):

  1. Flash bootloader + partitions + boot_app0 + firmware with esptool, and
     assert every region reports "Hash of data verified".
  2. Capture ~20s of boot serial with pyserial and assert:
       * No abort() / "Guru Meditation" / LoadProhibited / Backtrace        (PRO-329, PRO-330)
       * Reaches the BLE-init marker and continues past it without a panic   (PRO-330)
       * Reaches steady-state markers ("Started webserver")                  (liveness)
       * No boot loop: the ROM reset banner "ESP-ROM:esp32s3" appears once   (PRO-329 loop form)
  3. Settings round-trip: read current mainBrightness via GET /api/settings,
     write a DIFFERENT valid value via POST /api/settings (form-encoded, the
     way the web UI submits), reboot, read it back via GET /api/settings, and
     assert read == written                                                  (PRO-331)
  4. On ANY assertion miss, exit non-zero. The captured serial is always
     written to an artifact file for post-mortem.

Exit codes:
  0  PASS       -- every check that ran passed (an explicit
                  --skip-settings-roundtrip opt-out still counts as PASS).
  1  FAIL       -- one or more assertions missed.
  2  INCOMPLETE -- boot checks passed but the PRO-331 settings round-trip was
                  skipped because --base-url was not provided. NOT a full green
                  run: the most important regression gate never executed. Pass
                  --base-url (or --skip-settings-roundtrip to opt out explicitly).

Serial capture ordering: the board is reset only AFTER the serial port is
open (esptool runs with --after no_reset, then capture_serial pulses reset via
_pulse_reset once the port is open), so the earliest boot output -- including
the ESP-ROM:esp32s3 banner the boot loop assertion counts -- is guaranteed to
land inside the capture window rather than being lost before pyserial opens the
port. The same open-then-reset ordering is used for the round-trip reboot. The
boot-loop banner assertion is softened when no reset is triggered (--no-flash
without --reset-on-capture) because an already-booted board emits no fresh ROM
banner.

OFFLINE-SAFE: this module imports cleanly and `--help` works with NO board
attached and WITHOUT pyserial/esptool/requests installed -- those third-party
imports are deferred into the functions that actually talk to hardware. CI never
runs the hardware path; the on-board run is the maintainer's manual acceptance
step.

Usage:
    python3 scripts/boot_smoke_test.py [options]

Examples:
    # Full gate on the default reference board (flash + boot capture + round-trip)
    python3 scripts/boot_smoke_test.py

    # Different port / longer capture; skip flashing an already-flashed board
    python3 scripts/boot_smoke_test.py --port /dev/ttyACM1 --capture-seconds 30 --no-flash

    # Skip the settings round-trip (e.g. board not on the network yet)
    python3 scripts/boot_smoke_test.py --skip-settings-roundtrip

Author: GaggiMate project (PRO-332)
"""

import argparse
import re
import subprocess
import sys
import time
from pathlib import Path

# --- Boot markers (real strings emitted by the current firmware on dev-master) ---

# Panic / crash signatures. Any of these in the boot window is a hard failure
# (PRO-329 abort, PRO-330 LoadProhibited). ROM reset banners are counted
# separately (see ROM_RESET_BANNER) to detect the boot-loop form.
PANIC_PATTERNS = [
    "Guru Meditation",
    "LoadProhibited",
    "StoreProhibited",
    "abort()",
    "assert failed",
    "Backtrace:",
]

# NimBLEClientController::initClient() logs this immediately before
# NimBLEDevice::init() -- the call that LoadProhibited'd in PRO-330. Reaching it
# AND continuing past it without a panic proves BLE init survived.
BLE_INIT_MARKER = "Pre-BLE-init heap:"

# WebUIPlugin logs this once the async webserver is up -- a steady-state marker
# a board that aborted early never reaches.
WEBSERVER_MARKER = "Started webserver"

# The ESP32-S3 second-stage ROM banner. Exactly one per healthy boot; more than
# one inside the capture window means the board is boot-looping.
ROM_RESET_BANNER = "ESP-ROM:esp32s3"

DEFAULT_PORT = "/dev/ttyACM0"
DEFAULT_BAUD = 115200
DEFAULT_BUILD_DIR = ".pio/build/display"
DEFAULT_CAPTURE_SECONDS = 20.0
DEFAULT_ARTIFACT = "boot_smoke_serial.log"
# docs/boot_app0.bin is committed in the repo; the other binaries are produced
# by `pio run -e display` into the build dir.
DEFAULT_BOOT_APP0 = "docs/boot_app0.bin"


def _info(msg):
    print(f"  {msg}")


def _ok(msg):
    print(f"\u2705 {msg}")


def _fail(msg):
    print(f"\u274c {msg}")


def _warn(msg):
    print(f"\u26a0\ufe0f  WARNING: {msg}")


def _section(title):
    print("\n" + "=" * 72)
    print(title)
    print("=" * 72)


def resolve_flash_images(build_dir, boot_app0):
    """Return the ordered (offset, path) flash regions, or raise if any are missing.

    Offsets are the standard ESP32-S3 layout used by the Arduino/PlatformIO
    `display` env (default board partition table): bootloader @ 0x0,
    partitions @ 0x8000, boot_app0 @ 0xe000, application @ 0x10000.
    """
    build = Path(build_dir)
    regions = [
        ("0x0", build / "bootloader.bin"),
        ("0x8000", build / "partitions.bin"),
        ("0xe000", Path(boot_app0)),
        ("0x10000", build / "firmware.bin"),
    ]
    missing = [str(p) for _, p in regions if not p.exists()]
    if missing:
        raise FileNotFoundError(
            "Missing flash image(s): "
            + ", ".join(missing)
            + "\n  Build the firmware first: pio run -e display"
        )
    return regions


def flash_board(port, baud, regions):
    """Flash all regions with esptool and assert every region verifies.

    Returns True on success. Raises RuntimeError if any region fails to verify.
    """
    # Deferred import: keeps --help / py_compile working without esptool.
    try:
        import esptool  # noqa: F401
    except ImportError:
        raise RuntimeError(
            "esptool is not installed. Install it with: pip install esptool"
        )

    cmd = [
        sys.executable,
        "-m",
        "esptool",
        "--chip",
        "esp32s3",
        "--port",
        port,
        "--baud",
        str(baud),
        "--before",
        "default_reset",
        "--after",
        "no_reset",
        "write_flash",
        "-z",
        "--flash_mode",
        "dio",
        "--flash_freq",
        "80m",
    ]
    for offset, path in regions:
        cmd += [offset, str(path)]

    _info("esptool " + " ".join(cmd[2:]))
    proc = subprocess.run(cmd, capture_output=True, text=True)
    output = proc.stdout + proc.stderr
    print(output)

    if proc.returncode != 0:
        raise RuntimeError(f"esptool exited with code {proc.returncode}")

    # esptool prints one "Hash of data verified." per written region.
    verified = output.count("Hash of data verified")
    if verified < len(regions):
        raise RuntimeError(
            f"esptool verified only {verified}/{len(regions)} regions "
            "(expected one 'Hash of data verified' per region)"
        )
    _ok(f"Flash verified: {verified}/{len(regions)} regions hashed OK")
    return True


def _pulse_reset(ser):
    """Pulse the board's EN line low via DTR/RTS on an already-open serial port.

    This is the classic ESP32 auto-reset sequence. NOTE: auto-reset over DTR/RTS
    is board/adapter-dependent -- some LilyGo-T-RGB / native-USB-CDC (ACM)
    setups do not expose the classic EN/BOOT strapping over the CDC ACM
    interface, so this pulse may be a no-op there. On such boards use the
    --no-flash manual-reset fallback (physically tap the reset button during the
    capture window) instead of relying on this pulse.
    """
    ser.setDTR(False)
    ser.setRTS(True)
    time.sleep(0.1)
    ser.setRTS(False)
    time.sleep(0.1)


def capture_serial(port, baud, seconds, artifact_path, reset_on_open=False):
    """Capture serial output for `seconds` and return the captured text.

    Always writes what was captured to `artifact_path` (even on partial capture).

    When `reset_on_open` is True the board's reset line is pulsed AFTER the port
    is open, so the earliest boot output (the ESP-ROM:esp32s3 banner that
    assert_boot_healthy counts) is guaranteed to fall inside the capture window
    rather than being lost in the race between reset and the port opening.
    """
    # Deferred import: keeps --help / py_compile working without pyserial.
    try:
        import serial  # pyserial
    except ImportError:
        raise RuntimeError(
            "pyserial is not installed. Install it with: pip install pyserial"
        )

    _info(f"Opening {port} @ {baud} for {seconds:.0f}s of boot capture...")
    chunks = []
    try:
        with serial.Serial(port, baud, timeout=0.2) as ser:
            if reset_on_open:
                _info("Port open; pulsing reset to capture the boot from the start...")
                _pulse_reset(ser)
            deadline = time.monotonic() + seconds
            while time.monotonic() < deadline:
                data = ser.read(4096)
                if data:
                    chunks.append(data.decode("utf-8", errors="replace"))
    finally:
        text = "".join(chunks)
        Path(artifact_path).write_text(text, encoding="utf-8")
        _info(f"Serial artifact written: {artifact_path} ({len(text)} chars)")
    return text


def assert_boot_healthy(serial_text, expect_reset_banner=True):
    """Assert the captured boot serial is healthy. Returns a list of failures.

    `expect_reset_banner` controls the ROM-reset-banner check: when True (the
    board was reset during this capture) exactly one banner is required and
    zero/many are failures. When False (no reset was triggered -- e.g. a
    --no-flash capture of an already-booted board) a missing banner is NOT a
    failure, but more than one is still flagged as a boot loop.
    """
    failures = []

    # PRO-329 / PRO-330: no panic signatures.
    for pat in PANIC_PATTERNS:
        if pat in serial_text:
            failures.append(f"panic signature present in boot log: {pat!r}")

    # PRO-330: BLE init was reached AND survived (marker present, no panic after it).
    if BLE_INIT_MARKER not in serial_text:
        failures.append(
            f"BLE-init marker {BLE_INIT_MARKER!r} not seen "
            "(BLE stack never initialized -- possible PRO-330 regression)"
        )

    # Liveness: reached steady state.
    if WEBSERVER_MARKER not in serial_text:
        failures.append(
            f"steady-state marker {WEBSERVER_MARKER!r} not seen "
            "(firmware did not reach a running webserver)"
        )

    # PRO-329 (loop form): exactly one ROM reset banner in the window.
    reset_count = len(re.findall(re.escape(ROM_RESET_BANNER), serial_text))
    if reset_count == 0:
        if expect_reset_banner:
            failures.append(
                f"ROM reset banner {ROM_RESET_BANNER!r} not seen at all "
                "(serial capture may have missed the boot -- power-cycle and retry)"
            )
        # else: no reset was triggered (already-booted board); absence is expected.
    elif reset_count > 1:
        failures.append(
            f"boot loop detected: {reset_count} ROM resets in the capture window "
            "(expected exactly 1 -- possible PRO-329 static-init abort loop)"
        )

    return failures


def settings_roundtrip(base_url, port, baud, capture_seconds, artifact_path, timeout):
    """PRO-331 gate: write a setting, reboot, read it back, assert it survived.

    Reads the current mainBrightness via GET /api/settings, writes a DIFFERENT
    valid value via POST /api/settings (form-encoded, exactly how the web UI
    submits the settings form -- the route has no JSON body parser, so the
    handler only sees fields via request->arg(), which is populated from query
    params / form bodies, NOT from a raw JSON body), hard-resets the board, waits
    for it to come back, then reads GET /api/settings and asserts the value
    persisted. The post-reboot boot serial is captured and asserted healthy too
    (it is the most important boot for PRO-331). Returns a list of failures
    (empty == pass).
    """
    # Deferred import: keeps --help / py_compile working without requests.
    try:
        import requests
    except ImportError:
        return [
            "requests is not installed (needed for the settings round-trip). "
            "Install it with: pip install requests, or pass --skip-settings-roundtrip"
        ]

    settings_url = base_url.rstrip("/") + "/api/settings"
    failures = []

    # mainBrightness is a real, persisted, boot-safe display-brightness setting
    # (0-255) read back verbatim in the GET response. We read the current value
    # and write a DIFFERENT valid sentinel so the round-trip actually proves
    # persistence (writing the value it already has would pass trivially).
    sentinel_key = "mainBrightness"
    primary_sentinel = 42
    alt_sentinel = 200  # used if the board already happens to hold primary_sentinel

    try:
        _info(f"GET  {settings_url} (read baseline)")
        before = requests.get(settings_url, timeout=timeout).json()
        original = before.get(sentinel_key)

        # Choose a sentinel that differs from the current value so the read-back
        # comparison genuinely proves the new value persisted.
        try:
            original_int = int(original)
        except (TypeError, ValueError):
            original_int = None
        sentinel_value = alt_sentinel if original_int == primary_sentinel else primary_sentinel

        # Form-encoded body (Content-Type: application/x-www-form-urlencoded).
        # requests sends `data=` as a form body, which AsyncWebServer parses into
        # request->arg(); a `json=` body would be ignored by handleSettings.
        _info(f"POST {settings_url} (form) {{{sentinel_key}: {sentinel_value}}}")
        requests.post(
            settings_url, data={sentinel_key: sentinel_value}, timeout=timeout
        ).raise_for_status()
    except Exception as exc:  # network / HTTP / JSON failure
        return [f"settings round-trip could not write the sentinel: {exc}"]

    # Reboot and recapture so a fresh NVS read happens (this is the PRO-331 path).
    # capture_serial opens the port FIRST, then pulses reset, so the boot is
    # captured from the ROM banner onward without a race.
    _info("Rebooting board (reset after port open) to force a fresh NVS read...")
    try:
        roundtrip_serial = capture_serial(
            port, baud, capture_seconds, artifact_path + ".roundtrip", reset_on_open=True
        )
    except RuntimeError as exc:
        failures.append(f"could not capture round-trip reboot serial: {exc}")
        return failures

    # The post-reboot boot is the most important one for PRO-331: assert it is
    # healthy too (a reset was triggered, so the ROM banner is expected).
    for f in assert_boot_healthy(roundtrip_serial, expect_reset_banner=True):
        failures.append(f"post-reboot boot (round-trip): {f}")

    # Wait for the network to come back, then read the value back.
    read_back = None
    deadline = time.monotonic() + max(timeout, 15)
    while time.monotonic() < deadline:
        try:
            after = requests.get(settings_url, timeout=timeout).json()
            read_back = after.get(sentinel_key)
            break
        except Exception:
            time.sleep(2)

    if read_back is None:
        failures.append(
            "settings round-trip: board did not respond to GET /api/settings after reboot"
        )
    else:
        try:
            read_back_int = int(read_back)
        except (TypeError, ValueError):
            read_back_int = None
        if read_back_int != sentinel_value:
            failures.append(
                f"settings round-trip FAILED (PRO-331 regression): wrote {sentinel_key}="
                f"{sentinel_value}, read back {read_back!r} after reboot "
                f"(was {original!r} before) -- NVS did not persist the value"
            )
        else:
            _ok(f"settings round-trip: {sentinel_key}={sentinel_value} survived reboot")

    return failures


def build_arg_parser():
    parser = argparse.ArgumentParser(
        prog="boot_smoke_test.py",
        description=(
            "Pre-nightly on-device boot smoke-test (HIL) for the GaggiMate display "
            "firmware. Flashes the reference board, captures boot serial, and asserts "
            "the boot is healthy (catches PRO-329 / PRO-330 / PRO-331). Run it on the "
            "reference LilyGo-T-RGB before tagging a nightly. See "
            "docs/boot-smoke-test-spike.md."
        ),
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument("--port", default=DEFAULT_PORT, help="Serial port of the board")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD, help="Serial baud rate")
    parser.add_argument(
        "--build-dir",
        default=DEFAULT_BUILD_DIR,
        help="PlatformIO build dir holding bootloader.bin / partitions.bin / firmware.bin",
    )
    parser.add_argument(
        "--boot-app0", default=DEFAULT_BOOT_APP0, help="Path to boot_app0.bin"
    )
    parser.add_argument(
        "--capture-seconds",
        type=float,
        default=DEFAULT_CAPTURE_SECONDS,
        help="Seconds of boot serial to capture and assert against",
    )
    parser.add_argument(
        "--artifact",
        default=DEFAULT_ARTIFACT,
        help="Path to write the captured boot serial (the run artifact)",
    )
    parser.add_argument(
        "--no-flash",
        action="store_true",
        help="Skip flashing (board already has the image under test)",
    )
    parser.add_argument(
        "--reset-on-capture",
        action="store_true",
        help=(
            "Pulse the board reset at the start of the boot capture even on a "
            "--no-flash run (so the ROM banner / boot is captured and the "
            "boot-loop assertion stays active). Ignored without --no-flash "
            "since a flashed board is always reset for capture."
        ),
    )
    parser.add_argument(
        "--skip-settings-roundtrip",
        action="store_true",
        help="Skip the PRO-331 settings persistence round-trip",
    )
    parser.add_argument(
        "--base-url",
        default=None,
        help="Board HTTP base URL for the settings round-trip, e.g. http://192.168.1.50",
    )
    parser.add_argument(
        "--http-timeout",
        type=float,
        default=10.0,
        help="Per-request HTTP timeout (seconds) for the settings round-trip",
    )
    return parser


def main(argv=None):
    args = build_arg_parser().parse_args(argv)

    all_failures = []

    _section("GaggiMate boot smoke-test (HIL) -- PRO-332")
    print(f"Port: {args.port} @ {args.baud}   Build dir: {args.build_dir}")
    print(f"Capture: {args.capture_seconds:.0f}s   Artifact: {args.artifact}")

    # --- Step 1: flash + verify ---
    if args.no_flash:
        _info("Skipping flash (--no-flash); using whatever image is on the board.")
    else:
        _section("Step 1/3: flash + verify (esptool)")
        try:
            regions = resolve_flash_images(args.build_dir, args.boot_app0)
            flash_board(args.port, args.baud, regions)
        except (FileNotFoundError, RuntimeError) as exc:
            _fail(str(exc))
            print("\nRESULT: FAIL (flash/verify step)")
            return 1

    # --- Step 2: capture boot serial + assert healthy ---
    _section("Step 2/3: capture boot serial + assert healthy")
    # esptool runs with --after no_reset, so a flashed board is reset HERE (after
    # the port is open) to capture the boot from the ROM banner. A --no-flash run
    # captures an already-booted board and does NOT reset unless --reset-on-capture
    # is passed; in that case the ROM banner is not expected and the boot-loop
    # banner assertion is softened accordingly.
    did_reset = (not args.no_flash) or args.reset_on_capture
    if args.no_flash and not did_reset:
        _info(
            "No reset will be triggered (--no-flash without --reset-on-capture); "
            "capturing the already-booted board. The ROM-banner check is softened "
            "-- pass --reset-on-capture (or tap reset during the window) to exercise "
            "the boot-loop assertion."
        )
    try:
        serial_text = capture_serial(
            args.port, args.baud, args.capture_seconds, args.artifact,
            reset_on_open=did_reset,
        )
    except RuntimeError as exc:
        _fail(str(exc))
        print("\nRESULT: FAIL (serial capture step)")
        return 1

    boot_failures = assert_boot_healthy(serial_text, expect_reset_banner=did_reset)
    if boot_failures:
        for f in boot_failures:
            _fail(f)
    else:
        _ok("boot serial healthy: no panics, BLE init survived, webserver up, single boot")
    all_failures += boot_failures

    # --- Step 3: settings round-trip (PRO-331) ---
    # roundtrip_incomplete tracks the IMPLICIT skip (no --base-url and no explicit
    # --skip-settings-roundtrip). In that case the PRO-331 gate never ran, so a
    # clean boot must NOT be reported as a full PASS -- see the verdict block.
    roundtrip_incomplete = False
    if args.skip_settings_roundtrip:
        _info("Skipping settings round-trip (--skip-settings-roundtrip).")
    elif not args.base_url:
        roundtrip_incomplete = True
        _warn(
            "No --base-url given; the PRO-331 settings round-trip did NOT run. "
            "This is the most important regression gate -- the result below is "
            "therefore NOT a full pass. Pass --base-url http://<board-ip> to "
            "exercise it, or --skip-settings-roundtrip to intentionally opt out."
        )
    else:
        _section("Step 3/3: settings round-trip (PRO-331)")
        rt_failures = settings_roundtrip(
            args.base_url,
            args.port,
            args.baud,
            args.capture_seconds,
            args.artifact,
            args.http_timeout,
        )
        for f in rt_failures:
            _fail(f)
        all_failures += rt_failures

    # --- Verdict ---
    # Three outcomes, with distinct exit codes so CI/automation can tell them apart:
    #   1 = FAIL       (one or more assertions missed)
    #   2 = INCOMPLETE (boot checks passed, but the PRO-331 round-trip was skipped
    #                   because --base-url was not provided -- NOT a full green run)
    #   0 = PASS       (all checks that ran passed; an explicit --skip-settings-roundtrip
    #                   opt-out is intentional and still counts as PASS)
    _section("RESULT")
    if all_failures:
        print(f"FAIL: {len(all_failures)} assertion(s) missed. See {args.artifact}.")
        return 1
    if roundtrip_incomplete:
        print(
            "INCOMPLETE: boot checks passed, but the PRO-331 settings round-trip was "
            "SKIPPED because --base-url was not provided. This is NOT a full green run "
            "-- the most important regression gate did not execute. Pass "
            "--base-url http://<board-ip> to exercise it (exit code 2)."
        )
        return 2
    print(f"PASS: boot smoke-test green. Artifact: {args.artifact}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
