#!/usr/bin/env python3
"""Break one exact thing, run one exact check, and put it back — with the guards that cost a day.

WHY THIS EXISTS AS A FILE. A green proves nothing until the guard has been seen to go red on the
property it claims to hold, so every substantive change here comes with a sabotage. That sabotage
was rewritten from scratch in each session, and it reacquired the same three defects each time.
Each one produced a wrong conclusion before being noticed:

  1. NO REBUILD AFTER REVERT. The source went back, the built artifact did not, so the next check
     ran against the sabotage. That was diagnosed as an engine bug in code that was already
     correct, and only a hand comparison found the truth.
  2. REBUILD ONLY FOR .cpp. Sabotaging a header left the extension module untouched, so every
     verdict measured unmodified code and every sabotage came back GREEN -- the failure mode a
     sabotage exists to detect, in the tool meant to detect it.
  3. NO UNIQUENESS CHECK ON THE REPLACEMENT. The revert is anchored on the replacement text; when
     that text already occurred elsewhere, the revert refused and left the tree sabotaged.

WHAT A GREEN MEANS HERE. Not "the code is fine": it means the sabotage did not reach the property,
which is a statement about the SABOTAGE first. Three times in one session a green turned out to be
a witness that never reached the site it claimed to pin -- a pattern stopped by an earlier check, a
count that stayed equal because one divergence became another, a rebuild that never happened. Read
a green as "find a better witness", not as "no guard needed".

WHAT IT WILL NOT DO. It refuses to run on a dirty tree for the file it is about to edit: a revert
restores what this script wrote, not what was already there unsaved, and `git stash` is not a way
out (a stash that saved nothing followed by a pop dequeues ANOTHER session's work -- that happened,
on 117 lines across three engine headers).

USAGE
  tools/sabotage.py --file <path> --label <words> --old <text> --new <text> -- <check...>

  <text> may be multi-line; pass it as one shell argument. The check command runs from the repo
  root; its exit status is the verdict. Anything under include/ or bindings/*/src rebuilds the
  Python extension first, since that is what the checks import.

EXAMPLE
  tools/sabotage.py --file include/real/frontend/ast.hpp --label "position at cursor" \\
      --old 'throw regex_error(message, question_pos);' \\
      --new 'throw regex_error(message, pos_);' \\
      -- make test
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

#: Editing any of these changes what the built artifacts contain, so they must be rebuilt before a
#: check can mean anything. Headers are included deliberately: this engine is header-only, so a
#: .hpp edit is a code edit, and treating only .cpp as one is defect 2 above.
REBUILD_PREFIXES = ("include/", "bindings/python/src/", "bindings/c/")


def git(*args: str) -> str:
    return subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True,
                          check=False).stdout


def needs_rebuild(rel: str) -> bool:
    return rel.endswith((".hpp", ".cpp", ".h")) and rel.startswith(REBUILD_PREFIXES)


def rebuild() -> bool:
    done = subprocess.run(["make", "python-build"], cwd=ROOT, capture_output=True, text=True)
    if done.returncode != 0:
        print("  sabotage: the rebuild FAILED, so the verdict below would be meaningless:")
        print("\n".join("    " + line for line in done.stderr.strip().split("\n")[-4:]))
        return False
    return True


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(add_help=True, description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--file", required=True, help="path, relative to the repo root")
    ap.add_argument("--label", required=True, help="what this sabotage claims to break, in a few words")
    ap.add_argument("--old", required=True, help="exact text to replace (must occur EXACTLY once)")
    ap.add_argument("--new", required=True, help="replacement (must occur ZERO times before)")
    ap.add_argument("check", nargs=argparse.REMAINDER,
                    help="-- followed by the command whose exit status is the verdict")
    args = ap.parse_args(argv)

    check = [a for a in args.check if a != "--"]
    if not check:
        ap.error("no check command given (put it after --)")

    src = ROOT / args.file
    if not src.is_file():
        print(f"  sabotage: {args.file} is not a file")
        return 2

    rel = str(Path(args.file))
    if git("status", "--porcelain", "--", rel).strip():
        print(f"  sabotage: REFUSING -- {rel} has uncommitted changes.")
        print("  The revert below restores what this script wrote, not what you have unsaved.")
        print("  Commit first. Do NOT reach for `git stash`: a stash that saved nothing followed")
        print("  by a pop dequeues another session's work, which has happened here.")
        return 2

    text = src.read_text()
    if text.count(args.old) != 1:
        print(f"  sabotage: INVALID [{args.label}] -- the anchor occurs "
              f"{text.count(args.old)} times, it must occur exactly once.")
        return 2
    if args.new in text:
        print(f"  sabotage: INVALID [{args.label}] -- the REPLACEMENT already occurs in the file.")
        print("  The revert is anchored on it, so it would refuse and leave the tree sabotaged.")
        return 2

    src.write_text(text.replace(args.old, args.new, 1))
    verdict = 3
    try:
        if needs_rebuild(rel) and not rebuild():
            return 2
        done = subprocess.run(check, cwd=ROOT, capture_output=True, text=True)
        bit = done.returncode != 0
        print(f"  [{args.label}] " + ("RED — the guard reacted." if bit else
                                      "GREEN — NOTHING REACTED."))
        if not bit:
            print("    A green is a statement about the SABOTAGE first: it did not reach the")
            print("    property. Find a witness that does before concluding the guard is missing.")
        for line in (done.stdout + done.stderr).split("\n"):
            if any(k in line for k in ("checks failed", "FAILED", "AssertionError", "Error ")):
                print(f"    {line.strip()[:110]}")
                break
        verdict = 0 if bit else 1
    finally:
        back = src.read_text()
        if back.count(args.new) == 1:
            src.write_text(back.replace(args.new, args.old, 1))
        else:
            # No `return` here: a return inside finally swallows an exception in flight, and this
            # branch is exactly where one is most likely (the check crashed mid-edit). Print and let
            # the outer status stand -- the message is what the reader needs.
            print(f"  sabotage: CANNOT REVERT -- the replacement now occurs "
                  f"{back.count(args.new)} times in {rel}. Restore it by hand:")
            print(f"    git checkout -- {rel}")
            verdict = 2
        if needs_rebuild(rel):
            rebuild()  # the artifact must not outlive the sabotage; this is defect 1
        dirty = git("status", "--porcelain", "--", rel).strip()
        print(f"    reverted and rebuilt — {rel}: {'CLEAN' if not dirty else 'STILL DIRTY: ' + dirty}")
    return verdict


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
