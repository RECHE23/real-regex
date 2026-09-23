#!/usr/bin/env python3
"""Refuse to treat a test binary older than the sources as a verdict.

cmake --build can report "Nothing to be done" after a checkout that changed
only headers: the test TUs did not change, and a missing depfile leaves the
header-only engine invisible. The binary that then runs is the previous
commit's. Invoked after every ``--build`` that produces ``real_tests_bin``
(``make build``, ``coverage-build``, ``sanitize``). Compares the binary's
mtime to include/real/, tests/, and bindings/c/. It does not rebuild; it
refuses to speak. A green prints how many sources were compared.

Usage:
    python3 tools/check_coverage_fresh.py build/real_tests_bin
    python3 tools/check_coverage_fresh.py build/coverage/real_tests_bin
    python3 tools/check_coverage_fresh.py --self-test
"""
from __future__ import annotations

import contextlib
import io
import os
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SOURCE_ROOTS = (
    ROOT / "include" / "real",
    ROOT / "tests",
    ROOT / "bindings" / "c",
)
SOURCE_SUFFIXES = {".hpp", ".h", ".cpp", ".cc", ".c"}


def sources(roots: tuple[Path, ...] = SOURCE_ROOTS) -> list[Path]:
    found: list[Path] = []
    for base in roots:
        for path in base.rglob("*"):  # an absent root yields nothing
            if path.is_file() and path.suffix in SOURCE_SUFFIXES:
                found.append(path)
    return found


def judge(binary: Path, roots: tuple[Path, ...] = SOURCE_ROOTS, base: Path = ROOT) -> int:
    """Refuses (1) a missing binary, an empty source set, or a binary older than any source; else 0."""
    if not binary.is_file():
        print(f"check_coverage_fresh: {binary} not found — not a verdict.",
              file=sys.stderr)
        return 1
    srcs = sources(roots)
    if not srcs:
        print(
            "check_coverage_fresh: FAIL -- no sources under include/real/, "
            "tests/, or bindings/c/",
            file=sys.stderr,
        )
        return 1
    bin_time = binary.stat().st_mtime
    stale = [p for p in srcs if p.stat().st_mtime > bin_time]
    if not stale:
        print(f"check_coverage_fresh: {len(srcs)} source(s) compared, {binary} is fresh")
        return 0
    stale.sort()
    example = stale[0].relative_to(base)
    print(
        f"check_coverage_fresh: {binary} is older than {len(stale)} source(s), "
        f"e.g. {example}.\n"
        "  cmake --build reporting nothing to do is not a verdict.\n"
        "  Remove this build directory and re-run.",
        file=sys.stderr,
    )
    return 1


# Arm markers, one per verdict: a case must print its own arm's and no other's.
_ARMS = {
    "missing": "not found — not a verdict",
    "empty": "no sources under",
    "stale": "is older than",
    "fresh": "is fresh",
}


def self_test() -> int:
    """Drives each arm of ``judge`` alone on a synthetic tree with pinned modification times."""
    def case(name: str, want_rc: int, arm: str, build) -> bool:
        with tempfile.TemporaryDirectory() as tmp:
            base = Path(tmp)
            roots = (base / "include", base / "tests", base / "absent")
            binary = build(base, roots)
            out, err = io.StringIO(), io.StringIO()
            try:
                with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
                    rc = judge(binary, roots, base)
            except Exception as exc:  # a removed guard surfaces as a crash; report it as this case's failure
                print(f"SELF-TEST FAILED: {name}: judge raised {type(exc).__name__}: {exc}")
                return False
        text = out.getvalue() + err.getvalue()
        wrong = [a for a, m in _ARMS.items() if a != arm and m in text]
        if rc != want_rc or _ARMS[arm] not in text or wrong:
            print(f"SELF-TEST FAILED: {name}: rc={rc} (want {want_rc}), arm {arm!r} "
                  f"{'present' if _ARMS[arm] in text else 'ABSENT'}, other arms {wrong}\n    {text.strip()}")
            return False
        return True

    def touch(path: Path, when: int) -> Path:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("x")
        os.utime(path, (when, when))
        return path

    def missing(base, roots):
        touch(roots[0] / "a.hpp", 100)
        return base / "bin"  # never created

    def empty(base, roots):
        (roots[0]).mkdir(parents=True)
        touch(roots[0] / "notes.txt", 100)  # not a source suffix: the set is still empty
        return touch(base / "bin", 200)

    def stale(base, roots):
        touch(roots[0] / "old.hpp", 100)
        touch(roots[1] / "new.cpp", 300)  # newer than the binary
        return touch(base / "bin", 200)

    def fresh(base, roots):
        touch(roots[0] / "a.hpp", 100)
        touch(roots[1] / "b.cpp", 150)
        touch(roots[1] / "later.txt", 900)  # newer, but not a source: ignored
        return touch(base / "bin", 200)     # roots[2] does not exist: skipped, not an error

    results = [
        case("binary missing", 1, "missing", missing),
        case("no source-suffixed file under any root", 1, "empty", empty),
        case("a source newer than the binary", 1, "stale", stale),
        case("every source older; a newer non-source and an absent root ignored", 0, "fresh", fresh),
    ]
    if not all(results):
        print(f"check_coverage_fresh: self-test FAILED ({results.count(False)} of {len(results)} case(s))")
        return 1
    print(f"check_coverage_fresh: self-test OK — {len(results)} cases: a missing binary, an empty source set "
          "and a stale binary each refused alone; a fresh one passes past a newer non-source and an absent root")
    return 0


def main() -> int:
    if sys.argv[1:] == ["--self-test"]:
        return self_test()
    if len(sys.argv) != 2:
        print("usage: check_coverage_fresh.py <test-binary> | --self-test", file=sys.stderr)
        return 2
    return judge(Path(sys.argv[1]))


if __name__ == "__main__":
    sys.exit(main())
