#!/usr/bin/env python3
r"""Generate the PR body for a `dev-master` -> `master` promotion PR (PRO-644).

Promotion PRs bundle everything that has landed on `dev-master` since the last
promotion (PR #634 carried 39 commits) and were being opened by hand with a
COMPLETELY EMPTY body, so the only way to see what was being shipped to
`master` was to expand the commit list. The reviewer bot flagged it on every
review pass of #634 and it had recurred across several promotions before that.

This script is the convention that stops it recurring: it derives the body
mechanically from `git log <base>..<head>`, so there is nothing to remember
beyond piping it into `gh pr create --body-file`.

WHAT IT EMITS
    * how much is being promoted — commit count plus the number of distinct
      merged PRs referenced by the commit subjects (`... (#630)` squash-merge
      suffixes and `Merge pull request #629 ...` subjects);
    * the Linear issues touched since the last promotion, split into the ones
      whose commits used a closing keyword (`Fixes`/`Closes`/`Resolves`) and
      the ones only mentioned (`Ref PRO-123`), each with the first commit
      subject that referenced it;
    * the commit range, so the body still says something useful once the
      branches have moved on.

Deliberately NO Linear magic words in the output. The issues in a promotion
range were already closed when their own PR merged into `dev-master`; repeating
`Fixes PRO-123` here would make Linear re-close them on the promotion merge and
overwrite whatever state they had reached in the meantime. The list is
informational — plain `PRO-123` keys, which Linear still auto-links.

Usage:
    # the normal case: everything on origin/dev-master not yet on origin/master
    python3 scripts/generate_promotion_pr_body.py --fetch > /tmp/promo-body.md
    gh pr create --base master --head dev-master --body-file /tmp/promo-body.md \
        --title "chore(release): promote dev-master to master"

    python3 scripts/generate_promotion_pr_body.py --fetch          # git fetch origin first
    python3 scripts/generate_promotion_pr_body.py --base master --head dev-master   # local refs

No third-party dependencies (there are none in scripts/); `git` on PATH is the
only requirement, and only for the wrapper — the parsing helpers are pure
functions over commit-message strings so they can be unit-tested without a repo
(see scripts/test_generate_promotion_pr_body.py).
"""
import argparse
import os
import re
import subprocess
import sys

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

DEFAULT_BASE = "origin/master"
DEFAULT_HEAD = "origin/dev-master"

# Linear issue keys for the coding team. Anchored on both sides so `PROFILE-12`,
# `APRO-3` and `PRO-12abc` are not issue references, while the shapes that
# actually occur in commit subjects — `(PRO-632)`, `PRO-635,`, `PRO-610.` — are.
#
# CASE-SENSITIVE on purpose. Issue keys are written upper-case everywhere
# (AGENTS.md's magic words, every commit in the repo), whereas the LOWER-case
# form appears in branch names, which land verbatim in merge subjects:
# `Merge pull request #629 from carloshrdezc/carlos/pro-635-merge-master-ancestry`.
# A case-insensitive match would harvest issue keys out of branch names — usually
# the same issue the commit already references, but not reliably (a branch can be
# named after a different issue than the one its commits close), and always as a
# *mention* rather than a close, which is the wrong bucket. The trailing
# `(?!...-)` already rejects `pro-635-merge`, but a branch name ending at the key
# would still slip through, so the case rule is the real guard.
_ISSUE_RE = re.compile(r"(?<![0-9A-Za-z])PRO-(\d+)(?![0-9A-Za-z-])")

# A closing keyword immediately before a reference. GitHub/Linear accept the
# `Fixes:`/`Fixed` variants too, so match the family rather than three literals.
_CLOSING_RE = re.compile(r"(?:fix(?:e[sd])?|close[sd]?|resolve[sd]?)\b[\s:]*\Z", re.IGNORECASE)

# Separator between two references that share one closing keyword, i.e. the
# `, ` in `Fixes PRO-1, PRO-2` — both are closed, but only the first one has the
# keyword in front of it.
_CHAIN_RE = re.compile(r"\A[\s,;/&]*(?:and[\s]*)?\Z", re.IGNORECASE)

