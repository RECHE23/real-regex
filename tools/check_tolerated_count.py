#!/usr/bin/env python3
r"""Tie the tolerated-divergence count in the docs to the one the exhaustive check measures.

WHY THIS EXISTS. `fuzz/exhaustive_compat.cpp` counts the documented nullable-loop capture class,
prints it, and exits on `serious == 0`. Four LIVE pages quote that count in prose. Nothing connected
the two: `check_doc_mirror` ties each mirror to its canon and neither pair to the measurement, so a
4 549th case of the documented class would leave every gate green and four published pages wrong.
That is the same failure as a closed count written without a sweep, in the other direction -- a
number asserted once and then never re-asked.

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

#: Where the measurement lives, and the constant that carries it.
SOURCE = ROOT / "fuzz" / "exhaustive_compat.cpp"
SOURCE_RE = re.compile(r"tolerated_with_residue\s*\{(\d+)\}")

#: The live pages that quote it. Both canons and both of their site mirrors.
PAGES = (
    ROOT / "docs" / "COMPATIBILITY.md",
    ROOT / "docs" / "site" / "drop-in" / "std-regex-reference.md",
    ROOT / "docs" / "divergences.dox",
    ROOT / "docs" / "site" / "differences-from-re.md",
)

#: The prose shape, in both spellings the pages use (parenthesised, and em-dash flanked).
PAGE_RE = re.compile(r"([\d][\d ]*?) cases out of 3\.2 M in the tier-1 space")


def grouped(value: int) -> str:
    """The docs' own spelling: space-grouped thousands, as in `4 548`."""
    return f"{value:,}".replace(",", " ")


def measured() -> int | None:
    """The pinned constant from the exhaustive check, or None when it cannot be read."""
    match = SOURCE_RE.search(SOURCE.read_text(encoding="utf-8"))
    return int(match.group(1)) if match else None


def run(*, page_texts: dict[pathlib.Path, str] | None = None, quiet: bool = False) -> int:
    """Compare every page's quoted count against the C++ constant."""
    expected = measured()
    if expected is None:
        print(f"check_tolerated_count: FAIL — no `tolerated_with_residue` constant in "
              f"{SOURCE.relative_to(ROOT)}. The source of truth moved or was renamed; this script "
              f"is now checking nothing, which is worse than not existing.")
        return 1

    texts = page_texts if page_texts is not None else {p: p.read_text(encoding="utf-8") for p in PAGES}
    wanted = grouped(expected)
    bad: list[str] = []
    for path, text in texts.items():
        found = PAGE_RE.findall(text)
        if not found:
            bad.append(f"{path.relative_to(ROOT)}: the sentence is gone (expected `{wanted} cases "
                       f"out of 3.2 M in the tier-1 space`)")
            continue
        for quoted in found:
            if quoted.strip() != wanted:
                bad.append(f"{path.relative_to(ROOT)}: quotes `{quoted.strip()}`, measured `{wanted}`")

    if bad:
        if not quiet:
            print("check_tolerated_count: FAIL — the docs and the measurement disagree.")
            for line in bad:
                print(f"    {line}")
            print(f"    The measurement is `tolerated_with_residue` in {SOURCE.relative_to(ROOT)} "
                  f"(= {wanted}). If the class really changed size, move the constant AND every page "
                  f"in the same commit; if it did not, something regressed in the compat routing.")
        return 1

    if not quiet:
        print(f"check_tolerated_count: OK — {len(texts)} page(s) quote {wanted}, the count "
              f"{SOURCE.relative_to(ROOT)} pins.")
    return 0


def self_test() -> int:
    """Inject the exact drift this exists to catch, and require the comparison to notice.

    A guard never seen red is not a guard. The injection is done on an in-memory copy, so a killed
    run cannot leave a falsified page behind.
    """
    expected = measured()
    if expected is None:
        print("check_tolerated_count: SELF-TEST INCONCLUSIVE — the constant is unreadable, so "
              "there is nothing to drift from.")
        return 1
    wanted, drifted = grouped(expected), grouped(expected + 1)
    for target in PAGES:
        text = target.read_text(encoding="utf-8")
        if wanted not in text:
            print(f"check_tolerated_count: SELF-TEST INCONCLUSIVE — cannot inject into "
                  f"{target.relative_to(ROOT)}; it does not quote {wanted}.")
            return 1
        texts = {p: (text.replace(wanted, drifted, 1) if p == target else
                     p.read_text(encoding="utf-8")) for p in PAGES}
        if run(page_texts=texts, quiet=True) == 0:
            print(f"check_tolerated_count: SELF-TEST FAILED — {drifted} in "
                  f"{target.relative_to(ROOT)} did NOT trip the comparison. The guard is blind on "
                  f"that page; fix it before trusting a green.")
            return 1
    print(f"check_tolerated_count: self-test OK — a drifted count trips the comparison on each of "
          f"the {len(PAGES)} pages.")
    return 0


if __name__ == "__main__":
    sys.exit(self_test() if "--self-test" in sys.argv[1:] else run())
