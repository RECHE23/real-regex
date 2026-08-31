#!/usr/bin/env python3
"""Every site page that declares a canon must still agree with it — in the way it says it does.

WHAT IT READS. Seven files, no builds and no imports: the standard library alone, so this runs in
the gate's cheap section beside check-site-anchors.

WHY THIS EXISTS. Five pages under docs/site/ name a canon in an HTML comment. The rule was prose
only, and it drifted twice, both times shipping. differences-from-re.md was edited alone four
times, so v2026.8.20 published a site page reading "Five differences are in the module instead"
over an /api page still reading "Four". And std-regex-reference.md kept naming an internal template
parameter (`TrailingLA=false`) for two weeks after the canon deliberately stopped naming it — the
canon was reworded to satisfy check_doc_voice_source, and the mirror was forgotten.

THEY ARE NOT ALL THE SAME RELATIONSHIP, and that is the design. Two are MIRRORS: a body copied
byte-for-byte through a handful of named transformations, which a machine can check. Three are
DISTILLATIONS — "linked, not copied", "distilled here, not copied" — and comparing their prose
would be wrong rather than merely hard: a distillation is allowed to say less, and differently.
go.md was measured at 0 of its canon's 9 blocks verbatim while being 92% of its length; its header
had said only "Canon =", and that ambiguity is what cost a manual comparison during the Go v0.2.0
break.

THE TRANSFORMATIONS LIVE IN EACH MIRROR'S HEADER, NOT HERE. This script applies exactly what those
headers declare and compares everything else. A transformation known only to this file would put
the rule back in the tool, which is how the first pair drifted; writing the fifth rule of the
second pair into its header is what made it visible at all.

WHAT IS NOT COMPARED IS NAMED AND COUNTED. div_lookbehind (the canon \refs a page the site does
not publish, so the mirror substitutes a link and the PROSE differs) and the Feature scorecard
section (substituted by a pointer to the CI-probed Features matrix — a copied table would drift).
Both appear in the verdict: a silent exemption is how a check comes to measure nothing.

SELF-TEST. `--self-test` injects the drift each pair exists to catch — a bullet, then a paragraph,
added to a mirror and not to its canon — and fails if either comparison stays green. A guard that
has never been seen to fail is a guard nobody has checked.
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

#: The SECOND pair: docs/COMPATIBILITY.md -> docs/site/drop-in/std-regex-reference.md. A different
#: pair, not the same one with other paths: it is split by `##` heading rather than by \section,
#: and its transformations are its own. Both are named in the mirror's header, which is the
#: contract this file applies -- a transformation that lives only here would put the rule back in
#: the tool, which is how the first pair drifted for two weeks.
CANON_2 = ROOT / "docs" / "COMPATIBILITY.md"
MIRROR_2 = ROOT / "docs" / "site" / "drop-in" / "std-regex-reference.md"

#: The one substituted section, by heading on each side. Its body is deliberately NOT mirrored
#: (the canon's generated scorecard would drift as a copy), so both headings must exist and
#: nothing inside is compared.
SUBSTITUTED_SECTION = ("Feature scorecard", "Feature status")


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


def heading_sections(text: str) -> dict[str, str]:
    """Split on `## Heading`, returning {heading: body}. `###` stays inside its parent.

    Everything before the first `##` is kept as `(preamble)`: the canon's lead blockquote carries
    two cross-references, so dropping it would hide a transformation rather than check it.
    """
    out: dict[str, str] = {}
    current, body = "(preamble)", []
    text = re.sub(r"<!--.*?-->", "", text, flags=re.S)  # the mirror's own header is not prose
    for line in text.split("\n"):
        if line.startswith("## ") and not line.startswith("### "):
            out[current] = "\n".join(body).strip()
            current, body = line[3:].strip(), []
            continue
        body.append(line)
    out[current] = "\n".join(body).strip()
    return out


def normalise_headings(text: str) -> str:
    """The four rules the std-regex-reference header declares, and nothing else.

    Cross-references keep their LINK TEXT and lose their target: the canon points at Doxygen ids,
    the mirror at site paths, and the header says the target is re-pointed. The one /api member
    link is reduced to the member's own name on both sides -- a sentinel would stop checking that
    the RIGHT member is cited, which is the only thing left worth comparing there.
    """
    text = re.sub(r"\\ref (?:\w+::)*(\w+)\b", r"\1", text)            # \ref a::b::member -> member
    # The H1 anchor is translated, not dropped: `{#compat}` on the canon's heading is `(compat)=`
    # on the line before the mirror's. Reduce both spellings to the same token so the ID is still
    # compared -- deleting them on both sides would stop checking that they agree.
    text = re.sub(r"\s*\{#(\w+)\}", r" ANCHOR:\1", text)
    text = re.sub(r"\((\w+)\)=", r"ANCHOR:\1", text)
    text = re.sub(r'<a href="[^"]*">\s*<code>([^<]*)</code>\s*</a>', r"\1", text)
    text = re.sub(r'<a href="[^"]*">([^<]*)</a>', r"\1", text)
    text = re.sub(r"\[([^\]]*)\]\(@ref [^)]*\)", r"\1", text)         # [text](@ref x) -> text
    text = re.sub(r"\{(?:doc|ref)\}`([^<`]*?)\s*<[^>]*>`", r"\1", text)  # {doc}`text <p>` -> text
    # The anchor moves from a SUFFIX on the canon's heading to a line BEFORE the mirror's, so its
    # position cannot be compared -- only its presence and its id. Lift every anchor out and
    # re-emit it in one canonical place.
    anchors = sorted(re.findall(r"ANCHOR:(\w+)", text))
    text = re.sub(r"\s*ANCHOR:\w+", "", text)
    text = re.sub(r"\s+", " ", text).strip()
    return text + ("  [anchors: " + ",".join(anchors) + "]" if anchors else "")


def compare_headings(canon_text: str, mirror_text: str) -> tuple[list[str], list[str], list[str], int]:
    """Second pair: sections matched by `##` heading, one deliberately substituted."""
    canon, mirror = heading_sections(canon_text), heading_sections(mirror_text)
    sub_canon, sub_mirror = SUBSTITUTED_SECTION
    missing_sub = [s for s, d in ((sub_canon, canon), (sub_mirror, mirror)) if s not in d]
    canon_names = set(canon) - {sub_canon}
    mirror_names = set(mirror) - {sub_mirror}
    only_canon = sorted((canon_names - mirror_names) | {f"{sub_canon} (substituted heading)"
                                                        for _ in missing_sub[:1] if sub_canon in missing_sub})
    only_mirror = sorted((mirror_names - canon_names) | {f"{sub_mirror} (substituted heading)"
                                                         for _ in missing_sub[:1] if sub_mirror in missing_sub})
    shared = sorted(canon_names & mirror_names)
    differing = [n for n in shared
                 if normalise_headings(canon[n]) != normalise_headings(mirror[n])]
    return only_canon, only_mirror, differing, len(shared)


def first_divergence(a: str, b: str) -> str:
    """A short excerpt around the first differing character, for a message worth reading."""
    at = next((i for i in range(min(len(a), len(b))) if a[i] != b[i]), min(len(a), len(b)))
    lo = max(0, at - 50)
    return f"      canon : …{a[lo:at + 70]}\n      mirror: …{b[lo:at + 70]}"


#: The three site pages that DISTILL their canon instead of mirroring it. Their headers say so --
#: "linked, not copied", "distilled here, not copied" -- and go.md was measured: 0 of its canon's
#: 9 prose blocks appear verbatim, despite the page being 92% of the canon's size. Comparing their
#: prose would be wrong, not merely hard: a distillation is allowed to say less, and differently.
#: What IS checkable is that the canon each one names still exists.
DISTILLED = (
    (ROOT / "docs" / "site" / "drop-in" / "go.md", ROOT / "bindings" / "go" / "README.md"),
    (ROOT / "docs" / "site" / "drop-in" / "regex.md", ROOT / "bindings" / "rust" / "README.md"),
    (ROOT / "docs" / "site" / "drop-in" / "re2.md",
     ROOT / "include" / "real" / "compat" / "re2" / "re2.hpp"),
)


def check_distilled(*, quiet: bool = False) -> int:
    """A distillation's canon must still exist. That is all a machine can say about it."""
    missing = [(page, canon) for page, canon in DISTILLED if not canon.exists()]
    if missing:
        if not quiet:
            for page, canon in missing:
                print(f"check_doc_mirror: FAIL — {page.relative_to(ROOT)} names a canon that is "
                      f"gone: {canon.relative_to(ROOT)}")
        return 1
    if not quiet:
        print(f"check_doc_mirror: {len(DISTILLED)} distilled page(s) — canon path present; prose "
              f"deliberately NOT compared (they condense, they do not copy)")
    return 0


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