# `feat(web): ... (#630)` — the PR number GitHub appends when squash-merging.
_SQUASH_PR_RE = re.compile(r"\(#(\d+)\)\s*\Z")

# `Merge pull request #629 from carloshrdezc/...` — the non-squash form.
_MERGE_PR_RE = re.compile(r"\AMerge pull request #(\d+)\b")

# Separator between commits, so a multi-line commit body survives the round trip
# through `git log --format=%B` (a blank line would not: bodies contain those).
# ASCII RS (0x1E), written as git's `%x1e` escape rather than a literal: argv
# strings are NUL-terminated but otherwise byte-transparent, and RS is expanded
# by git into the output where a raw one is vanishingly unlikely to occur in a
# commit message. NOT `%x00` — python rejects an embedded NUL in argv.
_LOG_SEP = "\x1e"
_LOG_SEP_FMT = "%x1e"


def issue_refs(message: str) -> dict:
    """Map every `PRO-NNN` in `message` to whether it was *closed* by it.

    Value is True when a closing keyword governs the reference, False for a bare
    mention (`Ref PRO-123`, `see PRO-123`, a subject-line `(PRO-123)` tag). A key
    that appears both ways in the same message counts as closing: the closing
    statement is the stronger claim.

    Only the upper-case `PRO-NNN` form counts (see `_ISSUE_RE`); keys are
    normalised through `int()` so a zero-padded `PRO-0644` cannot produce a
    second entry alongside `PRO-644`.
    """
    found = {}
    chain_end = None  # end offset of the last closing ref, for `Fixes A, B`
    for match in _ISSUE_RE.finditer(message):
        key = "PRO-%d" % int(match.group(1))
        closing = bool(_CLOSING_RE.search(message[: match.start()]))
        if not closing and chain_end is not None:
            closing = bool(_CHAIN_RE.match(message[chain_end : match.start()]))
        chain_end = match.end() if closing else None
        found[key] = found.get(key, False) or closing
    return found


def issue_key_order(key: str) -> int:
    """Sort key for `PRO-NNN`, numeric so PRO-9 precedes PRO-100."""
    return int(key.split("-", 1)[1])


def collect_issues(messages) -> tuple:
    """Split the issues referenced by `messages` into (closed, mentioned).

    Both lists are `(key, subject)` pairs sorted by issue number, where
    `subject` is the first line of the first commit that referenced the issue —
    enough to read the body without cross-checking the commit list. An issue
    closed by one commit and merely mentioned by another appears only in
    `closed`, so the two lists never overlap.
    """
    closing = {}
    mentions = {}
    for message in messages:
        subject = message.strip().splitlines()[0].strip() if message.strip() else ""
        for key, is_closing in issue_refs(message).items():
            bucket = closing if is_closing else mentions
            bucket.setdefault(key, subject)
    mentioned = {k: v for k, v in mentions.items() if k not in closing}
    return (
        [(k, closing[k]) for k in sorted(closing, key=issue_key_order)],
        [(k, mentioned[k]) for k in sorted(mentioned, key=issue_key_order)],
    )


def pr_numbers(messages) -> list:
    """Return the sorted, de-duplicated merged-PR numbers in `messages`.

    Both merge shapes this repo produces are recognised: the `(#630)` suffix
    GitHub adds when squash-merging, and `Merge pull request #629 from ...`.
    Commits pushed straight to `dev-master` contribute no number, which is why
    the body reports the commit count as the primary figure and the PR count as
    a secondary one.
    """
    numbers = set()
    for message in messages:
        if not message.strip():
            continue
        subject = message.strip().splitlines()[0].strip()
        for pattern in (_SQUASH_PR_RE, _MERGE_PR_RE):
            match = pattern.search(subject)
            if match:
                numbers.add(int(match.group(1)))
                break
    return sorted(numbers)


def _plural(count: int, word: str) -> str:
    return "%d %s%s" % (count, word, "" if count == 1 else "s")


