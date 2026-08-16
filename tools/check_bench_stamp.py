#!/usr/bin/env python3
"""Warn when the engine has changed IN SUBSTANCE since docs/BENCHMARKS.md was last stamped.

WHAT IT READS. The first REAL `X.Y.Z` in the file -- the Version cell -- and the last
commit that *wrote that string* (`git log -S`), not the last commit that touched the path.
A host-name rewrite, a typo fix, or any other edit that leaves the stamp alone is not a
re-measure. `make version-check` reads the same cell and compares it to the project version;
this check asks a different question of the same cell.

WHY THIS EXISTS, AND WHY THE CHECK ALREADY IN `make version-check` COULD NOT DO IT
---------------------------------------------------------------------------------
That one compares the VERSION STRING recorded in docs/BENCHMARKS.md against the project
version. It therefore fires only across a release bump, and is structurally blind to the case
that actually happens: a perf change lands between releases, the recorded figures become wrong,
and the version has not moved -- so the check reports clean.

That is not hypothetical. `perf(first-use)` (d7d9485) closed a deficit this file documented as
open, making two of its statements untrue, and `make version-check` said
"bench-stamp = 2026.7.61" with no complaint, because 2026.7.61 was still the current version.

SUBSTANCE, NOT TOUCH
--------------------
A check that fired whenever any file under include/real/ changed would be useless here, and
measurably so: over the fourteen most recent commits touching that tree, eleven were
documentation-only. Warning on all fourteen would train the reader to ignore it, which is the
failure mode this is meant to prevent, not reproduce.

The same applies to the ledger file itself. Binding the stamp to "last commit that touched
docs/BENCHMARKS.md" treated a host-name rewrite as a re-measure and reset the warning. The
pickaxe on the Version cell is the load-bearing form of that distinction.

So a commit counts only if it changed CODE -- the file with comments and whitespace stripped.
Measured against those same fourteen commits: it flags the two real engine changes, stays quiet
on the eleven documentation ones, and has one conservative false positive (a generated struct
reformatted from one line onto four, which is a code-line change even though nothing about the
program differs). Conservative is the right direction for a staleness warning.

IT WARNS, IT DOES NOT FAIL
--------------------------
Benchmarks cannot be re-run per commit, so "the engine moved" is a state to be aware of, not an
error to block on -- the same posture, deliberately, as the version-string check it complements.
It names the offending commits so the warning is actionable rather than ambient.

SHALLOW CLONES
--------------
This reads git history. CI checks out at the default depth of 1, so the history is not there and
the answer would be meaningless. It says so and exits 0 rather than passing silently: a check
that cannot see its subject must announce that, not report clean. That is why this lives in
full-local-gate and not in the CI preflight -- putting it there would require fetching the full
history on every run to serve a warning.

Usage:
    python3 tools/check_bench_stamp.py          # warn (always exits 0)
    python3 tools/check_bench_stamp.py --list   # also list every commit considered
"""
from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

BENCH = "docs/BENCHMARKS.md"
ENGINE = "include/real/"
STAMP_RE = re.compile(r"REAL `([0-9][0-9.]+)`")


def git(*args: str) -> str:
    proc = subprocess.run(["git", *args], capture_output=True, text=True)
    return proc.stdout.strip() if proc.returncode == 0 else ""


def code_only(text: str) -> list[str]:
    """The source with comments, blank lines and whitespace runs removed.

    Deliberately crude and deliberately conservative: it drops comment-only lines and any
    trailing comment, then collapses whitespace. Reformatting real code still reads as a change,
    which is the safe direction -- a missed staleness is worse than an extra warning.
    """
    out: list[str] = []
    for line in text.split("\n"):
        s = line.strip()
        if not s or s.startswith(("//", "/*", "*")) or s == "*/":
            continue
        s = re.sub(r"\s*//!?<?.*$", "", s)
        s = re.sub(r"\s+", " ", s).strip()
        if s:
            out.append(s)
    return out


def stamp_commit() -> tuple[str, str, str]:
    """The commit that last wrote the Version cell's REAL `X.Y.Z`.

    Returns (sha, version, how) where how is "pickaxe" or "touch". The fallback
    exists so a file that has never contained the current stamp string still
    produces a window, and so the warning names that it is the weaker one.
    """
    text = Path(BENCH).read_text(encoding="utf-8")
    m = STAMP_RE.search(text)
    if not m:
        return "", "", ""
    ver = m.group(1)
    needle = f"REAL `{ver}`"
    sha = git("log", "-1", "--format=%H", "-S", needle, "--", BENCH)
    if sha:
        return sha, ver, "pickaxe"
    sha = git("log", "-1", "--format=%H", "--", BENCH)
    return sha, ver, "touch"


def changed_code(commit: str) -> bool:
    """Whether \\p commit changed any engine header in substance."""
    files = [
        f
        for f in git("diff-tree", "--no-commit-id", "--name-only", "-r", commit, "--", ENGINE).split("\n")
        if f.endswith((".hpp", ".h"))
    ]
    for f in files:
        before = git("show", f"{commit}~1:{f}")
        after = git("show", f"{commit}:{f}")
        if code_only(before) != code_only(after):
            return True
    return False


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--list", action="store_true", help="list every commit considered, with its verdict")
    args = ap.parse_args()

    if git("rev-parse", "--is-inside-work-tree") != "true":
        print("check-bench-stamp: not a git work tree — skipped")
        return 0
    if git("rev-parse", "--is-shallow-repository") == "true":
        print(
            "check-bench-stamp: SKIPPED — this is a shallow clone, so the history this reads is absent.\n"
            "  Reporting clean here would be a verdict about nothing. Run it in a full clone."
        )
        return 0

    stamp, ver, how = stamp_commit()
    if not stamp:
        print(f"check-bench-stamp: no stamp in {BENCH} — skipped")
        return 0

    commits = [c for c in git("log", "--format=%h", f"{stamp}..HEAD", "--", ENGINE).split("\n") if c]
    substantive = []
    for c in commits:
        hit = changed_code(c)
        if hit:
            substantive.append(c)
        if args.list:
            subject = git("log", "-1", "--format=%s", c)
            print(f"  {c}  {'CODE' if hit else 'docs':<5} {subject[:66]}")

    label = f"{BENCH} REAL `{ver}`" if ver else BENCH
    if how == "touch":
        print(
            f"check-bench-stamp: note — no pickaxe hit for REAL `{ver}`; "
            f"falling back to last touch of {BENCH}"
        )
    if not commits:
        print(f"check-bench-stamp: clean — no engine commit since {label} was stamped")
        return 0
    if not substantive:
        print(
            f"check-bench-stamp: clean — {len(commits)} engine commit(s) since {label} was stamped, "
            "none of them changed code"
        )
        return 0

    print(
        f"check-bench-stamp: WARN — {len(substantive)} of {len(commits)} engine commit(s) since {label} "
        "was stamped changed CODE, so its figures may no longer describe this tree:"
    )
    for c in substantive:
        print(f"    {c}  {git('log', '-1', '--format=%s', c)[:70]}")
    print(
        "  Re-measure and re-stamp before a release, or proceed knowingly. Not an error: benchmarks "
        "cannot be re-run per commit."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
