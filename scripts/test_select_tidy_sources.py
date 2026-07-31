#!/usr/bin/env python3
"""Regression tests for scripts/select_tidy_sources.py (PRO-608).

These exist to catch a SILENT SHRINKAGE of clang-tidy's scope. PRO-608 was
exactly that failure: the CI step ran, printed a file list, exited 0, and lint
99% of `src/display/` was never analysed — because the compile DB fed to it only
ever held three real sources. Nothing failed, so nothing was noticed for months.

Two layers of defence, both here:
  1. Unit tests over a synthetic compile DB — pin the filtering contract
     (what is in scope, what is out, path-shape handling).
  2. `test_real_compile_db_meets_minimum` — when a real compile_commands.json is
     present at the project root, assert it actually contains the expected
     firmware-logic sources. The CI step asserts a minimum count too (see the
     `Run clang-tidy over in-scope logic` step in .github/workflows/ci.yml);
     this test is the same guard reachable locally without CI.

Run with no dependencies (there is no pytest in this repo):

    python3 scripts/test_select_tidy_sources.py
"""
import json
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from select_tidy_sources import select  # noqa: E402

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Sources that MUST be in the clang-tidy set once the native-tidy compile DB is
# generated. Deliberately a small, load-bearing subset — the four biggest
# hand-written logic TUs plus a policy-bearing test suite — not the full list,
# so adding/removing a plugin doesn't churn this test. Growing the list is fine;
# shrinking it should require justifying why coverage went down.
REQUIRED_IN_REAL_DB = (
    "src/display/core/Controller.cpp",
    "src/display/core/Settings.cpp",
    "src/display/core/PluginManager.cpp",
    "src/display/plugins/WebUIPlugin.cpp",
    "src/display/plugins/ShotHistoryPlugin.cpp",
)

# Mirrors MIN_TIDY_FILES in .github/workflows/ci.yml. Keep the two in sync.
# The native-tidy DB selects 15 files (14 firmware TUs + test/tidy/tidy_seam_headers.cpp).
# 12 leaves room to retire a plugin from the env without churn, while still failing
# loudly on the PRO-608 regression shape (the old native DB selected 3).
MIN_TIDY_FILES = 12


def db(*files, directory="/repo"):
    return [{"directory": directory, "command": "g++ -c " + f, "file": f} for f in files]


