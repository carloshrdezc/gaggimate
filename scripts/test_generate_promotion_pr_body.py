#!/usr/bin/env python3
"""Regression tests for scripts/generate_promotion_pr_body.py (PRO-644).

Same shape as scripts/test_select_tidy_sources.py and
scripts/test_check_ws_api_spec_drift.py: stdlib `unittest`, no third-party
dependencies, run directly.

Everything here is exercised against FABRICATED commit-message strings — no git
repo, no subprocess. That is deliberate: the git-invoking wrappers
(`commit_messages`, `_git`, `main`) are thin and untestable without a fixture
repo, while the parsing is where a promotion body can silently go wrong. A
regex that quietly matches nothing produces a body that *looks* fine (correct
commit count, empty issue list) — which is the same class of silent-nothing
failure the empty bodies in PRO-644 were, one level down. So the fixtures below
include the real subject shapes from the range PR #634 promoted.

Run with no dependencies (there is no pytest in this repo):

    python3 scripts/test_generate_promotion_pr_body.py
"""
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from generate_promotion_pr_body import (  # noqa: E402
    collect_issues,
    issue_key_order,
    issue_refs,
    pr_numbers,
    render_body,
)

# Real subjects/bodies from `origin/master..origin/dev-master` at the time of
# PRO-644 (the range PR #634 promoted with an empty body), trimmed to the lines
# that carry references. Newest first, as `git log` emits them.
REAL_MESSAGES = [
    "fix(web): quiet standby readiness narration + 2-col mobile recipe grid (#636)\n\nFixes PRO-640",
    "fix(web): point dashboard GitHub link to Carlos's fork (#635)\n\nFixes PRO-639",
    "fix(web): key Beanconqueror browser-shot brew UUIDs by storageKey, not id\n\nFixes PRO-638",
    "test(web): make huge-quantity bag-total test observe the addition (PRO-637) (#633)",
    "fix(web): guard collectNonFiniteNumbers against cyclic input (PRO-636) (#632)",
    "fix(web): correct Beanconqueror bean inventory export (PRO-632) (#631)\n\nFixes PRO-632",
    "feat(web): export beans and shots as a Beanconqueror backup (PRO-632) (#630)\n\nRef PRO-428",
    "Merge pull request #629 from carloshrdezc/carlos/pro-635-merge-master-ancestry",
    "chore(release): merge master (2.0.17) into dev-master for tag ancestry (PRO-635)",
    "build(deps-dev): bump jsdom from 29.1.1 to 30.0.1 in /web (#614)",
    "build(deps): bump actions/checkout from 4 to 7 (#599)",
]


