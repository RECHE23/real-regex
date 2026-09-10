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

The rule is general and the implementation is not: it enforces the shapes the pages carry today,
one CLAIMS entry each. That is deliberate -- a generic "does this sentence over-attribute" checker
would need to judge prose, and a guard that guesses is worse than one with a stated perimeter. A new
implementation-specific claim gets a new entry, and the perimeter stays visible instead of assumed.

THE SECOND ENTRY, AND WHY IT IS HERE. POSIX submatch selection is implementation-specific in exactly
the same way: **macOS's libc** maximises group 1 then group 2, **glibc** reports the winning thread
(as this engine does, and as Go's `regexp` does). The pages first said "libc reports" -- the same
over-attribution as "std::regex refuses", with `libc` in the place of `std`, committed by the same
hand that had already written the guard for `std`. A guard named for one word does not cover the
class, which is why the shapes are a table now and the self-test drives each entry on its own.

Both entries locate their sentence in a BLOCK (blank-line separated), not a bullet: the `std` claim
lives in a bullet, the `libc` claim in a numbered list item spanning several lines, and a
bullet-only locator sees the first and not the second.
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

#: One entry per implementation-specific claim the pages carry. `shape` locates the block that makes
#: the claim; `names` are the implementations it must name (all of them -- naming one half of a split
#: tells half the story); `over` matches the family word used as the subject of the behaviour, which
#: is the error each entry exists to refuse.
#:
#: `over` is checked UNCONDITIONALLY, never gated on the right word being absent somewhere: the first
#: version of this file only looked when the bullet mentioned no implementation, and a sabotage showed
#: that to be the wrong gate -- `libstdc++` appears twice in that bullet, so renaming ONE occurrence
#: left the word present and the check silent. A block must therefore never QUOTE the forbidden
#: sentence, even as a counter-example.
CLAIMS: tuple[dict[str, object], ...] = (
    {
        "name":  "high-endpoint class range",
        # `[A-\x80]`, `[\x7f-\x80]`, ...
        "shape": re.compile(r"`\[[^`\]]*-\\x(8[0-9a-f]|[9a-f][0-9a-f])\]`"),
        "names": ("libstdc++",),
        "why":   "libc++ ACCEPTS that range, so an unattributed sentence is false there",
        # `std refuses`, `refused by std`, in either word order.
        "over":  re.compile(r"(?:(?<!libstdc\+\+)(?<!libc\+\+)\bstd(?:::regex)?\b[^.\n]{0,40}?"
                            r"\b(?:refuses?|rejects?|throws?)\b"
                            r"|\b(?:refused|rejected|thrown)\b[^.\n]{0,20}?\bby\s+(?:\*\*)?std(?:::regex)?\b)",
                            re.I),
        "fix":   "Name libstdc++ (or libc++) as the subject of the verb.",
    },
    {
        "name":  "POSIX submatch selection",
        # The one-line proof is this claim's distinctive artifact, as the range is the other's.
        "shape": re.compile(r"`\(x\|xy\)\(y\*\)`"),
        "names": ("macOS", "glibc"),
        "why":   "glibc reports the winning thread like this engine, so `libc maximises` is false there",
        # Bare `libc` as the subject of the behaviour. `glibc` cannot match (no word boundary before
        # `libc` inside it); `macOS's libc` is excluded by the lookbehind. `POSIX specifies ...` is
        # CORRECT and deliberately not matched -- the standard does specify the rule; no
        # implementation follows it universally.
        "over":  re.compile(r"(?<!macOS's )\blibc\b[^.\n]{0,40}?"
                            r"\b(?:maximis\w+|maximiz\w+|reports?|renders?|selects?)\b", re.I),
        "fix":   "Name macOS's libc and glibc; they disagree.",
    },
)


def blocks(text: str) -> list[str]:
    """The page split on blank lines, so a claim is judged with its own paragraph.

    Blocks rather than bullets: the `std` claim lives in a bullet, the `libc` claim in a numbered
    list item spanning several lines. A bullet-only split saw the first and not the second, which is
    a guard that reads clean because it never looked.
    """
    out: list[str] = []
    current: list[str] = []
    for line in text.splitlines():
        if line.strip():
            current.append(line)
        elif current:
            out.append("\n".join(current))
            current = []
    if current:
        out.append("\n".join(current))
    return out


def run(*, page_texts: dict[pathlib.Path, str] | None = None, quiet: bool = False) -> int:
    """Every claim, on every page: located, attributed, and not over-attributed."""
    texts = page_texts if page_texts is not None else {p: p.read_text(encoding="utf-8") for p in PAGES}
    bad: list[str] = []
    for path, text in texts.items():
        rel = path.relative_to(ROOT)
        for claim in CLAIMS:
            shape, names = claim["shape"], claim["names"]
            carrying = [b for b in blocks(text) if shape.search(b)]
            if not carrying:
                bad.append(f"{rel}: no block makes the {claim['name']} claim. The entry is gone, "
                           f"so this check now guards nothing.")
                continue
            for block in carrying:
                missing = [n for n in names if n not in block]
                if missing:
                    bad.append(f"{rel}: the {claim['name']} block does not name "
                               f"{', '.join(repr(n) for n in missing)} — {claim['why']}.")
                hit = shape and claim["over"].search(block)
                if hit is not None:
                    bad.append(f"{rel}: {hit.group(0)!r} attributes the {claim['name']} behaviour to "
                               f"a whole family. {claim['fix']}")

    if bad:
        if not quiet:
            print("check_stdlib_attribution: FAIL — a one-implementation behaviour is unattributed.")
            for line in bad:
                print(f"    {line}")
            print("    Measured: libstdc++ refuses `[A-\\x80]` (`Invalid range in bracket expression`, "
                  "signed `char` endpoints) and libc++ accepts it; macOS's libc maximises POSIX "
                  "submatches and glibc does not. Name the implementation, or say nothing.")
        return 1

    if not quiet:
        print(f"check_stdlib_attribution: OK — {len(texts)} page(s) x {len(CLAIMS)} claim(s) name "
              f"their implementation and attribute nothing to a family.")
    return 0


def self_test() -> int:
    """Drive each claim, and each of its two arms, on its own.

    Two arms per entry -- the implementation is not named, and the family is the subject -- and they
    must be driven separately. `check_apt_bound` shipped a self-test whose one synthetic input tripped
    two arms at once, so blinding either left the sibling firing and the assertion green; that file was
    measured blind on three of four arms. The same trap applies per ENTRY: an injection that trips the
    `std` claim says nothing about the `libc` one.
    """
    live = {p: p.read_text(encoding="utf-8") for p in PAGES}
    cases: list[tuple[str, dict[pathlib.Path, str]]] = []
    for claim in CLAIMS:
        for target in PAGES:
            block_present = any(claim["shape"].search(b) for b in blocks(live[target]))
            if not block_present:
                print(f"check_stdlib_attribution: SELF-TEST INCONCLUSIVE — "
                      f"{target.relative_to(ROOT)} makes no {claim['name']} claim, so there is "
                      f"nothing to un-name.")
                return 1
            # Arm 1: the implementation goes unnamed. Every required name is removed, one case,
            # because a partially-named split is arm 1 too and the message lists what is missing.
            stripped = live[target]
            for name in claim["names"]:
                stripped = stripped.replace(name, "the library")
            cases.append((f"{claim['name']} unnamed in {target.relative_to(ROOT)}",
                          {p: (stripped if p == target else live[p]) for p in PAGES}))
            # Arm 3: the claim's block is GONE. Driven on its own because arms 1 and 2 both keep the
            # shape in place, so neither exercises the "no block makes this claim" branch -- the arm
            # that catches a page quietly dropping the sentence this file exists to police.
            without = claim["shape"].sub("(removed)", live[target])
            cases.append((f"{claim['name']} block removed from {target.relative_to(ROOT)}",
                          {p: (without if p == target else live[p]) for p in PAGES}))
    # Arm 2, per claim: the family as the subject of the behaviour, injected into the very block
    # that carries the claim so the block-local check is what refuses it.
    overs = (("high-endpoint class range", "std::regex refuses that range."),
             ("POSIX submatch selection", "libc reports the maximised groups."))
    # The entry set is pinned here rather than derived, so deleting a CLAIMS entry is a diagnosis
    # instead of a StopIteration: a guard that refuses by crashing reports nothing a reader can act
    # on, and an exit status cannot tell the two apart.
    known = {c["name"] for c in CLAIMS}
    expected = {name for name, _ in overs}
    if known != expected:
        print(f"check_stdlib_attribution: SELF-TEST FAILED — CLAIMS is {sorted(known)} but this "
              f"self-test drives {sorted(expected)}. An entry was added or removed without its "
              f"family-as-subject case; the perimeter has to stay visible.")
        return 1
    for name, sentence in overs:
        claim = next(c for c in CLAIMS if c["name"] == name)
        target = PAGES[0]
        text = live[target]
        blks = [b for b in blocks(text) if claim["shape"].search(b)]
        injected = text.replace(blks[0], blks[0] + "\n" + sentence, 1)
        cases.append((f"{name}: family as the subject",
                      {p: (injected if p == target else live[p]) for p in PAGES}))

    for label, texts in cases:
        if run(page_texts=texts, quiet=True) == 0:
            print(f"check_stdlib_attribution: SELF-TEST FAILED — {label} did NOT trip the check.")
            return 1

    # The live tree must be green, or every refusal above proves nothing about THIS tree.
    if run(page_texts=live, quiet=True) != 0:
        print("check_stdlib_attribution: SELF-TEST FAILED — the live pages are already red, so the "
              "synthetic trips cannot be trusted.")
        return 1

    # The printing branch: an exit status cannot tell a clean refusal from a traceback, and this
    # file's message names files, claims and the fix a reader acts on.
    import contextlib
    import io
    out, err = io.StringIO(), io.StringIO()
    label, texts = cases[0]
    try:
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = run(page_texts=texts, quiet=False)
    except Exception as exc:  # noqa: BLE001 — any exception here IS the defect
        print(f"check_stdlib_attribution: SELF-TEST FAILED — the failure path raised "
              f"{type(exc).__name__}: {exc}")
        return 1
    printed = out.getvalue()
    problems = [f"exit {code}, expected 1"] if code != 1 else []
    if err.getvalue().strip():
        problems.append(f"wrote to stderr: {err.getvalue().strip()[:80]!r}")
    for token in ("libstdc++", "macOS's libc", "glibc"):
        if token not in printed:
            problems.append(f"the footer never names {token!r}")
    if problems:
        print("check_stdlib_attribution: SELF-TEST FAILED — the verbose failure path is wrong:")
        for problem in problems:
            print(f"    {problem}")
        for line in printed.strip().split("\n"):
            print(f"      {line}")
        return 1

    print(f"check_stdlib_attribution: self-test OK — {len(cases)} case(s): each of the "
          f"{len(CLAIMS)} claim(s) goes unnamed on each of the {len(PAGES)} pages and trips, each "
          f"claim's family-as-subject sentence trips, the live pages stay green, and the failure "
          f"path names both implementations on stdout alone.")
    return 0


if __name__ == "__main__":
    sys.exit(self_test() if "--self-test" in sys.argv[1:] else run())