def render_body(base: str, head: str, messages, base_sha: str = "", head_sha: str = "") -> str:
    """Render the Markdown promotion-PR body for `messages` (newest first)."""
    commits = len(messages)
    range_ref = "`%s..%s`" % (base, head)
    if base_sha and head_sha:
        range_ref += " (`%s..%s`)" % (base_sha, head_sha)

    lines = ["## Promoting `%s` to `%s`" % (head, base), ""]
    if not commits:
        lines += [
            "Nothing to promote: %s is empty — `%s` already contains every commit on `%s`." % (range_ref, base, head),
            "",
        ]
        return "\n".join(lines)

    prs = pr_numbers(messages)
    summary = "Bundles **%s**" % _plural(commits, "commit")
    if prs:
        summary += " from **%s**" % _plural(len(prs), "merged pull request")
    lines += ["%s. Range: %s" % (summary, range_ref), ""]

    closed, mentioned = collect_issues(messages)
    if closed:
        lines += ["### Linear issues closed since the last promotion (%d)" % len(closed), ""]
        lines += ["- %s — %s" % (key, subject) for key, subject in closed]
        lines.append("")
    if mentioned:
        lines += ["### Also referenced (%d)" % len(mentioned), ""]
        lines += ["- %s — %s" % (key, subject) for key, subject in mentioned]
        lines.append("")
    if not closed and not mentioned:
        lines += ["No `PRO-NNN` references in this range (dependency bumps and similar).", ""]

    lines += [
        "<sub>Generated by `scripts/generate_promotion_pr_body.py` (PRO-644). Issue keys are "
        "informational — they were already closed by their own PR merging into "
        "`%s`, so no closing keywords here.</sub>" % head,
        "",
    ]
    return "\n".join(lines)


def _git(*args) -> str:
    """Run a read-only git command in the project root and return its stdout."""
    result = subprocess.run(
        ("git",) + args,
        cwd=PROJECT_ROOT,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if result.returncode != 0:
        detail = result.stderr.strip() or "exit status %d" % result.returncode
        raise RuntimeError("git %s failed: %s" % (" ".join(args), detail))
    return result.stdout


def verify_ref(ref: str) -> None:
    """Raise RuntimeError with an actionable message if `ref` is not a commit."""
    try:
        _git("rev-parse", "--verify", "--quiet", ref + "^{commit}")
    except RuntimeError:
        # `--quiet` means git says nothing, so the generic wrapper message would
        # be a bare "exit status 1". Name the ref instead: in a fresh clone the
        # cause is almost always an unfetched remote branch.
        raise RuntimeError("unknown ref: %s" % ref) from None


def commit_messages(base: str, head: str) -> list:
    """Full commit messages for `base..head`, newest first."""
    out = _git("log", "--format=%B" + _LOG_SEP_FMT, "%s..%s" % (base, head))
    return [chunk.strip() for chunk in out.split(_LOG_SEP) if chunk.strip()]


def short_sha(ref: str) -> str:
    try:
        return _git("rev-parse", "--short", ref).strip()
    except RuntimeError:
        return ""


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description="Generate a dev-master -> master promotion PR body.")
    parser.add_argument("--base", default=DEFAULT_BASE, help="promotion target (default: %s)" % DEFAULT_BASE)
    parser.add_argument("--head", default=DEFAULT_HEAD, help="branch being promoted (default: %s)" % DEFAULT_HEAD)
    parser.add_argument("--fetch", action="store_true", help="run `git fetch origin` first so the remote refs are current")
    args = parser.parse_args(argv)

    if args.base == args.head:
        print("--base and --head must differ (got %r)" % args.base, file=sys.stderr)
        return 2

    try:
        if args.fetch:
            _git("fetch", "origin")
        for ref in (args.base, args.head):
            verify_ref(ref)
        messages = commit_messages(args.base, args.head)
    except RuntimeError as exc:
        # Overwhelmingly this is a stale/missing remote ref in a fresh clone.
        print("%s\nhint: `git fetch origin`, or pass local refs via --base/--head." % exc, file=sys.stderr)
        return 1

    sys.stdout.write(render_body(args.base, args.head, messages, short_sha(args.base), short_sha(args.head)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
