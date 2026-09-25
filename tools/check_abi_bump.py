#!/usr/bin/env python3
"""Refuse an incompatible change to the C ABI that does not move REAL_ABI_VERSION.

The C ABI (bindings/c/real_capi.h) is frozen-additive: tests/bindings/capi_abi_golden.txt pins its
enums, flags and normalized prototypes, and gen_capi_abi_golden.py --check keeps the golden in step
with the header. That check cannot tell an addition from a break -- both change the golden. This one
compares the golden with the one at the last release tag: a line that was there and is gone (a
removed or changed function, enum value or flag) is incompatible, and passes only when
REAL_ABI_VERSION moved up with it. Additions, reorderings and a version that moves without a break
pass. See docs/site/developer/versioning.md.

Usage:
  python3 tools/check_abi_bump.py                 # against the last v* release tag
  python3 tools/check_abi_bump.py --baseline REF  # against another git ref
  python3 tools/check_abi_bump.py --self-test     # each verdict, on synthetic goldens
"""
from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
GOLDEN = "tests/bindings/capi_abi_golden.txt"

#: A golden older than the version line pinned the surface REAL_ABI_VERSION 1 names.
UNVERSIONED = 1


def parse(text: str) -> tuple[int, set[str]]:
    """The golden's ABI version and its contract lines (comments, blanks and the version excluded)."""
    version = UNVERSIONED
    lines: set[str] = set()
    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("ABI_VERSION "):
            version = int(line.split()[1])
            continue
        lines.add(line)
    return version, lines


def verdict(old_text: str, new_text: str) -> tuple[bool, str]:
    """(passes, why) for a move from the old golden to the new one."""
    old_version, old_lines = parse(old_text)
    new_version, new_lines = parse(new_text)
    removed = sorted(old_lines - new_lines)
    if new_version < old_version:
        return False, f"REAL_ABI_VERSION went down, {old_version} -> {new_version}"
    if removed and new_version == old_version:
        listing = "\n".join(f"    - {line}" for line in removed)
        return False, (f"{len(removed)} line(s) of the released interface are gone or changed, and "
                       f"REAL_ABI_VERSION is still {new_version}:\n{listing}\n"
                       f"  Keep them (the interface is frozen-additive), or move REAL_ABI_VERSION with a "
                       f"new year (docs/site/developer/versioning.md).")
    if removed:
        return True, f"incompatible change carried by REAL_ABI_VERSION {old_version} -> {new_version}"
    added = len(new_lines - old_lines)
    return True, f"additive ({added} line(s) added), REAL_ABI_VERSION {new_version}"


def git(*args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True, check=False)


def last_release() -> str | None:
    done = git("describe", "--tags", "--abbrev=0", "--match", "v[0-9]*")
    return done.stdout.strip() or None


def self_test() -> int:
    base = "ABI_VERSION 2\nFN int real_a(void)\nFN int real_b(int)\nENUM REAL_ERR_NONE=0\n"
    cases = (
        ("an addition passes", base + "FN int real_c(void)\n", True),
        ("a reordering passes", "ABI_VERSION 2\nENUM REAL_ERR_NONE=0\nFN int real_b(int)\nFN int real_a(void)\n",
         True),
        ("a removal without a new version fails", "ABI_VERSION 2\nFN int real_a(void)\nENUM REAL_ERR_NONE=0\n",
         False),
        ("a changed signature without a new version fails",
         "ABI_VERSION 2\nFN int real_a(void)\nFN int real_b(long)\nENUM REAL_ERR_NONE=0\n", False),
        ("a changed enum value without a new version fails",
         "ABI_VERSION 2\nFN int real_a(void)\nFN int real_b(int)\nENUM REAL_ERR_NONE=1\n", False),
        ("a removal with a new version passes", "ABI_VERSION 3\nFN int real_a(void)\nENUM REAL_ERR_NONE=0\n", True),
        ("a version that goes down fails", base.replace("ABI_VERSION 2", "ABI_VERSION 1"), False),
    )
    failures = 0
    for label, new, expected in cases:
        passes, why = verdict(base, new)
        if passes != expected:
            failures += 1
            print(f"check_abi_bump --self-test: '{label}' gave {passes} ({why})")
    # A golden from before the version line is version 1: a removal against it needs 2.
    unversioned = "FN int real_a(void)\nFN int real_b(int)\n"
    if verdict(unversioned, "ABI_VERSION 1\nFN int real_a(void)\n")[0]:
        failures += 1
        print("check_abi_bump --self-test: a removal against an unversioned golden passed at version 1")
    if not verdict(unversioned, "ABI_VERSION 1\nFN int real_a(void)\nFN int real_b(int)\nFN int real_c(void)\n")[0]:
        failures += 1
        print("check_abi_bump --self-test: an addition against an unversioned golden failed")
    if failures:
        return 1
    print(f"check_abi_bump --self-test: {len(cases) + 2} verdicts as expected")
    return 0


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--baseline", metavar="REF", help="compare against this git ref instead of the last release")
    ap.add_argument("--self-test", action="store_true", help="check each verdict on synthetic goldens")
    args = ap.parse_args(argv)
    if args.self_test:
        return self_test()
    ref = args.baseline or last_release()
    if ref is None:
        print("check_abi_bump: no v* release tag to compare against (a shallow clone?); fetch the tags")
        return 1
    shown = git("show", f"{ref}:{GOLDEN}")
    if shown.returncode != 0:
        print(f"check_abi_bump: {ref} has no {GOLDEN}: {shown.stderr.strip()}")
        return 1
    passes, why = verdict(shown.stdout, (ROOT / GOLDEN).read_text(encoding="utf-8"))
    print(f"check_abi_bump: {'PASS' if passes else 'FAIL'} against {ref} -- {why}")
    return 0 if passes else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
