#!/usr/bin/env python3
"""`docs/site/differences-from-re.md` must be a faithful mirror of `docs/divergences.dox`.

WHAT IT READS. Exactly the two files. Nothing is built, nothing is imported: this needs the
standard library alone, so it runs in the gate's cheap section beside check-site-anchors.

WHY THIS EXISTS. `docs/divergences.dox` is the canon -- it feeds `/api` and it is what in-repo
references cite. The site page carries a header saying it is a mirror and that prose must never be
edited there alone. That rule had no instrument, and it was broken four times before anyone
noticed, one of them shipping: v2026.8.20 published a site page reading "Five differences are in
the module instead" over an `/api` page still reading "Four", with the renamed callout constructs
and the per-surface fallback table present in one and absent from the other. A rule stated only in
prose is a rule that drifts.

WHAT IT COMPARES. Section by section: every `\\section div_X` in the canon against the `(div_X)=`
target in the mirror. The set of sections must match exactly -- one present on a single side is a
failure, not a warning -- and each shared section's body must be equal after applying the
transformations the mirror's own header documents:

  * doubled escapes collapse (`\\\\p{...}` in Doxygen is `\\p{...}` in CommonMark);
  * `\\ref div_X` and `{ref}`div_X`` are the same cross-reference;
  * whitespace runs are equivalent (the two formats wrap at different widths).

WHAT IT DOES NOT COMPARE, and why that is named rather than normalised. Where the canon `\\ref`s a
page the site does not publish, the mirror substitutes a raw link into `/api` -- that changes the
PROSE, not just the markup, so no normalisation can make the two equal. Those sections are listed
in LINK_SUBSTITUTED and skipped, and the count of skipped sections is printed with the verdict: a
silent exemption is how a check comes to measure nothing.

SELF-TEST. `--self-test` injects the exact drift this guard exists to catch -- one bullet added to
the mirror and not to the canon -- and fails if the comparison stays green. A guard that has never
been seen to fail is a guard nobody has checked.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CANON = ROOT / "docs" / "divergences.dox"
MIRROR = ROOT / "docs" / "site" / "differences-from-re.md"

#: Sections where the canon cross-references a page the site does not publish, so the mirror
#: substitutes a link and the prose legitimately differs. Each entry costs a comparison, so the
#: verdict prints how many were skipped.
LINK_SUBSTITUTED = frozenset({"div_lookbehind"})


def canon_sections(text: str) -> dict[str, str]:
    """Split the canon on `\\section div_X Title`, returning {id: body}."""
    out: dict[str, str] = {}
    current: str | None = None
    body: list[str] = []
    for line in text.split("\n"):
        found = re.match(r"\\section (\w+) ", line)
        if found:
            if current is not None:
                out[current] = "\n".join(body).strip()
            current, body = found.group(1), []
        elif current is not None:
            body.append(line)
    if current is not None:
        out[current] = "\n".join(body).strip()
    return out


def mirror_sections(text: str) -> dict[str, str]:
    """Split the mirror on `(div_X)=` targets, dropping the `## Title` line that follows each."""
    out: dict[str, str] = {}
    current: str | None = None
    body: list[str] = []
    for line in text.split("\n"):
        found = re.match(r"\((\w+)\)=$", line)
        if found:
            if current is not None:
                out[current] = "\n".join(body).strip()
            current, body = found.group(1), []
            continue
        if current is None:
            continue
        if not body and line.startswith("## "):
            continue  # the heading the canon carries on its \section line
        body.append(line)
    if current is not None:
        out[current] = "\n".join(body).strip()
    return out


def normalise(text: str) -> str:
    """Apply the mirror header's own transformations, so only real drift survives."""
    text = text.replace("\\\\", "\\")  # BEFORE the \ref rule: the canon writes \\ref, doubled
    text = re.sub(r"\{ref\}`(\w+)`", r"\1", text)
    text = re.sub(r"\\ref (\w+)", r"\1", text)
    return re.sub(r"\s+", " ", text).strip()


def compare(canon_text: str, mirror_text: str) -> tuple[list[str], list[str], list[str], int]:
    """Return (only-in-canon, only-in-mirror, differing, compared-count)."""
    canon, mirror = canon_sections(canon_text), mirror_sections(mirror_text)
    only_canon = sorted(set(canon) - set(mirror))
    only_mirror = sorted(set(mirror) - set(canon))
    shared = sorted(set(canon) & set(mirror) - LINK_SUBSTITUTED)
    differing = [name for name in shared if normalise(canon[name]) != normalise(mirror[name])]
    return only_canon, only_mirror, differing, len(shared)


def first_divergence(a: str, b: str) -> str:
    """A short excerpt around the first differing character, for a message worth reading."""
    at = next((i for i in range(min(len(a), len(b))) if a[i] != b[i]), min(len(a), len(b)))
    lo = max(0, at - 50)
    return f"      canon : …{a[lo:at + 70]}\n      mirror: …{b[lo:at + 70]}"


def run(canon_text: str, mirror_text: str, *, quiet: bool = False) -> int:
    only_canon, only_mirror, differing, compared = compare(canon_text, mirror_text)
    if not (only_canon or only_mirror or differing):
        if not quiet:
            skipped = len(LINK_SUBSTITUTED)
            print(f"check_doc_mirror: clean — {compared} section(s) compared, "
                  f"{skipped} skipped (link-substituted: {', '.join(sorted(LINK_SUBSTITUTED))})")
        return 0
    if quiet:
        return 1
    print("check_doc_mirror: FAIL — the site page has drifted from its canon.")
    print(f"  canon:  {CANON.relative_to(ROOT)}")
    print(f"  mirror: {MIRROR.relative_to(ROOT)}")
    for name in only_canon:
        print(f"  MISSING FROM MIRROR: {name}")
    for name in only_mirror:
        print(f"  MISSING FROM CANON:  {name}")
    canon, mirror = canon_sections(canon_text), mirror_sections(mirror_text)
    for name in differing:
        print(f"  DIFFERS: {name}")
        print(first_divergence(normalise(canon[name]), normalise(mirror[name])))
    print("  Edit the .dox first, then re-mirror — never the site page alone.")
    return 1


def self_test(canon_text: str, mirror_text: str) -> int:
    """Inject the exact drift this exists to catch, and require the comparison to notice."""
    anchor = "- **`re.Scanner` is absent.**"
    if anchor not in mirror_text:
        print("check_doc_mirror: SELF-TEST INCONCLUSIVE — the injection anchor is gone from the "
              "mirror. Pick another bullet; a self-test that cannot inject proves nothing.")
        return 1
    injected = mirror_text.replace(anchor, "- **Injected: a bullet the canon does not have.**\n"
                                   + anchor, 1)
    if run(canon_text, injected, quiet=True) == 0:
        print("check_doc_mirror: SELF-TEST FAILED — a bullet added to the mirror alone did NOT "
              "trip the comparison. The guard is blind; fix it before trusting a green.")
        return 1
    print("check_doc_mirror: self-test OK — a mirror-only bullet trips the comparison.")
    return 0


def main(argv: list[str]) -> int:
    canon_text, mirror_text = CANON.read_text(), MIRROR.read_text()
    if "--self-test" in argv:
        return self_test(canon_text, mirror_text) or run(canon_text, mirror_text)
    return run(canon_text, mirror_text)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
