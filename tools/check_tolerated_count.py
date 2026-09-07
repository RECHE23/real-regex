#!/usr/bin/env python3
r"""Tie the tolerated-divergence count in the docs to the one the exhaustive check measures.

WHY THIS EXISTS. `fuzz/exhaustive_compat.cpp` counts the documented nullable-loop capture class and
the size of the space it walked, prints both, and exits on `serious == 0`. Four LIVE pages quote
BOTH in one sentence -- "N cases out of M in the tier-1 space". Nothing connected them to the
measurement: `check_doc_mirror` ties each mirror to its canon and neither pair to the numbers, so a
4 549th case of the documented class would leave every gate green and four published pages wrong.
That is the same failure as a closed count written without a sweep, in the other direction -- a
number asserted once and then never re-asked.

The space half arrived later and for the sharper reason: it read "3.2 M", an approximation that
would have absorbed the loss of tens of thousands of agreeing cases without one character changing,
in the very sentence whose job is to say how much was compared. Both numbers are pinned now, and the
self-test drifts each of them on each page -- guarding one of two is how the first one came to be an
approximation while the other was watched.

The source of truth is the C++ constant, because that is where the measurement lives. This script
reads it and requires every page to quote exactly it. Old release notes are NOT read: they are dated
snapshots of a past measurement, and rewriting them would be falsifying a record. Neither is the
`pike.hpp` span-filler comment, for the same reason -- it reports a measurement in the past tense.

The four pages are checked directly even though `check_doc_mirror` already ties each mirror to its
canon (verified: editing a canon's figure alone trips it). Checking two would work today and would
depend on a section staying inside that comparison -- a coupling whose failure would be silent, and
silence is the thing this file is about.
"""
from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]

#: Where the measurement lives, and the two constants the guarded sentence carries. The sentence is
#: "N cases out of M in the tier-1 space": N is the documented class, M is the SPACE. M used to read
#: "3.2 M" -- an arrangement that would have absorbed the loss of tens of thousands of agreeing cases
#: without a character changing, in a sentence whose whole job is to say how much was compared.
SOURCE = ROOT / "fuzz" / "exhaustive_compat.cpp"
SOURCE_RES = (
    ("tolerated", re.compile(r"tolerated_with_residue\s*\{(\d+)\}")),
    ("space", re.compile(r"cases_at_default_tier\s*\{(\d+)\}")),
)

#: The live pages that quote it. Both canons and both of their site mirrors.
PAGES = (
    ROOT / "docs" / "COMPATIBILITY.md",
    ROOT / "docs" / "site" / "drop-in" / "std-regex-reference.md",
    ROOT / "docs" / "divergences.dox",
    ROOT / "docs" / "site" / "differences-from-re.md",
)

#: The prose shape, in both spellings the pages use (parenthesised, and em-dash flanked). BOTH
#: numbers are captured: pinning one of the two would leave the other free to drift, which is the
#: state this file was in until the space stopped being an approximation.
PAGE_RE = re.compile(r"([\d][\d ]*?) cases out of ([\d][\d ]*?) in the tier-1 space")


def grouped(value: int) -> str:
    """The docs' own spelling: space-grouped thousands, as in `4 548`."""
    return f"{value:,}".replace(",", " ")


def measured() -> dict[str, int] | None:
    """Both pinned constants from the exhaustive check, or None when either cannot be read."""
    text = SOURCE.read_text(encoding="utf-8")
    out: dict[str, int] = {}
    for name, pattern in SOURCE_RES:
        match = pattern.search(text)
        if match is None:
            return None
        out[name] = int(match.group(1))
    return out


def run(*, page_texts: dict[pathlib.Path, str] | None = None, quiet: bool = False) -> int:
    """Compare every page's quoted count against the C++ constant."""
    expected = measured()
    if expected is None:
        print(f"check_tolerated_count: FAIL — `tolerated_with_residue` or `cases_at_default_tier` "
              f"is missing from {SOURCE.relative_to(ROOT)}. A source of truth moved or was renamed; "
              f"this script is now checking nothing, which is worse than not existing.")
        return 1

    texts = page_texts if page_texts is not None else {p: p.read_text(encoding="utf-8") for p in PAGES}
    want_tol, want_space = grouped(expected["tolerated"]), grouped(expected["space"])
    bad: list[str] = []
    for path, text in texts.items():
        found = PAGE_RE.findall(text)
        if not found:
            bad.append(f"{path.relative_to(ROOT)}: the sentence is gone (expected `{want_tol} cases "
                       f"out of {want_space} in the tier-1 space`)")
            continue
        for quoted_tol, quoted_space in found:
            if quoted_tol.strip() != want_tol:
                bad.append(f"{path.relative_to(ROOT)}: quotes {quoted_tol.strip()!r} cases, "
                           f"measured {want_tol!r}")
            if quoted_space.strip() != want_space:
                bad.append(f"{path.relative_to(ROOT)}: quotes a space of {quoted_space.strip()!r}, "
                           f"measured {want_space!r}")

    if bad:
        if not quiet:
            print("check_tolerated_count: FAIL — the docs and the measurement disagree.")
            for line in bad:
                print(f"    {line}")
            print(f"    The measurements are `tolerated_with_residue` (= {want_tol}) and "
                  f"`cases_at_default_tier` (= {want_space}) in {SOURCE.relative_to(ROOT)}. If a "
                  f"count really changed, move the constant AND every page in the same commit; if "
                  f"it did not, something regressed in the compat routing or the enumerator.")
        return 1

    if not quiet:
        print(f"check_tolerated_count: OK — {len(texts)} page(s) quote {want_tol} out of "
              f"{want_space}, both counts {SOURCE.relative_to(ROOT)} pins.")
    return 0


def self_test() -> int:
    """Inject the exact drift this exists to catch, and require the comparison to notice.

    A guard never seen red is not a guard. The injection is done on an in-memory copy, so a killed
    run cannot leave a falsified page behind.
    """
    expected = measured()
    if expected is None:
        print("check_tolerated_count: SELF-TEST INCONCLUSIVE — a constant is unreadable, so there "
              "is nothing to drift from.")
        return 1
    #: Both numbers, on every page: pinning one and self-testing only that one is how the space came
    #: to be an approximation while the class was guarded.
    for which in ("tolerated", "space"):
        wanted, drifted = grouped(expected[which]), grouped(expected[which] + 1)
        for target in PAGES:
            text = target.read_text(encoding="utf-8")
            if wanted not in text:
                print(f"check_tolerated_count: SELF-TEST INCONCLUSIVE — cannot inject the {which} "
                      f"count into {target.relative_to(ROOT)}; it does not quote {wanted}.")
                return 1
            texts = {p: (text.replace(wanted, drifted, 1) if p == target else
                         p.read_text(encoding="utf-8")) for p in PAGES}
            if run(page_texts=texts, quiet=True) == 0:
                print(f"check_tolerated_count: SELF-TEST FAILED — a drifted {which} count "
                      f"({drifted}) in {target.relative_to(ROOT)} did NOT trip the comparison. The "
                      f"guard is blind there; fix it before trusting a green.")
                return 1
    print(f"check_tolerated_count: self-test OK — a drift in EITHER count trips the comparison on "
          f"each of the {len(PAGES)} pages ({2 * len(PAGES)} injections).")
    return 0


if __name__ == "__main__":
    sys.exit(self_test() if "--self-test" in sys.argv[1:] else run())