class IssueRefs(unittest.TestCase):
    def test_extracts_a_bare_reference_as_a_mention(self):
        self.assertEqual(issue_refs("test(web): observe the addition (PRO-637) (#633)"), {"PRO-637": False})

    def test_closing_keyword_marks_the_reference_as_closing(self):
        for keyword in ("Fixes", "Fixed", "Fix", "Closes", "Closed", "Close", "Resolves", "Resolved", "Resolve"):
            with self.subTest(keyword=keyword):
                self.assertEqual(issue_refs("subject\n\n%s PRO-644" % keyword), {"PRO-644": True})

    def test_closing_keyword_is_case_insensitive_and_accepts_a_colon(self):
        self.assertEqual(issue_refs("subject\n\nfixes: PRO-644"), {"PRO-644": True})
        self.assertEqual(issue_refs("subject\n\nCLOSES PRO-644"), {"PRO-644": True})

    def test_ref_keyword_is_a_mention_not_a_close(self):
        # AGENTS.md: `Ref PRO-123` is explicitly the related-but-not-closed form.
        self.assertEqual(issue_refs("subject\n\nRef PRO-428"), {"PRO-428": False})

    def test_a_chained_closing_list_closes_every_entry(self):
        # `Fixes PRO-1, PRO-2` — only the first ref has the keyword in front of it.
        self.assertEqual(issue_refs("subject\n\nFixes PRO-1, PRO-2 and PRO-3"), {"PRO-1": True, "PRO-2": True, "PRO-3": True})

    def test_a_chain_is_broken_by_intervening_prose(self):
        got = issue_refs("subject\n\nFixes PRO-1. While debugging this we also read PRO-2.")
        self.assertEqual(got, {"PRO-1": True, "PRO-2": False})

    def test_closing_wins_over_a_mention_of_the_same_issue(self):
        # The subject tags `(PRO-632)` (a mention) and the trailer closes it.
        got = issue_refs("fix(web): correct Beanconqueror bean inventory export (PRO-632) (#631)\n\nFixes PRO-632")
        self.assertEqual(got, {"PRO-632": True})

    def test_normalises_zero_padding_to_one_key(self):
        self.assertEqual(issue_refs("subject\n\nFixes PRO-0644 and PRO-644"), {"PRO-644": True})

    def test_lowercase_keys_in_branch_names_are_not_references(self):
        # `Merge pull request #629 from carloshrdezc/carlos/pro-635-merge-master-ancestry`
        # is a real subject in the range PR #634 promoted. The branch name must not
        # be mined for issue keys — see the _ISSUE_RE comment for why.
        self.assertEqual(issue_refs("Merge pull request #629 from carloshrdezc/carlos/pro-644-promotion-pr-body"), {})
        self.assertEqual(issue_refs("Merge pull request #629 from carloshrdezc/carlos/pro-644"), {})
        # The commit's own upper-case reference on the next line still counts.
        got = issue_refs("Merge pull request #629 from carloshrdezc/carlos/pro-644\n\nchore: body generator (PRO-644)")
        self.assertEqual(got, {"PRO-644": False})

    def test_ignores_lookalikes(self):
        # THE thing a sloppy `PRO-\d+` regex gets wrong. `PROFILE-12` and
        # `APRO-3` are not issue keys, and neither is a version-ish `PRO-12a`.
        for text in ("PROFILE-12 was renamed", "see APRO-3", "PRO-12abc", "sPRO-9", "PRO-644-branch"):
            with self.subTest(text=text):
                self.assertEqual(issue_refs(text), {})

    def test_ignores_pr_numbers_and_other_teams(self):
        self.assertEqual(issue_refs("build(deps): bump actions/checkout from 4 to 7 (#599)"), {})
        self.assertEqual(issue_refs("chore: port CAR-341 gates"), {})

    def test_matches_a_reference_at_the_very_start_of_a_message(self):
        # The `(?<!...)` lookbehind must not require a preceding character.
        self.assertEqual(issue_refs("PRO-644 promotion body generator"), {"PRO-644": False})


class CollectIssues(unittest.TestCase):
    def test_splits_closed_from_mentioned_and_attributes_a_subject(self):
        closed, mentioned = collect_issues(
            [
                "fix(web): a thing (#10)\n\nFixes PRO-2",
                "feat(web): another thing (#11)\n\nRef PRO-7",
            ]
        )
        self.assertEqual(closed, [("PRO-2", "fix(web): a thing (#10)")])
        self.assertEqual(mentioned, [("PRO-7", "feat(web): another thing (#11)")])

    def test_an_issue_closed_by_one_commit_is_not_also_listed_as_mentioned(self):
        closed, mentioned = collect_issues(["chore: follow-up for PRO-5", "fix: the actual fix\n\nFixes PRO-5"])
        self.assertEqual(closed, [("PRO-5", "fix: the actual fix")])
        self.assertEqual(mentioned, [])

    def test_deduplicates_across_commits_keeping_the_first_subject(self):
        # `messages` is newest-first, so "first" is the newest commit touching it.
        closed, _ = collect_issues(["newest\n\nFixes PRO-3", "older\n\nFixes PRO-3"])
        self.assertEqual(closed, [("PRO-3", "newest")])

    def test_sorts_numerically_not_lexicographically(self):
        closed, _ = collect_issues(["a\n\nFixes PRO-100", "b\n\nFixes PRO-9", "c\n\nFixes PRO-64"])
        self.assertEqual([k for k, _ in closed], ["PRO-9", "PRO-64", "PRO-100"])

    def test_tolerates_blank_and_reference_free_messages(self):
        self.assertEqual(collect_issues(["", "   ", "build(deps): bump preact (#605)"]), ([], []))

    def test_real_range_is_parsed_as_expected(self):
        closed, mentioned = collect_issues(REAL_MESSAGES)
        self.assertEqual(
            [k for k, _ in closed],
            ["PRO-632", "PRO-638", "PRO-639", "PRO-640"],
        )
        self.assertEqual([k for k, _ in mentioned], ["PRO-428", "PRO-635", "PRO-636", "PRO-637"])

    def test_issue_key_order_is_numeric(self):
        self.assertEqual(sorted(["PRO-100", "PRO-9"], key=issue_key_order), ["PRO-9", "PRO-100"])


