#!/usr/bin/env python3
"""Regression tests for scripts/generate_stable_versions.py (PRO-648).

Same shape as scripts/test_generate_promotion_pr_body.py and
scripts/test_select_tidy_sources.py: stdlib `unittest`, no third-party
dependencies, run directly.

    python3 scripts/test_generate_stable_versions.py

Why this file exists
--------------------
PRO-648 was reported as "the committed manifest is stale at 2.0.17". It is not
committed at all — `src/stable_versions.h` is gitignored and generator-owned
(CAR-248), so there is nothing in git to go stale. What *can* silently go stale
is a **developer's untracked working-tree copy**: the generator's offline branch
deliberately keeps an existing header when the GitHub API is unreachable, so a
rate-limited or offline build reuses whatever that machine last wrote. A stale
local header is invisible until someone reads it and files a bug against it.

The real defect this locks down is that the header's *content* is derived from
the releases feed by `_filter_stable`, and nothing exercised that function. A
regression there (dropping the prerelease filter, mis-slicing MAX_VERSIONS,
letting the moving `nightly`/`beta` tags through) produces a header that looks
entirely plausible — right shape, right count, compiles fine — while pointing
the OTA dropdown at tags that 404 or at a prerelease. That is the same
silent-nothing failure class as the empty promotion bodies in PRO-644.

Everything here runs against FABRICATED release payloads shaped like the real
`GET /repos/carloshrdezc/gaggimate/releases` response — no network, no git
repo, no subprocess. The network wrapper (`_fetch_releases`) is a thin
urllib call; the filtering and rendering are where correctness lives.

Note this module `import`s the generator. That import used to be a live network
fetch and a write to src/stable_versions.h, because the script ran `main()` in a
bare `else:` for PlatformIO's `pre:` hook. It is now gated on the SCons `env`
injection, which is what makes this test file possible at all.
"""
import os
import re
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import generate_stable_versions as gsv  # noqa: E402

# The real feed at the time of PRO-648, newest first, trimmed to the fields the
# generator reads. `nightly` and `beta` are real moving prerelease tags in this
# repo and they sit at/near the top of the feed, which is exactly why the
# filter cannot be a naive "take the first 5".
REAL_FEED = [
    {"tag_name": "nightly", "prerelease": True, "draft": False},
    {"tag_name": "2.1.0", "prerelease": False, "draft": False},
    {"tag_name": "beta", "prerelease": True, "draft": False},
    {"tag_name": "2.0.17", "prerelease": False, "draft": False},
    {"tag_name": "2.0.16", "prerelease": False, "draft": False},
    {"tag_name": "2.0.15", "prerelease": False, "draft": False},
    {"tag_name": "2.0.14", "prerelease": False, "draft": False},
    {"tag_name": "2.0.13", "prerelease": False, "draft": False},
    {"tag_name": "2.0.12", "prerelease": False, "draft": False},
]


