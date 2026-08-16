#!/usr/bin/env python3
"""Refuse to treat a test binary older than the sources as a verdict.

cmake --build can report "Nothing to be done" after a checkout that changed
only headers: the test TUs did not change, and a missing depfile leaves the
header-only engine invisible. The binary that then runs is the previous
commit's. Invoked after every ``--build`` that produces ``real_tests_bin``
(``make build``, ``coverage-build``, ``sanitize``). Compares the binary's
mtime to include/real/, tests/, and bindings/c/. It does not rebuild; it
refuses to speak.

Usage:
    python3 tools/check_coverage_fresh.py build/real_tests_bin
    python3 tools/check_coverage_fresh.py build/coverage/real_tests_bin
"""
from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SOURCE_ROOTS = (
    ROOT / "include" / "real",
    ROOT / "tests",
    ROOT / "bindings" / "c",
)
SOURCE_SUFFIXES = {".hpp", ".h", ".cpp", ".cc", ".c"}


def sources() -> list[Path]:
    found: list[Path] = []
    for base in SOURCE_ROOTS:
        if not base.is_dir():
            continue
        for path in base.rglob("*"):
            if path.is_file() and path.suffix in SOURCE_SUFFIXES:
                found.append(path)
    return found


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: check_coverage_fresh.py <test-binary>", file=sys.stderr)
        return 2
    binary = Path(sys.argv[1])
    if not binary.is_file():
        print(f"check_coverage_fresh: {binary} not found — not a verdict.",
              file=sys.stderr)
        return 1
    bin_time = binary.stat().st_mtime
    stale = [p for p in sources() if p.stat().st_mtime > bin_time]
    if not stale:
        return 0
    stale.sort()
    example = stale[0].relative_to(ROOT)
    print(
        f"check_coverage_fresh: {binary} is older than {len(stale)} source(s), "
        f"e.g. {example}.\n"
        "  cmake --build reporting nothing to do is not a verdict.\n"
        "  Remove this build directory and re-run.",
        file=sys.stderr,
    )
    return 1


if __name__ == "__main__":
    sys.exit(main())
