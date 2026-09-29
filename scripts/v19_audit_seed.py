"""Reviewer audit replay: random.seed(29092026), 25 CSV rows. A row is an error if any feature
derived from its actual `git show --name-only` paths is neither its primary nor a secondary, or if
its primary is not among those path features without a documented resolution."""
import csv, random, subprocess, sys
sys.path.insert(0, "scripts")
import v19_commit_feature_map as m
rows = list(csv.DictReader(open("docs/upstream-v1.9-preservation-matrix.csv")))
random.seed(29092026)
errs = 0
for r in random.sample(rows, 25):
    sha = r["sha"]
    merge = r["merge"] == "1"
    cmd = ["git", "diff", "--name-only", sha + "^1", sha] if merge else ["git", "show", "--name-only", "--format=", sha]
    paths = [p for p in subprocess.run(cmd, capture_output=True, text=True).stdout.split() if p]
    pf = {m.path_feature(p) for p in paths} - {m.NEUTRAL, None}
    owners = {r["feature"], *r["secondary"].split()}
    sync = r["matched_by"] == "sync-merge"
    miss = set() if sync else pf - owners
    bad_pri = (not sync and pf and r["feature"] not in pf
               and r["matched_by"] not in ("override", "mismatch-override", "crosscut-subject"))
    err = bool(miss or bad_pri)
    errs += err
    print(("ERR " if err else "ok  ") + sha[:8], r["feature"].split("-")[0], r["matched_by"],
          "sec=" + (r["secondary"].replace("-", "")[:40] or "-"), "|", r["subject"][:60])
print("errors", errs, "/ 25")
