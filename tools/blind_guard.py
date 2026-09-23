#!/usr/bin/env python3
"""Blind every condition of a guard, one at a time, and say which ones its self-test does not see.

A guard's ``--self-test`` passing says nothing about how many of its arms it drives. The proof is
to blind each condition in turn -- ``if X:`` becomes ``if False and (X):`` -- and re-run the
self-test: a blinding that stays GREEN is an arm no case reaches, or a condition that can never
decide a verdict. This tool does that for every ``if`` / ``elif`` in a Python guard, and classifies
each run on the four channels a verdict needs, because each of them has lied once:

  * the EXIT STATUS of the self-test, never a line read from its output;
  * a TRACEBACK, which is a crash and not a refusal -- a removed ``is None`` guard reddens by crashing,
    and a classifier that only looked for "FAILED" once labelled three crashes green;
  * whether the edit CHANGED the file at all -- a BSD ``sed`` alternation silently replaced nothing,
    and eleven "green" blindings were a file left untouched;
  * whether the blinded copy still PARSES -- a throwaway version of this tool pasted a trailing comment
    inside the parenthesis it opened, the copy died on a SyntaxError (no "Traceback" in its output), and
    the run was read as RED. It is its own verdict here, UNPARSABLE, never a pass or a refusal.
    (This tool writes the blinded condition itself, so no guard in this tree reaches that verdict; it
    stays as the net for a condition shape the pattern below does not foresee.)

A blinding is only evidence against a GREEN baseline: if the unblinded self-test already fails, every
blinding "reddens" and the sweep reports a guard fully proven while it proved nothing -- which this tool
once did. So the unblinded self-test runs first, and a red baseline stops the sweep.

The blinded copy is written BESIDE the guard, so its sibling imports and repository-relative paths
resolve exactly as the original's do, and it is removed afterwards.

Usage:
    python3 tools/blind_guard.py tools/check_layers.py            # every condition above the test section
    python3 tools/blind_guard.py tools/check_layers.py --until 'def main'
    python3 tools/blind_guard.py --self-test

By default the scan stops at the test section, whichever of its conventional first lines comes first:
the arm-marker table (``_ARMS``/``_GAPS``), a ``_self_test*`` helper, or ``self_test`` itself. The
conditions of the self-test are the instrument, not the guard.

Exit status is 1 when any condition stays GREEN or CRASHES, 0 when every one reddens by refusal.
"""
from __future__ import annotations

import argparse
import ast
import contextlib
import io
import pathlib
import re
import subprocess
import sys
import tempfile

CONDITION = re.compile(r"^(\s*)(if|elif) (.*):\s*(#.*)?$")


TEST_SECTION = ("_ARMS = ", "_GAPS = ", "def _self_test", "def self_test")


def blind(guard: pathlib.Path, until: str | tuple[str, ...] = TEST_SECTION) -> list[tuple[str, int, bool, str]]:
    """(verdict, line, changed, text) for every condition above the first line starting with ``until``."""
    lines = guard.read_text(encoding="utf-8").split("\n")
    marks = (until,) if isinstance(until, str) else until
    end = next((i for i, line in enumerate(lines) if line.startswith(marks)), len(lines))
    results = []
    for idx in range(end):
        m = CONDITION.match(lines[idx])
        if not m:
            continue
        blinded = list(lines)
        blinded[idx] = f"{m.group(1)}{m.group(2)} False and ({m.group(3)}):"
        changed = blinded != lines
        try:
            ast.parse("\n".join(blinded))
        except SyntaxError:
            results.append(("UNPARSABLE", idx + 1, changed, lines[idx].strip()))
            continue
        copy = guard.with_name(f"_blinded_{guard.name}")
        try:
            copy.write_text("\n".join(blinded), encoding="utf-8")
            run = subprocess.run([sys.executable, str(copy), "--self-test"], capture_output=True, text=True,
                                 cwd=guard.parent.parent)
        finally:
            copy.unlink(missing_ok=True)
        if not changed:
            verdict = "UNCHANGED"
        elif "Traceback" in run.stderr:
            verdict = "CRASH"
        elif run.returncode != 0:
            verdict = "RED"
        else:
            verdict = "GREEN"
        results.append((verdict, idx + 1, changed, lines[idx].strip()))
    return results


