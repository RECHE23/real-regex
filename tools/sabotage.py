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
  root; its exit status is the verdict. The ARTIFACT a file feeds is rebuilt before the check and
  again after the revert -- see ARTIFACTS below; headers rebuild the extension module (this engine
  is header-only, so a .hpp edit is a code edit), and `fuzz/exhaustive_compat.cpp` rebuilds its own
  runner. A file with no registered artifact says so instead of rebuilding something else.
  `--self-test` SIGTERMs a run in flight and requires the canary file to come back; a handler
  nobody has seen fire is defect 4 all over again.

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

#: Which artifact a sabotaged file feeds, and the command that rebuilds THAT artifact. Longest
#: prefix wins, so a specific file can override the directory it lives in.
#:
#: This used to be a set of prefixes and ONE recipe (`make python-build`), and the pairing is the
#: whole point. Defect 1 is "no rebuild after revert"; defect 2 is "rebuild only what the tool
#: happens to know how to build". A prefix list with a single recipe fixes the first and IS the
#: second: adding `fuzz/` to it and keeping `python-build` would rebuild the extension module for a
#: change to a fuzz binary, which is defect 2 wearing defect 1's clothes.
#:
#: Measured, in the corner the prefixes did not cover: a sabotage of `fuzz/exhaustive_compat.cpp`
#: reverted the source, printed CLEAN, and left `build/exhaustive_compat` compiled from the
#: sabotage. Every verdict this script printed stayed correct, because each check ran its own
#: `make`; the hand measurement that came afterwards did not, and read a pinned constant out of a
#: binary nobody had rebuilt.
ARTIFACTS: tuple[tuple[str, list[str], str], ...] = (
    ("include/", ["make", "python-build"], "the abi3 extension module"),
    ("bindings/python/src/", ["make", "python-build"], "the abi3 extension module"),
    ("bindings/c/", ["make", "python-build"], "the abi3 extension module"),
    # The compile alone -- not the enumeration, not the 3.2 M-case run. `exhaustive-compat` depends
    # on this target, so there is one compile line rather than two that can drift.
    ("fuzz/exhaustive_compat.cpp", ["make", "-C", "fuzz", "exhaustive-compat-build"],
     "build/exhaustive_compat"),
)


def git(*args: str) -> str:
    return subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True,
                          check=False).stdout


def artifact_for(rel: str) -> tuple[list[str], str] | None:
    """The rebuild command for this file's artifact, or None when this script knows of none.

    None is a NAMED silence, not a fallback: pretending to rebuild by running the recipe for some
    other artifact is worse than saying nothing, because it reads as coverage. A caller then knows
    the verdict rests on the check building whatever it needs.
    """
    if not rel.endswith((".hpp", ".cpp", ".h")):
        return None
    best: tuple[list[str], str] | None = None
    best_len = -1
    for prefix, cmd, label in ARTIFACTS:
        if rel.startswith(prefix) and len(prefix) > best_len:
            best, best_len = (cmd, label), len(prefix)
    return best


def rebuild(cmd: list[str], label: str) -> bool:
    done = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, errors="replace")
    if done.returncode != 0:
        print(f"  sabotage: rebuilding {label} FAILED, so the verdict below would be meaningless:")
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


#: The runner this script must be able to rebuild, and the constant that proves which source it was
#: compiled from. Its FAIL message prints that constant, which is what makes the state readable from
#: outside -- a guard that printed only "FAIL" could not be checked this way.
EC_SOURCE = "fuzz/exhaustive_compat.cpp"
EC_BINARY = "build/exhaustive_compat"
EC_CONST_REAL = "3218434"
EC_CONST_SABOTAGED = "9999999"


def self_test_artifact_map() -> int:
    """The mapping itself, in milliseconds: the right artifact per file, and no fallback."""
    for rel, wanted in (("include/real/real.hpp", ["make", "python-build"]),
                        ("bindings/python/src/_real.cpp", ["make", "python-build"]),
                        ("bindings/c/real_capi.cpp", ["make", "python-build"]),
                        (EC_SOURCE, ["make", "-C", "fuzz", "exhaustive-compat-build"])):
        got = artifact_for(rel)
        if got is None or got[0] != wanted:
            print(f"sabotage --self-test: {rel} maps to {got}, expected {wanted}. An artifact that "
                  f"follows the wrong file is defect 2, which is what this mapping replaced.")
            return 2
    for rel in ("fuzz/fuzz_compat.cpp", "docs/COMPATIBILITY.md"):
        if artifact_for(rel) is not None:
            print(f"sabotage --self-test: {rel} claims an artifact it does not have. A decorative "
                  f"rebuild reads as coverage; a named silence does not.")
            return 2
    print("sabotage --self-test: artifact map OK — 4 files map to their own artifact, 2 to none.")
    return 0


