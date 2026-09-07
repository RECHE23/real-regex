#!/usr/bin/env python3
r"""A `std::regex` behaviour that only ONE implementation has must name that implementation.

WHY THIS EXISTS. `std::regex` is three implementations, and the compat pages say so. But a sentence
written as "std refuses X" is FALSE wherever another implementation accepts X, and it is false in the
most expensive way: it reads as a fact about the standard. This repository has already produced that
sentence once. A 360-pattern differential sweep reported 54 patterns REAL accepts and "std::regex"
refuses; the reading was an artefact of the standard library it happened to be compiled against, and
`[A-\x80]` is refused by libstdc++ (which compares `char` endpoints as SIGNED, so `0x80` is -128 and
the range reads inverted) and accepted by libc++. The count was kept out of the live catalogue for
exactly that reason and then never rewritten under the right name -- a measured fact held in
quarantine rather than published correctly.

WHAT IT CHECKS. Two halves, on both compat pages (canon and mirror -- `check_doc_mirror` ties them
to each other, and nothing tied either to the attribution):

  1. The bullet about a class range with a high upper endpoint EXISTS and names `libstdc++`.
  2. No sentence anywhere on those pages attributes that refusal to bare `std`.

The rule is general and the implementation is not: it enforces the one shape the pages carry today.
That is deliberate -- a generic "does this sentence over-attribute" checker would need to judge
prose, and a guard that guesses is worse than one with a stated perimeter. When a second
implementation-specific claim is written, it gets a second entry here, and the perimeter stays
visible instead of being assumed.
"""
from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]

#: The canon and its mirror. Both are read: the pair guard catches drift BETWEEN them, not a
#: rewrite of both, which is what an "improved" sentence looks like in a single commit.
PAGES = (
    ROOT / "docs" / "COMPATIBILITY.md",
    ROOT / "docs" / "site" / "drop-in" / "std-regex-reference.md",
)

#: The shape whose refusal belongs to libstdc++ alone. Matches `[A-\x80]`, `[\x7f-\x80]`, ...
HIGH_RANGE = re.compile(r"`\[[^`\]]*-\\x(8[0-9a-f]|[9a-f][0-9a-f])\]`")

#: An attribution of the refusal to bare `std`, in either word order: `std refuses`, `refused by
#: std`. Checked UNCONDITIONALLY -- the first version of this file only looked when the bullet
#: mentioned no implementation at all, which a sabotage showed to be the wrong gate: `libstdc++`
#: appears twice in the bullet, so renaming ONE occurrence left the word present and the check
#: silent. Presence of the right word somewhere is not attribution of the claim.
#:
#: The bullet must therefore never QUOTE the forbidden sentence, even as a counter-example, and it
#: no longer does -- a catalogue entry that carries the wrong phrasing verbatim is also an invitation
#: to copy it.
BARE_STD_REFUSES = re.compile(
    r"(?:(?<!libstdc\+\+)(?<!libc\+\+)\bstd(?:::regex)?\b[^.\n]{0,40}?"
    r"\b(?:refuses?|rejects?|throws?)\b"
    r"|\b(?:refused|rejected|thrown)\b[^.\n]{0,20}?\bby\s+(?:\*\*)?std(?:::regex)?\b)", re.I)


def bullets(text: str) -> list[str]:
    """The page split into bullets, so an attribution is judged with its own sentence, not the file."""
    out: list[str] = []
    current: list[str] = []
    for line in text.splitlines():
        if line.startswith("- ") or line.startswith("| "):
            if current:
                out.append("\n".join(current))
            current = [line]
        elif current and line.startswith("  "):
            current.append(line)
        elif current:
            out.append("\n".join(current))
            current = []
    if current:
        out.append("\n".join(current))
    return out


def run(*, page_texts: dict[pathlib.Path, str] | None = None, quiet: bool = False) -> int:
    """Both halves, on both pages."""
    texts = page_texts if page_texts is not None else {p: p.read_text(encoding="utf-8") for p in PAGES}
    bad: list[str] = []
    for path, text in texts.items():
        rel = path.relative_to(ROOT)
        carrying = [b for b in bullets(text) if HIGH_RANGE.search(b)]
        if not carrying:
            bad.append(f"{rel}: no bullet describes a class range with an upper endpoint >= 0x80. "
                       f"The entry is gone, so this check now guards nothing.")
            continue
        for bullet in carrying:
            if "libstdc++" not in bullet:
                bad.append(f"{rel}: the high-endpoint range bullet does not name `libstdc++`. "
                           f"libc++ ACCEPTS that range, so an unattributed sentence is false there.")
            hit = BARE_STD_REFUSES.search(bullet)
            if hit is not None:
                bad.append(f"{rel}: {hit.group(0)!r} attributes the refusal to bare `std`. "
                           f"Name libstdc++ (or libc++) as the subject of the verb.")

    if bad:
        if not quiet:
            print("check_stdlib_attribution: FAIL — a one-implementation behaviour is unattributed.")
            for line in bad:
                print(f"    {line}")
            print("    Measured: libstdc++ refuses (`Invalid range in bracket expression`, signed "
                  "`char` endpoints), libc++ accepts. Name the implementation, or say nothing.")
        return 1

    if not quiet:
        print(f"check_stdlib_attribution: OK — {len(texts)} page(s) attribute the high-endpoint "
              f"range refusal to libstdc++ by name.")
    return 0


def self_test() -> int:
    """Inject the exact sentence this exists to keep out, on each page in turn."""
    for target in PAGES:
        text = target.read_text(encoding="utf-8")
        if "libstdc++" not in text:
            print(f"check_stdlib_attribution: SELF-TEST INCONCLUSIVE — {target.relative_to(ROOT)} "
                  f"does not mention libstdc++, so there is nothing to un-name.")
            return 1
        texts = {p: (text.replace("libstdc++", "std") if p == target else
                     p.read_text(encoding="utf-8")) for p in PAGES}
        if run(page_texts=texts, quiet=True) == 0:
            print(f"check_stdlib_attribution: SELF-TEST FAILED — renaming libstdc++ to `std` in "
                  f"{target.relative_to(ROOT)} did NOT trip the check. It is blind on that page.")
            return 1
    print(f"check_stdlib_attribution: self-test OK — an unattributed `std` trips the check on each "
          f"of the {len(PAGES)} pages.")
    return 0


if __name__ == "__main__":
    sys.exit(self_test() if "--self-test" in sys.argv[1:] else run())