def run_headings(canon_text: str, mirror_text: str, *, quiet: bool = False) -> int:
    """The second pair's verdict, in the first pair's shape."""
    only_canon, only_mirror, differing, compared = compare_headings(canon_text, mirror_text)
    if not (only_canon or only_mirror or differing):
        if not quiet:
            print(f"check_doc_mirror: clean — {compared} section(s) compared, 1 substituted "
                  f"({SUBSTITUTED_SECTION[0]} → {SUBSTITUTED_SECTION[1]}, body not mirrored)")
        return 0
    if quiet:
        return 1
    print("check_doc_mirror: FAIL — the site page has drifted from its canon.")
    print(f"  canon:  {CANON_2.relative_to(ROOT)}")
    print(f"  mirror: {MIRROR_2.relative_to(ROOT)}")
    for name in only_canon:
        print(f"  MISSING FROM MIRROR: {name}")
    for name in only_mirror:
        print(f"  MISSING FROM CANON:  {name}")
    canon, mirror = heading_sections(canon_text), heading_sections(mirror_text)
    for name in differing:
        print(f"  DIFFERS: {name}")
        print(first_divergence(normalise_headings(canon[name]), normalise_headings(mirror[name])))
    print("  Edit the canon first, then re-mirror — never the site page alone. If the difference "
          "is intended, DECLARE it in the mirror's header before teaching this script about it.")
    return 1