def _ec_tier_constant() -> str | None:
    """Which `cases_at_default_tier` the built runner carries, read from its own FAIL message."""
    binary = ROOT / EC_BINARY
    if not binary.is_file():
        return None
    corpus = ROOT / "build" / "st_ec_corpus.txt"
    corpus.write_text("a\nb\n")
    done = subprocess.run([str(binary), str(corpus), str(corpus)], cwd=ROOT,
                          capture_output=True, text=True, errors="replace")
    corpus.unlink(missing_ok=True)
    for token in (EC_CONST_SABOTAGED, EC_CONST_REAL):
        if token in done.stdout + done.stderr:
            return token
    return None


def self_test_rebuild() -> int:
    """Defect 1 in the corner the old prefix list did not cover: the artifact AFTER the revert.

    Drives a real run against the exhaustive-compat runner with a check that compiles nothing, so
    the only thing that can leave the binary correct afterwards is this script rebuilding it. The
    child's own verdict is not the measurement and is not printed: this phase tests the HARNESS
    rather than a guard, so "RED" or "GREEN" would both be the wrong word.
    """
    if not (ROOT / EC_SOURCE).is_file():
        print(f"sabotage --self-test: {EC_SOURCE} is gone; this phase now proves nothing.")
        return 2
    if git("status", "--porcelain", "--", EC_SOURCE).strip():
        print(f"sabotage --self-test: {EC_SOURCE} is dirty — commit first, as a real run requires.")
        return 2
    if not rebuild(["make", "-C", "fuzz", "exhaustive-compat-build"], EC_BINARY):
        return 2
    if _ec_tier_constant() != EC_CONST_REAL:
        print(f"sabotage --self-test: the freshly built runner does not carry {EC_CONST_REAL}, so "
              f"the probe cannot read the state it is about to check.")
        return 2

    seen = ROOT / "build" / "st_ec_seen.txt"
    seen.unlink(missing_ok=True)
    probe = ("import pathlib,subprocess;"
             "c=pathlib.Path('build/st_ec_mid.txt');c.write_text('a\\nb\\n');"
             "r=subprocess.run(['build/exhaustive_compat',str(c),str(c)],capture_output=True,"
             "text=True,errors='replace');c.unlink();"
             "pathlib.Path('build/st_ec_seen.txt').write_text("
             f"'{EC_CONST_SABOTAGED}' if '{EC_CONST_SABOTAGED}' in r.stdout+r.stderr else 'other')")
    done = subprocess.run(
        [sys.executable, str(ROOT / "tools" / "sabotage.py"),
         "--file", EC_SOURCE, "--label", "self-test: artifact after revert",
         "--old", "cases_at_default_tier {" + EC_CONST_REAL + "}",
         "--new", "cases_at_default_tier {" + EC_CONST_SABOTAGED + "}",
         "--", sys.executable, "-c", probe],
        cwd=ROOT, capture_output=True, text=True, errors="replace")

    mid = seen.read_text().strip() if seen.is_file() else "<the check never ran>"
    seen.unlink(missing_ok=True)
    if mid != EC_CONST_SABOTAGED:
        print(f"sabotage --self-test: during the run the binary carried {mid!r}, not the sabotage — "
              f"the PRE-check rebuild did not happen, so a check would measure unmodified code.")
        print("\n".join("    " + line for line in done.stdout.strip().split("\n")[-6:]))
        return 2
    after = _ec_tier_constant()
    if after != EC_CONST_REAL:
        print(f"sabotage --self-test: after the revert the binary carries {after!r}. The tree is "
              f"clean and the artifact is NOT — defect 1, which a hand measurement read once "
              f"already.")
        return 2
    print(f"sabotage --self-test: rebuild OK — the runner carried {EC_CONST_SABOTAGED} during the "
          f"run and {EC_CONST_REAL} after the revert.")
    return 0


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
        # Three phases, all or nothing: the mapping, the artifact after a revert, then the signal.
        for phase in (self_test_artifact_map, self_test_rebuild, self_test):
            code = phase()
            if code != 0:
                return code
        return 0
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
        artifact = artifact_for(rel)
        if artifact is not None and not rebuild(*artifact):
            return 2
        if artifact is None:
            print(f"  sabotage: no artifact registered for {rel} -- nothing is prebuilt, so the "
                  f"check below has to build whatever it measures.")
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
        if artifact is not None:
            rebuild(*artifact)  # the artifact must not outlive the sabotage; this is defect 1
        dirty = git("status", "--porcelain", "--", rel).strip()
        state = 'CLEAN' if not dirty else 'STILL DIRTY: ' + dirty
        built = artifact[1] if artifact is not None else "no registered artifact"
        print(f"    reverted ({built} rebuilt) — {rel}: {state}")
    return verdict


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