class PrNumbers(unittest.TestCase):
    def test_extracts_the_squash_merge_suffix(self):
        self.assertEqual(pr_numbers(["fix(web): a thing (PRO-636) (#632)"]), [632])

    def test_extracts_the_merge_commit_form(self):
        self.assertEqual(pr_numbers(["Merge pull request #629 from carloshrdezc/carlos/pro-635-merge"]), [629])

    def test_ignores_a_pr_number_that_is_not_the_squash_suffix(self):
        # An issue-body reference to another PR is not this commit's PR.
        self.assertEqual(pr_numbers(["fix: something\n\nSupersedes #500"]), [])

    def test_direct_pushes_contribute_no_number(self):
        self.assertEqual(pr_numbers(["chore: bump version to 2.0.18"]), [])

    def test_deduplicates_and_sorts_numerically(self):
        got = pr_numbers(["a (#12)", "b (#3)", "c (#12)", "Merge pull request #100 from x/y"])
        self.assertEqual(got, [3, 12, 100])

    def test_real_range_pr_count(self):
        self.assertEqual(len(pr_numbers(REAL_MESSAGES)), 9)


class RenderBody(unittest.TestCase):
    def body(self, messages=None, base="origin/master", head="origin/dev-master"):
        return render_body(base, head, REAL_MESSAGES if messages is None else messages, "d9aa40a8", "9619d0c4")

    def test_reports_commit_and_pr_counts_and_the_range(self):
        body = self.body()
        self.assertIn("Bundles **11 commits** from **9 merged pull requests**", body)
        self.assertIn("`origin/master..origin/dev-master`", body)
        self.assertIn("`d9aa40a8..9619d0c4`", body)

    def test_lists_closed_issues_with_their_subjects(self):
        body = self.body()
        self.assertIn("### Linear issues closed since the last promotion (4)", body)
        self.assertIn("- PRO-640 — fix(web): quiet standby readiness narration", body)

    def test_lists_mentioned_issues_separately(self):
        body = self.body()
        self.assertIn("### Also referenced (4)", body)
        self.assertIn("- PRO-428 —", body)

    def test_body_is_never_empty_which_is_the_whole_point(self):
        # PRO-644: the failure being fixed is a zero-length body. Guard the
        # degenerate inputs too — a body that renders to "" for an unusual range
        # would reintroduce exactly the reported bug.
        for messages in ([], [""], ["chore: no refs here"]):
            with self.subTest(messages=messages):
                body = self.body(messages)
                self.assertTrue(body.strip())
                self.assertIn("Promoting", body)

    def test_empty_range_says_so_explicitly(self):
        body = self.body([])
        self.assertIn("Nothing to promote", body)
        self.assertNotIn("Bundles", body)

    def test_reference_free_range_says_so_rather_than_emitting_an_empty_heading(self):
        body = self.body(["build(deps): bump preact from 10.29.3 to 10.29.7 (#605)"])
        self.assertIn("No `PRO-NNN` references in this range", body)
        self.assertNotIn("### Linear issues closed", body)

    def test_singular_wording_for_a_single_commit(self):
        body = self.body(["fix: one thing (#1)\n\nFixes PRO-1"])
        self.assertIn("**1 commit** from **1 merged pull request**", body)

    def test_omits_the_pr_count_when_nothing_was_merged_via_a_pr(self):
        body = self.body(["chore: direct push"])
        self.assertIn("**1 commit**", body)
        self.assertNotIn("merged pull request", body)

    def test_emits_no_closing_keywords(self):
        # Critical: these issues were already closed when their own PR merged into
        # dev-master. A `Fixes PRO-NNN` here would make Linear re-close them on the
        # promotion merge, clobbering whatever state they had reached since.
        body = self.body().lower()
        for keyword in ("fixes pro-", "closes pro-", "resolves pro-", "fixed pro-", "closed pro-", "resolved pro-"):
            with self.subTest(keyword=keyword):
                self.assertNotIn(keyword, body)

    def test_names_the_branches_being_promoted(self):
        self.assertIn("## Promoting `dev-master` to `master`", render_body("master", "dev-master", REAL_MESSAGES))

    def test_ends_with_a_single_trailing_newline(self):
        # It is piped into `gh pr create --body-file`; no trailing blank block.
        body = self.body()
        self.assertTrue(body.endswith("\n"))
        self.assertFalse(body.endswith("\n\n"))


if __name__ == "__main__":
    unittest.main(verbosity=2)