class FilterStable(unittest.TestCase):
    def test_real_feed_puts_the_latest_stable_release_first(self):
        # PRO-648's acceptance criterion: 2.1.0 leads the list, not 2.0.17.
        got = gsv._filter_stable(REAL_FEED)
        self.assertEqual(got, ["2.1.0", "2.0.17", "2.0.16", "2.0.15", "2.0.14"])
        self.assertEqual(got[0], "2.1.0")

    def test_excludes_prereleases(self):
        got = gsv._filter_stable(REAL_FEED)
        self.assertNotIn("nightly", got)
        self.assertNotIn("beta", got)
        for tag in got:
            self.assertRegex(tag, r"^\d+\.\d+\.\d+$")

    def test_excludes_a_versioned_prerelease_that_no_name_rule_would_catch(self):
        # The `prerelease` FLAG must be honoured on its own. `nightly`/`beta` are
        # also dropped by name, so a feed containing only those two cannot prove
        # the flag is respected — deleting the flag check still passes such a
        # test. A release-candidate tag is excluded by the flag and nothing else,
        # so this is the case that actually pins the behaviour.
        feed = [
            {"tag_name": "2.2.0-rc1", "prerelease": True, "draft": False},
            {"tag_name": "2.1.0", "prerelease": False, "draft": False},
        ]
        self.assertEqual(gsv._filter_stable(feed), ["2.1.0"])

    def test_excludes_drafts(self):
        feed = [{"tag_name": "2.2.0", "prerelease": False, "draft": True}] + REAL_FEED
        self.assertEqual(gsv._filter_stable(feed)[0], "2.1.0")

    def test_excludes_the_moving_tags_even_if_not_flagged_prerelease(self):
        # Defence in depth: `nightly`/`beta` are moving tags, so they must be
        # dropped by NAME regardless of how the API flags them. If a future
        # release run publishes them as non-prerelease, the OTA dropdown must
        # still not offer them as a pinnable stable version.
        feed = [
            {"tag_name": "nightly", "prerelease": False, "draft": False},
            {"tag_name": "BETA", "prerelease": False, "draft": False},
            {"tag_name": "2.1.0", "prerelease": False, "draft": False},
        ]
        self.assertEqual(gsv._filter_stable(feed), ["2.1.0"])

    def test_caps_at_max_versions(self):
        self.assertEqual(len(gsv._filter_stable(REAL_FEED)), gsv.MAX_VERSIONS)

    def test_preserves_feed_order_rather_than_sorting(self):
        # The feed is publish-ordered and the generator trusts it. Asserting this
        # explicitly so nobody "improves" it into a lexicographic sort, which
        # would rank 2.0.9 above 2.0.17.
        feed = [
            {"tag_name": "2.0.17", "prerelease": False, "draft": False},
            {"tag_name": "2.0.9", "prerelease": False, "draft": False},
        ]
        self.assertEqual(gsv._filter_stable(feed), ["2.0.17", "2.0.9"])

    def test_skips_blank_and_missing_tag_names(self):
        feed = [
            {"prerelease": False, "draft": False},
            {"tag_name": "", "prerelease": False, "draft": False},
            {"tag_name": "  ", "prerelease": False, "draft": False},
            {"tag_name": " 2.1.0 ", "prerelease": False, "draft": False},
        ]
        self.assertEqual(gsv._filter_stable(feed), ["2.1.0"])

    def test_empty_feed_yields_no_versions(self):
        # main() treats this as "no fresh data" and keeps/falls back rather than
        # emitting an empty C array (which would not compile).
        self.assertEqual(gsv._filter_stable([]), [])


class FormatHeader(unittest.TestCase):
    def header(self, versions=None):
        return gsv._format_header(versions or ["2.1.0", "2.0.17"], "github-api (test)")

    def test_marks_the_file_as_generated_and_not_hand_editable(self):
        # PRO-648 explicitly requires the artifact stay generator-owned.
        self.assertIn("DO NOT EDIT", self.header())
        self.assertIn("scripts/generate_stable_versions.py", self.header())

    def test_emits_a_compilable_array_and_count(self):
        body = self.header()
        self.assertIn("#pragma once", body)
        self.assertIn("#include <cstddef>", body)
        self.assertIn("constexpr const char *const STABLE_VERSIONS[] = {", body)
        self.assertIn('"2.1.0",', body)
        self.assertIn("sizeof(STABLE_VERSIONS) / sizeof(STABLE_VERSIONS[0])", body)

    def test_entry_order_matches_the_input(self):
        entries = re.findall(r'^\s+"(.+)",$', self.header(["2.1.0", "2.0.17", "2.0.16"]), re.M)
        self.assertEqual(entries, ["2.1.0", "2.0.17", "2.0.16"])

    def test_records_the_source_for_debuggability(self):
        self.assertIn("// Source: github-api (test)", self.header())

    def test_escapes_quotes_and_backslashes(self):
        body = gsv._format_header(['we"ird', "back\\slash"], "test")
        self.assertIn('"we\\"ird",', body)
        self.assertIn('"back\\\\slash",', body)

    def test_ends_with_a_single_trailing_newline(self):
        body = self.header()
        self.assertTrue(body.endswith("\n"))
        self.assertFalse(body.endswith("\n\n"))

    def test_real_feed_renders_a_header_led_by_2_1_0(self):
        # End-to-end over the two pure stages: feed -> filter -> header.
        body = gsv._format_header(gsv._filter_stable(REAL_FEED), "github-api (test)")
        entries = re.findall(r'^\s+"(.+)",$', body, re.M)
        self.assertEqual(entries, ["2.1.0", "2.0.17", "2.0.16", "2.0.15", "2.0.14"])
        self.assertNotIn("nightly", body)
        self.assertNotIn("beta", body)


