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

## Release / Promotion

### `generate_promotion_pr_body.py`

Generates the PR body for a `dev-master` -> `master` promotion PR (PRO-644).

Promotion PRs bundle everything that has landed on `dev-master` since the last
promotion (PR #634 carried 39 commits) and were being opened by hand with an
**empty body**, so the only way to see what was shipping to `master` was to
expand the commit list. This recurred across several promotions. Run the script
and pipe the output into `gh pr create --body-file` instead.

**Usage:**
```bash
# defaults to origin/master..origin/dev-master
python3 scripts/generate_promotion_pr_body.py --fetch > /tmp/promo-body.md
gh pr create --base master --head dev-master \
  --title "chore(release): promote dev-master to master" \
  --body-file /tmp/promo-body.md

# other ranges / local refs
python3 scripts/generate_promotion_pr_body.py --base master --head dev-master
```

**Emits:** the commit count and the number of distinct merged PRs in the range,
the Linear issues closed since the last promotion (split from the ones only
referenced with `Ref PRO-NNN`), each with the commit subject that referenced it,
and the commit range itself.

Deliberately **no closing keywords** (`Fixes`/`Closes`/`Resolves`) in the output:
those issues were already closed when their own PR merged into `dev-master`, and
repeating the magic words here would make Linear re-close them on the promotion
merge, clobbering whatever state they had reached since. Plain `PRO-NNN` keys
still auto-link.

**Requirements:** `git` on PATH and Python 3.x. No third-party dependencies.
Regression tests: `python3 scripts/test_generate_promotion_pr_body.py` (also run
in the `web` CI job).
