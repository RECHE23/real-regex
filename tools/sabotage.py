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
  4. FINALLY DOES NOT RUN ON SIGTERM. The revert lives in `finally`, which covers a crashing check
     (an exception) and not a killed run (a signal). SIGTERM ends the process without unwinding,
     so a harness timeout that used to fire between apply and revert left the file sabotaged — and
     the next gate would have judged the sabotage, not the code. SIGINT and SIGTERM now raise so
     that `finally` runs. SIGKILL is uncatchable and is not claimed.

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
  tools/sabotage.py --self-test

  <text> may be multi-line; pass it as one shell argument. The check command runs from the repo
  root; its exit status is the verdict. Anything under include/ or bindings/*/src rebuilds the
  Python extension first, since that is what the checks import. `--self-test` SIGTERMs a run in
  flight and requires the canary file to come back; a handler nobody has seen fire is defect 4
  all over again.

EXAMPLE
  tools/sabotage.py --file include/real/frontend/ast.hpp --label "position at cursor" \\
      --old 'throw regex_error(message, question_pos);' \\
      --new 'throw regex_error(message, pos_);' \\
      -- make test
"""

from __future__ import annotations

import argparse
import os
import signal
import subprocess
import sys
import time
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


def _raise_interrupt(signum: int, _frame: object) -> None:
    # KeyboardInterrupt is BaseException: a bare `except Exception` in a check cannot swallow it,
    # and `finally` still runs. The default SIGTERM action does not unwind, which is defect 4.
    raise KeyboardInterrupt(f"sabotage: signal {signum}")


def install_interrupt_handlers() -> None:
    """SIGINT and SIGTERM raise so the revert `finally` runs. SIGKILL is not claimed."""
    for name in ("SIGINT", "SIGTERM"):
        sig = getattr(signal, name, None)
        if sig is not None:
            signal.signal(sig, _raise_interrupt)


CANARY = "tools/sabotage_canary.txt"
CANARY_OLD = "SABOTAGE_CANARY_INTACT"
CANARY_NEW = "SABOTAGE_CANARY_SABOTAGED"


def self_test() -> int:
    """SIGTERM a run in flight; the canary must come back. SIGKILL is uncatchable and untested."""
    if os.name == "nt":
        print("sabotage --self-test: skipped (no Unix SIGTERM)")
        return 0
    path = ROOT / CANARY
    original = path.read_text()
    if original.count(CANARY_OLD) != 1 or CANARY_NEW in original:
        print("sabotage --self-test: canary is not in the expected state; restore it:")
        print(f"    git checkout -- {CANARY}")
        return 2
    proc = subprocess.Popen(
        [sys.executable, str(ROOT / "tools" / "sabotage.py"),
         "--file", CANARY, "--label", "signal-revert",
         "--old", CANARY_OLD, "--new", CANARY_NEW,
         "--", sys.executable, "-c", "import time; time.sleep(60)"],
        cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
    )
    try:
        deadline = time.monotonic() + 5.0
        while time.monotonic() < deadline:
            if CANARY_NEW in path.read_text():
                break
            if proc.poll() is not None:
                out, _ = proc.communicate()
                print("sabotage --self-test: child exited before applying the sabotage:")
                print(out)
                return 2
            time.sleep(0.02)
        else:
            proc.kill()
            proc.wait(timeout=2)
            print("sabotage --self-test: sabotage never applied")
            return 2
        os.kill(proc.pid, signal.SIGTERM)
        try:
            out, _ = proc.communicate(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=2)
            print("sabotage --self-test: did not exit after SIGTERM")
            if path.read_text() != original:
                path.write_text(original)
            return 2
        got = path.read_text()
        if got != original:
            print("sabotage --self-test: file NOT restored after SIGTERM")
            path.write_text(original)
            print(out)
            return 2
        print("sabotage --self-test: SIGTERM restored the canary")
        return 0
    except Exception:
        if path.exists() and path.read_text() != original:
            path.write_text(original)
        raise


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(add_help=True, description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--self-test", action="store_true",
                    help="SIGTERM a run in flight and require the canary file to come back")
    ap.add_argument("--file", help="path, relative to the repo root")
    ap.add_argument("--label", help="what this sabotage claims to break, in a few words")
    ap.add_argument("--old", help="exact text to replace (must occur EXACTLY once)")
    ap.add_argument("--new", help="replacement (must occur ZERO times before)")
    ap.add_argument("check", nargs=argparse.REMAINDER,
                    help="-- followed by the command whose exit status is the verdict")
    args = ap.parse_args(argv)
    if args.self_test:
        return self_test()
    if not args.file or not args.label or args.old is None or args.new is None:
        ap.error("--file, --label, --old and --new are required (or pass --self-test)")

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

    install_interrupt_handlers()
    src.write_text(text.replace(args.old, args.new, 1))
    verdict = 3
    child: subprocess.Popen[str] | None = None
    try:
        if needs_rebuild(rel) and not rebuild():
            return 2
        # errors="replace", not the default strict: a check's output is a BYTE stream, and a test
        # suite that prints a failing pattern prints whatever bytes that pattern holds. A byte-mode
        # witness (`\<C3>`) is not valid UTF-8, so strict decoding raised inside communicate() --
        # AFTER the check had run and BEFORE its exit status was read, which discards the verdict
        # and reports a harness traceback in its place. The one thing this script exists to deliver
        # is that verdict; it must not be lost to the contents of the output.
        child = subprocess.Popen(check, cwd=ROOT, stdout=subprocess.PIPE,
                                 stderr=subprocess.STDOUT, text=True, errors="replace")
        out, _ = child.communicate()
        bit = child.returncode != 0
        print(f"  [{args.label}] " + ("RED — the guard reacted." if bit else
                                      "GREEN — NOTHING REACTED."))
        if not bit:
            print("    A green is a statement about the SABOTAGE first: it did not reach the")
            print("    property. Find a witness that does before concluding the guard is missing.")
        for line in out.split("\n"):
            if any(k in line for k in ("checks failed", "FAILED", "AssertionError", "Error ")):
                print(f"    {line.strip()[:110]}")
                break
        verdict = 0 if bit else 1
    except KeyboardInterrupt:
        print("  sabotage: interrupted — reverting")
        if child is not None and child.poll() is None:
            child.terminate()
            try:
                child.wait(timeout=2)
            except subprocess.TimeoutExpired:
                child.kill()
        verdict = 2
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