class ImportIsSideEffectFree(unittest.TestCase):
    """PRO-648: importing the generator must not fetch or write anything.

    The script is a PlatformIO `pre:` hook, so it used to call `main()` from a
    bare `else:` on the `Import("env")` NameError guard — meaning ANY import ran
    a live GitHub API request and overwrote src/stable_versions.h. That made the
    module untestable (this file could not import it without network I/O) and
    made `import`-ing it from any other tool a silent mutation of the tree.
    """

    def test_the_platformio_hook_is_gated_on_the_scons_env(self):
        # Running under unittest, SCons never injected `env`, so the guard must
        # be False — and this module already imported cleanly above, which is
        # the actual proof that no fetch/write happened at import time.
        self.assertFalse(gsv._PLATFORMIO_BUILD)

    def test_importing_the_module_writes_no_header(self):
        """The assertion that actually fails if the `elif` regresses to `else:`.

        Checking `_PLATFORMIO_BUILD is False` above is necessary but NOT
        sufficient: with a bare `else:` the flag is still False *and* `main()`
        still runs, so that test alone stays green while the side effect returns.
        This runs the import in a subprocess with its own cwd so the write has
        somewhere harmless to land, and asserts nothing landed.

        Network is blocked via a `sitecustomize` shim so the check is hermetic
        and cannot flake on GitHub rate limits. Blocking it does not weaken the
        test: on failure the generator's offline branch writes the fallback
        header, so a regression still produces a file.
        """
        import subprocess
        import tempfile

        scripts_dir = os.path.dirname(os.path.abspath(__file__))
        with tempfile.TemporaryDirectory() as tmp:
            os.mkdir(os.path.join(tmp, "src"))
            with open(os.path.join(tmp, "sitecustomize.py"), "w", encoding="utf-8") as f:
                f.write(
                    "import urllib.request\n"
                    "def _blocked(*a, **k):\n"
                    "    raise OSError('network blocked by test')\n"
                    "urllib.request.urlopen = _blocked\n"
                )
            env = dict(os.environ)
            env["PYTHONPATH"] = os.pathsep.join([tmp, scripts_dir])
            proc = subprocess.run(
                [sys.executable, "-c", "import generate_stable_versions"],
                cwd=tmp,
                env=env,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
            )
            self.assertEqual(proc.returncode, 0, proc.stdout)
            self.assertFalse(
                os.path.exists(os.path.join(tmp, "src", "stable_versions.h")),
                "importing generate_stable_versions wrote src/stable_versions.h; the "
                "PlatformIO `main()` call must stay gated on _PLATFORMIO_BUILD (PRO-648). "
                "Subprocess output:\n" + proc.stdout,
            )
            # Nor should it have reached out to the network at all.
            self.assertNotIn("GitHub API fetch failed", proc.stdout)
            self.assertNotIn("[stable_versions]", proc.stdout)

    def test_module_exposes_main_without_having_run_it(self):
        self.assertTrue(callable(gsv.main))

    def test_config_still_points_at_the_fork_the_ota_url_uses(self):
        # The slug is duplicated in WebUIPlugin.h's RELEASE_URL; if the fork
        # moves, both change together. Pin it so a silent edit to one is caught.
        self.assertEqual(gsv.OWNER, "carloshrdezc")
        self.assertEqual(gsv.REPO, "gaggimate")
        self.assertEqual(gsv.HEADER_PATH, os.path.join("src", "stable_versions.h"))

    def test_page_size_exceeds_max_versions(self):
        # The feed carries `nightly` + `beta` above the newest stable tag, so a
        # page size equal to MAX_VERSIONS could return fewer than 5 stable tags.
        self.assertGreater(gsv.API_PAGE_SIZE, gsv.MAX_VERSIONS)


if __name__ == "__main__":
    unittest.main(verbosity=2)