class SelectScope(unittest.TestCase):
    def test_selects_firmware_logic_sources(self):
        got = select(db("src/display/core/Controller.cpp", "src/display/plugins/WebUIPlugin.cpp"))
        self.assertEqual(
            got,
            [
                os.path.normpath("/repo/src/display/core/Controller.cpp"),
                os.path.normpath("/repo/src/display/plugins/WebUIPlugin.cpp"),
            ],
        )

    def test_selects_test_suites_so_policy_seam_headers_get_a_tu(self):
        # PRO-608: the *Policy.h headers are header-only and included ONLY by
        # test/test_*/ sources. If these stop being selected, .clang-tidy's
        # HeaderFilterRegex has no translation unit to fire on and ~31 seam
        # headers silently go unanalysed again.
        got = select(db("test/test_ws_reassembly_cap/test_ws_reassembly_cap.cpp"))
        self.assertEqual(len(got), 1)
        self.assertTrue(got[0].endswith(os.path.normpath("test/test_ws_reassembly_cap/test_ws_reassembly_cap.cpp")))

    def test_selects_ota_semver_sources(self):
        # Linted before PRO-608 (by accident, via the old "/src/" substring
        # match); kept explicitly so the rewrite is not a coverage regression.
        got = select(db("lib/OTA/src/semver.c", "lib/OTA/src/semver_extensions.cpp"))
        self.assertEqual(len(got), 2)

    def test_excludes_generated_ui_and_vendored_drivers(self):
        self.assertEqual(
            select(
                db(
                    "src/display/ui/default/lvgl/ui_BrewScreen.c",
                    "src/display/drivers/LilyGoDriver.cpp",
                    "src/display/webassets/web_ui_blob.S",
                )
            ),
            [],
        )

    def test_excludes_pio_build_dir_and_vendored_libdeps(self):
        # THE regression this rewrite exists for: a vendored library with its own
        # src/ dir (lvgl) matched the old `"/src/" in norm` heuristic, so the
        # native-tidy DB would have handed clang-tidy ~190 LVGL files.
        self.assertEqual(
            select(
                db(
                    ".pio/libdeps/native-tidy/lvgl/src/core/lv_obj.c",
                    ".pio/libdeps/native-tidy/Nanopb/pb_decode.c",
                    ".pio/build/native-tidy/nanopb/generated-src/comms.pb.c",
                )
            ),
            [],
        )

    def test_excludes_other_vendored_lib_subtrees(self):
        self.assertEqual(select(db("lib/NimBLEComm/src/NimBLEComm.cpp")), [])

    def test_handles_absolute_paths(self):
        got = select(db("/home/runner/work/gaggimate/src/display/core/Settings.cpp", directory="/home/runner/work/gaggimate"))
        self.assertEqual(got, [os.path.normpath("/home/runner/work/gaggimate/src/display/core/Settings.cpp")])

    def test_absolute_path_outside_the_project_is_rejected(self):
        # An absolute entry that isn't under `directory` is not our source tree,
        # even if it happens to contain a src/ segment.
        self.assertEqual(select(db("/opt/vendor/thing/src/thing.cpp", directory="/repo")), [])

    def test_handles_windows_separators(self):
        got = select(db(r"src\display\core\Settings.cpp"))
        self.assertEqual(len(got), 1)

    def test_deduplicates_and_sorts(self):
        got = select(db("src/b.cpp", "src/a.cpp", "src/a.cpp"))
        self.assertEqual(got, [os.path.normpath("/repo/src/a.cpp"), os.path.normpath("/repo/src/b.cpp")])

    def test_skips_entries_without_a_file_key(self):
        self.assertEqual(select([{"directory": "/repo", "command": "g++"}]), [])

    def test_relative_paths_without_directory_are_kept_relative(self):
        got = select(db("src/display/core/utils.cpp", directory=""))
        self.assertEqual(got, [os.path.normpath("src/display/core/utils.cpp")])


class RealCompileDb(unittest.TestCase):
    """Guards the actual DB when one has been generated (skipped otherwise)."""

    def setUp(self):
        self.path = os.path.join(PROJECT_ROOT, "compile_commands.json")
        if not os.path.isfile(self.path):
            self.skipTest("no compile_commands.json at project root; run `pio run -e native-tidy -t compiledb`")
        with open(self.path, encoding="utf-8") as fh:
            self.selected = select(json.load(fh))
        self.rel = {os.path.relpath(p, PROJECT_ROOT).replace("\\", "/") for p in self.selected}

    def test_real_compile_db_meets_minimum(self):
        self.assertGreaterEqual(
            len(self.selected),
            MIN_TIDY_FILES,
            f"clang-tidy scope shrank to {len(self.selected)} files (expected >= {MIN_TIDY_FILES}). "
            "Either the compile DB was generated from the wrong env (use native-tidy, not native) "
            "or a build_src_filter change dropped firmware logic. See PRO-608.",
        )

    def test_real_compile_db_contains_required_sources(self):
        missing = [f for f in REQUIRED_IN_REAL_DB if f not in self.rel]
        self.assertFalse(missing, f"firmware logic missing from the clang-tidy set: {missing} (see PRO-608)")

    def test_real_compile_db_has_no_vendored_or_generated_files(self):
        bad = [f for f in self.rel if f.startswith(".pio/") or "/display/ui/" in f or "/display/drivers/" in f]
        self.assertFalse(bad, f"vendored/generated files leaked into the clang-tidy set: {bad[:10]}")


if __name__ == "__main__":
    unittest.main(verbosity=2)
