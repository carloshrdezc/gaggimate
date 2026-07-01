# Gaggimate Development Scripts

This directory contains various utility scripts for development and debugging.

## Core Dump Analysis

### `analyze_coredump.py` / `analyze_coredump.sh`

Automated ESP32 core dump analysis for PlatformIO projects.

**Features:**
- Automatically extracts ELF core dump from ESP32 proprietary format
- Uses ESP-IDF GDB tools for detailed analysis
- Shows exact crash location with line numbers
- Displays full call stack (backtrace)
- Shows register values at time of crash
- Lists all threads and their states
- Provides actionable debugging recommendations

**Usage:**
```bash
# Python script (direct)
python3 scripts/analyze_coredump.py <coredump_file> [environment]

# Shell wrapper (simpler)
./scripts/analyze_coredump.sh <coredump_file> [environment]
```

**Examples:**
```bash
# Analyze core dump with default environment (display)
python3 scripts/analyze_coredump.py ~/Downloads/coredump.bin

# Analyze core dump with specific environment
python3 scripts/analyze_coredump.py ~/Downloads/coredump.bin display
python3 scripts/analyze_coredump.py ~/Downloads/coredump.bin controller
python3 scripts/analyze_coredump.py ~/Downloads/coredump.bin display-headless

# Using shell wrapper
./scripts/analyze_coredump.sh ~/Downloads/coredump.bin
./scripts/analyze_coredump.sh ~/Downloads/coredump.bin controller
```

**Requirements:**
- ESP-IDF tools installed (automatic with VS Code ESP-IDF extension)
- PlatformIO project with built firmware
- Python 3.x

**Sample Output:**
```
🚀 ESP32 Core Dump Analyzer
==================================================
Core dump: /home/user/Downloads/coredump.bin
Environment: display

✅ Found GDB: xtensa-esp32s3-elf-gdb
✅ ELF header found at offset: 20
✅ Extracted ELF core dump to: /tmp/tmpXXXXX.elf

================================================================================
🔍 CORE DUMP ANALYSIS
================================================================================
#0  DefaultUI::updateStatusScreen (this=0x3fced5e4) at src/display/ui/default/DefaultUI.cpp:655
655         if (process->getType() != MODE_BREW) {
#1  0x420254df in DefaultUI::loop (this=0x3fced5e4) at src/display/ui/default/DefaultUI.cpp:230
#2  0x42025510 in DefaultUI::loopTask (arg=0x3fced5e4) at src/display/ui/default/DefaultUI.cpp:766
...
```

**Getting Core Dumps:**
1. **From Web Interface:** Visit `http://your-device-ip/`, go to System & Updates, click "Download Core Dump"
2. **From Serial Monitor:** Core dumps appear in terminal output after crashes
3. **From Device Flash:** Use `esptool.py` to read core dump partition

**Interactive Analysis:**
For deeper debugging, use the extracted ELF file with GDB interactively:
```bash
xtensa-esp32s3-elf-gdb .pio/build/display/firmware.elf
(gdb) core-file /tmp/extracted_coredump.elf
(gdb) bt
(gdb) list
(gdb) info locals
(gdb) print variable_name
```

## Pre-nightly Boot Smoke-test (HIL)

### `boot_smoke_test.py`

Manual on-device boot gate (approach B from `docs/boot-smoke-test-spike.md`,
PRO-332). CI is compile-only and the desktop simulator stubs the hardware / IDF /
NVS layers, so neither catches boot/runtime/NVS regressions — only a real boot
does. Run this on the reference LilyGo-T-RGB **before tagging a nightly** and on
any firmware / platform / lib-pin PR.

**What it asserts:**
- esptool flash of bootloader + partitions + boot_app0 + firmware, every region
  reporting `Hash of data verified` (flash integrity).
- ~20 s of boot serial with no panic (`Guru Meditation` / `LoadProhibited` /
  `abort()`), BLE init reached and survived, `Started webserver` seen, and
  exactly one ROM reset (no boot loop) — catches PRO-329 and PRO-330.
- Settings round-trip: write a known value via `POST /api/settings`, reboot, read
  it back via `GET /api/settings`, assert it persisted — catches PRO-331.

Fails with a non-zero exit on any assertion miss and always saves the captured
serial to an artifact file. Exit code 2 / `RESULT: INCOMPLETE` is returned
instead of pass/fail when the PRO-331 settings round-trip gate was skipped (no
`--base-url` given and `--skip-settings-roundtrip` not passed) — treat it as
not-safe-to-tag, not as a pass.

**Usage:**
```bash
pio run -e display                                   # produce the flash images first
python3 scripts/boot_smoke_test.py --base-url http://<board-ip>
python3 scripts/boot_smoke_test.py --help            # all options (works with no board)
```

Pure offline tooling: `--help` and import work with no board attached and without
`pyserial` / `esptool` / `requests` installed (those are deferred imports used
only on the hardware path). See `CONTRIBUTING.md` "Pre-nightly on-device boot
smoke-test (HIL)" for when it is mandatory and how to read the result.