def baseline(guard: pathlib.Path) -> subprocess.CompletedProcess:
    """The unblinded self-test, run the way every blinding is."""
    return subprocess.run([sys.executable, str(guard), "--self-test"], capture_output=True, text=True,
                          cwd=guard.parent.parent)


def report(guard: pathlib.Path, until: str | tuple[str, ...] = TEST_SECTION) -> int:
    base = baseline(guard)
    if base.returncode != 0:
        print(f"blind_guard: {guard.name}'s own --self-test is RED before any blinding (exit {base.returncode}) -- "
              "every blinding would read as a refusal, so nothing is judged. Make it green first.")
        return 1
    results = blind(guard, until)
    if not results:
        print(f"blind_guard: no condition found in {guard} above {until!r}")
        return 1
    for verdict, line, _changed, text in results:
        if verdict != "RED":
            print(f"  {verdict:9} {guard.name}:{line}  {text[:90]}")
    bad = [r for r in results if r[0] != "RED"]
    if bad:
        print(f"blind_guard: {len(bad)} of {len(results)} condition(s) in {guard.name} are not proven by its "
              "self-test -- a GREEN is a case missing or a dead condition; a CRASH is caught by traceback, "
              "not by refusal; UNPARSABLE means the blinding itself broke the file")
        return 1
    print(f"blind_guard: all {len(results)} condition(s) in {guard.name} redden by refusal when blinded")
    return 0


def self_test() -> int:
    """A two-arm guard whose self-test drives one arm: the driven one must be RED, the other GREEN, and a
    third arm that raises when blinded must be a CRASH."""
    guard_text = '''import sys
def judge(x):
    if x < 0:
        return 1
    if x > 100:
        return 2
    return 0
def pick(d):
    if d is None:
        return 0
    return d["k"]
def self_test():
    ok = judge(-1) == 1 and judge(5) == 0 and pick(None) == 0
    return 0 if ok else 1
if __name__ == "__main__":
    sys.exit(self_test() if sys.argv[1:] == ["--self-test"] else 0)
'''
    with tempfile.TemporaryDirectory() as tmp:
        guard = pathlib.Path(tmp) / "tools" / "g.py"
        guard.parent.mkdir()
        guard.write_text(guard_text, encoding="utf-8")
        got = {text: verdict for verdict, _line, _changed, text in blind(guard, "def self_test")}
        broken = pathlib.Path(tmp) / "tools" / "red.py"
        broken.write_text(guard_text.replace("return 0 if ok else 1", "return 1"), encoding="utf-8")
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            refused = report(broken)
    want = {"if x < 0:": "RED", "if x > 100:": "GREEN", "if d is None:": "CRASH"}
    if got != want:
        print(f"blind_guard: self-test FAILED -- got {got}, want {want}")
        return 1
    if refused != 1 or "RED before any blinding" not in out.getvalue():
        print(f"blind_guard: self-test FAILED -- a guard whose self-test is already red was not refused: "
              f"{out.getvalue().strip()!r}")
        return 1
    print("blind_guard: self-test OK — a driven arm reddens, an undriven one stays green, a removed None "
          "guard is told apart as a crash, and a guard whose self-test is already red is refused")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("guard", nargs="?", type=pathlib.Path)
    ap.add_argument("--until", default=None,
                    help="stop at the first line starting with this (default: the test section)")
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()
    if args.self_test:
        return self_test()
    if args.guard is None:
        ap.error("a guard path is required")
    return report(args.guard, args.until or TEST_SECTION)


if __name__ == "__main__":
    sys.exit(main())