def self_test_headings(canon_text: str, mirror_text: str) -> int:
    """Same injection, on the second pair's own splitter."""
    anchor = "## regex_replace"
    if anchor not in mirror_text:
        print("check_doc_mirror: SELF-TEST INCONCLUSIVE — the injection anchor is gone from "
              "std-regex-reference.md. Pick another heading.")
        return 1
    injected = mirror_text.replace(anchor, anchor + "\n\nInjected: a paragraph the canon lacks.\n", 1)
    if run_headings(canon_text, injected, quiet=True) == 0:
        print("check_doc_mirror: SELF-TEST FAILED — a paragraph added to std-regex-reference.md "
              "alone did NOT trip the comparison. The guard is blind on the second pair.")
        return 1
    print("check_doc_mirror: self-test OK — a mirror-only paragraph trips the second pair too.")
    return 0


def main(argv: list[str]) -> int:
    canon_text, mirror_text = CANON.read_text(), MIRROR.read_text()
    canon2_text, mirror2_text = CANON_2.read_text(), MIRROR_2.read_text()
    status = 0
    if "--self-test" in argv:
        status |= self_test(canon_text, mirror_text)
        status |= self_test_headings(canon2_text, mirror2_text)
    status |= run(canon_text, mirror_text)
    status |= run_headings(canon2_text, mirror2_text)
    status |= check_distilled()
    return status


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
