#!/usr/bin/env python3
"""Enforce the Doxygen comment-FORM convention across include/real/.

The convention, as stated by the project:

  * an OBJECT (function, enum) is documented with a ``/*! ... */`` block;
  * an ATTRIBUTE (member variable) is documented with a trailing ``//!<`` on its own
    declaration line -- *when the documentation fits on that line*.

The second half of that sentence is load-bearing, and its two exceptions are STRUCTURAL, not
stylistic -- line length is explicitly not a criterion here:

  * 277 attributes are declarations that do not end on the line Doxygen reports, because
    their initializer spans hundreds of lines (the generated ``code_range`` tables). A
    trailing comment has nowhere to attach.
  * 47 carry genuinely multi-line rationale -- up to 19 lines: the ``pattern_hints`` layout
    rule, the possessive linearity invariant, the fingerprint-not-pointer cache key.
    ``//!<`` is a single-line form; collapsing those paragraphs and measured tables into one
    line would destroy them.

Everything else is flagged, however long the resulting line is.

Entity kinds NOT checked: ``typedef`` (a ``using`` reads as an attribute, and 54 of them
are already trailing one-liners), plus ``define`` and ``friend`` (too few to have a form).

WHY THIS IS A GATE STEP AND NOT A ONE-OFF SCRIPT
------------------------------------------------
The Doxyfile carried ``WARN_IF_UNDOCUMENTED = YES`` for a long time while reporting clean,
because ``EXTRACT_ALL = YES`` silently neutralised it -- the gate measured nothing and read
as proof. A style normalised once and left unguarded decays exactly the same way, and the
decay is invisible. So the fixer and the checker are the same file: ``--fix`` applies the
convention, ``--check`` (the default) fails when anything drifts off it.

Member KINDS come from Doxygen's own XML, never from a regex over the source. Three
hand-rolled heuristics were written and discarded during this tree's documentation pass --
one counted locals as members, one had inverted brace tracking, one anchored on the include
path and silently dropped every warning Doxygen reports under a synthesized ``<name>``
pseudo-location. Doxygen already knows what each entity is; ask it.

Usage:
    python3 tools/check_doc_style.py                 # check, exit 1 on any violation
    python3 tools/check_doc_style.py --fix           # rewrite in place
    python3 tools/check_doc_style.py --stats         # form distribution, no verdict
    python3 tools/check_doc_style.py --only pike.hpp # restrict to matching paths
"""
from __future__ import annotations

import argparse
import glob
import os
import re
import sys
import xml.etree.ElementTree as ET
from collections import Counter, defaultdict

XML_DIR = "build/doc/xml"
ROOT = "include/real/"

# Headers emitted by tools/gen_unicode_*.py. --fix must never rewrite one: the byte-identity
# regen guards would fail, and the edit would be lost at the next regeneration anyway. These
# are still CHECKED -- a violation here is a real one, it just has to be fixed in the
# generator that emits the comment (see tools/REGEN.md).
GENERATED = re.compile(
    r"include/real/unicode/unicode_(props|fold|binprop|property|script|scx)\.hpp$"
)

# Deliberately NO column budget. uncrustify carries no `code_width`, and line length is
# explicitly not a criterion for this project -- a long trailing //!< is preferred over a
# leading block purely to keep the form uniform. The only reasons an attribute may keep a
# leading block are STRUCTURAL, and both are checked below:
#   * the declaration does not end on the reported line (a multi-line initializer, e.g. the
#     generated range tables) -- a trailing comment has nowhere to attach;
#   * the documentation is genuinely multi-line -- //!< is a single-line form, and collapsing
#     paragraphs, \param lists or measured tables into one line would destroy them.

# Kinds that must use a /*! ... */ block.
OBJECT_KINDS = {"function", "enum"}
# Kinds that prefer a trailing //!< when the text fits on one line.
ATTRIBUTE_KINDS = {"variable"}

# Lines that legitimately sit between a doc comment and the declaration Doxygen reports:
# a template header, an attribute, or a preprocessor guard around one.
INTERVENING = re.compile(
    r"^\s*(template\s*<|\[\[|__attribute__|#\s*(if|ifdef|ifndef|else|elif|endif)\b)"
)


def require_fresh_xml() -> None:
    """Refuse to run against XML older than the headers it describes.

    This guard exists because its absence produced a false clean. ``make doc-check`` runs the
    CI Doxygen inside Docker and does NOT refresh ``build/doc/xml``, so this script silently
    read an hours-old snapshot: every entry whose line number had drifted failed the shape
    check below and was skipped, and the run reported clean while real violations remained.
    Nothing was corrupted -- the rewrite verifies the comment shape at the reported line before
    touching it, so a stale entry is dropped rather than mangled -- but the verdict was
    worthless, which is the same failure mode as ``EXTRACT_ALL = YES`` neutralising
    ``WARN_IF_UNDOCUMENTED``. Refresh with ``doxygen Doxyfile`` (writes build/doc/xml), or pass
    ``--refresh``.
    """
    if not os.path.isdir(XML_DIR):
        sys.exit(f"{XML_DIR} not found -- run `doxygen Doxyfile` first, or pass --refresh.")
    xmls = glob.glob(os.path.join(XML_DIR, "*.xml"))
    if not xmls:
        sys.exit(f"{XML_DIR} holds no XML -- run `doxygen Doxyfile` first, or pass --refresh.")
    oldest_xml = min(os.path.getmtime(p) for p in xmls)
    stale = [
        h
        for h in glob.glob(os.path.join(ROOT, "**", "*.hpp"), recursive=True)
        if os.path.getmtime(h) > oldest_xml
    ]
    if stale:
        sys.exit(
            f"check_doc_style: the Doxygen XML in {XML_DIR} is OLDER than "
            f"{len(stale)} header(s), e.g. {stale[0]}.\n"
            "  Line numbers would not match the source, entries would be silently skipped, and "
            "the verdict would be a false clean.\n"
            "  Run `doxygen Doxyfile` (note: `make doc-check` runs in Docker and does not "
            "refresh this), or pass --refresh."
        )


def refresh_xml() -> None:
    """Run the local doxygen so the XML matches the working tree."""
    import subprocess

    print("check_doc_style: refreshing build/doc/xml (doxygen Doxyfile) ...")
    proc = subprocess.run(["doxygen", "Doxyfile"], capture_output=True, text=True)
    if proc.returncode != 0:
        sys.exit(f"doxygen failed:\n{proc.stderr[-2000:]}")


def xml_members(only: str | None) -> list[tuple[str, int, str, str]]:
    """Every documented member Doxygen found under include/real/, as (file, line, kind, name)."""
    out: list[tuple[str, int, str, str]] = []
    for path in glob.glob(os.path.join(XML_DIR, "*.xml")):
        try:
            root = ET.parse(path).getroot()
        except ET.ParseError:
            continue
        for md in root.iter("memberdef"):
            loc = md.find("location")
            if loc is None:
                continue
            f, line = loc.get("file"), loc.get("line")
            if not f or not line or ROOT not in f:
                continue
            if only and only not in f:
                continue
            name = md.findtext("name") or "?"
            out.append((f, int(line), md.get("kind") or "?", name))
    # One member can appear in several XML files (class + namespace views); dedupe.
    return sorted(set(out))


class Source:
    """Header files, read once and rewritten at most once."""

    def __init__(self) -> None:
        self.files: dict[str, list[str]] = {}
        self.dirty: set[str] = set()

    def lines(self, path: str) -> list[str] | None:
        if path not in self.files:
            try:
                with open(path, encoding="utf-8") as fh:
                    self.files[path] = fh.read().split("\n")
            except OSError:
                self.files[path] = None  # type: ignore[assignment]
        return self.files[path]

    def flush(self) -> list[str]:
        written = []
        for path in sorted(self.dirty):
            with open(path, "w", encoding="utf-8") as fh:
                fh.write("\n".join(self.files[path]))
            written.append(path)
        return written


def doc_block_above(lines: list[str], decl: int) -> tuple[int, int] | None:
    """The contiguous run of `//!` lines documenting the declaration at index `decl`.

    Returns (first, last) inclusive indices, or None when the declaration is not preceded by
    a `//!` run. Skips template/attribute/preprocessor lines, which Doxygen reports past.
    """
    j = decl - 1
    hops = 0
    while j >= 0 and hops < 8:
        stripped = lines[j].strip()
        if stripped.startswith("//!"):
            break
        if INTERVENING.match(lines[j]):
            j -= 1
            hops += 1
            continue
        return None
    else:
        return None
    if j < 0:
        return None
    last = j
    first = j
    while first - 1 >= 0 and lines[first - 1].strip().startswith("//!"):
        first -= 1
    return first, last


def classify(lines: list[str], decl: int) -> str:
    """The comment form attached to the declaration at index `decl`."""
    if "//!<" in lines[decl]:
        return "trailing"
    if doc_block_above(lines, decl) is not None:
        return "slash_bang"
    j = decl - 1
    hops = 0
    while j >= 0 and hops < 8:
        if INTERVENING.match(lines[j]):
            j -= 1
            hops += 1
            continue
        break
    if j >= 0 and lines[j].strip().endswith("*/"):
        return "block"
    return "other"


def indent_of(line: str) -> str:
    return line[: len(line) - len(line.lstrip())]


def trailing_candidate(lines: list[str], decl: int, first: int, last: int) -> str | None:
    """The one-line `//!<` form of a single-line `//!` block, or None when it cannot be used."""
    if first != last:
        return None  # multi-line rationale: the block stays
    decl_line = lines[decl]
    # A declaration that already carries a comment, or that does not end here, is left alone.
    if "//" in decl_line or "/*" in decl_line:
        return None
    if not decl_line.rstrip().endswith(";"):
        return None
    text = re.sub(r"^\s*//!\s*(\\brief\s+)?", "", lines[last]).strip()
    if not text:
        return None
    return decl_line.rstrip() + " //!< " + text


def to_block(lines: list[str], first: int, last: int) -> list[str]:
    """Rewrite a `//!` run as a `/*! ... */` block, preserving every line verbatim.

    `//!` becomes ` *` at the same indent, which lands the text one column left and keeps the
    run's internal alignment intact -- the same geometry the blocks already in this tree use.
    """
    indent = indent_of(lines[first])
    out = [f"{indent}/*!"]
    for k in range(first, last + 1):
        rest = lines[k].strip()[3:]  # everything after `//!`
        out.append(f"{indent} *{rest}".rstrip())
    out.append(f"{indent} */")
    return out


def orphan_blocks(only: str | None) -> list[tuple[str, int, int]]:
    """Every ``/*! ... */`` block carrying more than one ``\\brief``, as (file, line, count).

    Two ``\\brief`` in one block is the mechanical signature of an ORPHANED doc block: a comment
    whose function was renamed or deleted, which then collapsed onto the next declaration and now
    describes something it does not document. Three were found by hand in this tree (dfa.hpp's
    \\throws contract sitting on a size cap, and the two emit_klass blocks in compiler.hpp), plus
    two more later -- pike.hpp still carried the block of a removed ``ensure_search_dfas``, and
    onepass.hpp described an epoch counter that a perf commit had deleted.

    It also catches the inverse mistake, which is how eleven of the thirteen got here: appending a
    fresh ``\\brief``+``\\return`` run to a function that already had a description, instead of
    adding only the missing ``\\return`` to the block already there.

    Doxygen NEVER warns about this -- it silently takes one of the two and renders it -- so the
    defect is invisible to every other check in this repository. It is trivially detectable, which
    is the whole argument for checking it here rather than re-auditing by eye.
    """
    found: list[tuple[str, int, int]] = []
    for path in sorted(glob.glob(os.path.join(ROOT, "**", "*.hpp"), recursive=True)):
        if only and only not in path:
            continue
        with open(path, encoding="utf-8") as fh:
            lines = fh.read().split("\n")
        i = 0
        while i < len(lines):
            if not lines[i].strip().startswith("/*!"):
                i += 1
                continue
            j = i
            while j < len(lines) and not lines[j].strip().endswith("*/"):
                j += 1
            body = "\n".join(lines[i : j + 1])
            count = len(re.findall(r"\\brief\b", body))
            if count > 1:
                found.append((path, i + 1, count))
            i = j + 1
    return found


def adjacent_blocks(only: str | None) -> list[tuple[str, int, str]]:
    """Doc comments that sit back to back with NO declaration between them, as (file, line, brief).

    The orphaned-block defect in its second shape, which \\ref orphan_blocks cannot see: rather than two
    ``\\brief`` inside one block, two SEPARATE blocks stack up and only the last one describes the
    declaration below. Doxygen concatenates them silently, so the rendered entity carries a paragraph
    about something else entirely -- eight were found this way, six of them dead text (a stranded
    ``regex_error`` description sitting on ``error_kind``, a ``program_view`` description on a forward
    declaration, a ``slot_storage`` description on a byte-class table) and two a single comment
    needlessly split into brief-then-params.

    All eight came from the same slip, made repeatedly during the completeness pass: appending a NEW
    block to an entity that already had one, instead of extending the block already there.
    """
    found: list[tuple[str, int, str]] = []
    for path in sorted(glob.glob(os.path.join(ROOT, "**", "*.hpp"), recursive=True)):
        if only and only not in path:
            continue
        with open(path, encoding="utf-8") as fh:
            lines = fh.read().split("\n")
        i = 0
        while i < len(lines):
            if not lines[i].strip().startswith("/*!"):
                i += 1
                continue
            j = i
            while j < len(lines) and not lines[j].strip().endswith("*/"):
                j += 1
            k = j + 1
            while k < len(lines) and not lines[k].strip():
                k += 1
            if k < len(lines) and lines[k].strip().startswith(("/*!", "//!")):
                brief = lines[i + 1].strip()[1:].strip() if i + 1 < len(lines) else ""
                found.append((path, i + 1, brief[:60]))
            i = j + 1
    return found


SENTENCE_END = re.compile(r"[.:;!?)\]`\"—]\s*$")


def split_blocks(only: str | None) -> list[tuple[str, int, str]]:
    """Every ``/*! ... */`` block whose last text line stops mid-sentence, as (file, line, tail).

    The signature of a doc comment CUT IN HALF. Four were produced in this tree by this very script,
    before the guards above existed: reading a stale XML put the reported declaration inside a ``//!``
    run, so only a prefix of the run was wrapped in the block and the remainder was left dangling after
    the ``*/`` (or swept into the member's trailing ``//!<``). Doxygen renders the truncated half without
    complaint -- the block is well formed, it just stops in the middle of a sentence -- so nothing else
    in this repository could see it.

    A heuristic, unlike the other checks here, so it can be wrong in one direction: a block legitimately
    ending on a word (a bare identifier, a table row) reads as suspicious. It has no false NEGATIVES for
    the damage it targets, which is the direction that matters; a false positive is fixed by ending the
    sentence.
    """
    found: list[tuple[str, int, str]] = []
    for path in sorted(glob.glob(os.path.join(ROOT, "**", "*.hpp"), recursive=True)):
        if only and only not in path:
            continue
        with open(path, encoding="utf-8") as fh:
            lines = fh.read().split("\n")
        i = 0
        while i < len(lines):
            if not lines[i].strip().startswith("/*!"):
                i += 1
                continue
            j = i
            while j < len(lines) and not lines[j].strip().endswith("*/"):
                j += 1
            text = [l.strip()[1:].strip() for l in lines[i + 1 : j] if l.strip().startswith("*")]
            text = [t for t in text if t]
            if text and not SENTENCE_END.search(text[-1]):
                found.append((path, i + 1, text[-1][-60:]))
            i = j + 1
    return found


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--fix", action="store_true", help="rewrite violations in place")
    ap.add_argument("--stats", action="store_true", help="print the form distribution and exit 0")
    ap.add_argument("--only", metavar="SUBSTR", help="restrict to files whose path contains SUBSTR")
    ap.add_argument("--refresh", action="store_true", help="run `doxygen Doxyfile` first")
    args = ap.parse_args()

    if args.refresh:
        refresh_xml()
    require_fresh_xml()

    members = xml_members(args.only)
    src = Source()

    forms: dict[str, Counter] = defaultdict(Counter)
    # violations: path -> list of (decl_index, kind, name, first, last, trailing_or_None)
    todo: dict[str, list[tuple[int, str, str, int, int, str | None]]] = defaultdict(list)
    allowed_block = 0

    for path, line, kind, name in members:
        lines = src.lines(path)
        if lines is None or line - 1 >= len(lines):
            continue
        decl = line - 1
        # A reported "declaration" that is itself a comment line means the XML does not describe this
        # source. The staleness guard above should have caught it, but this is the invariant that
        # actually matters, so it is asserted at the point of use: when it was absent, a stale XML put
        # `decl` INSIDE a //! run, so doc_block_above returned a PREFIX of the run and to_block wrapped
        # only that prefix -- leaving the tail as //! lines after the */ and cutting four doc comments
        # mid-sentence (lazy_dfa's byte-program cap, onepass's il_warm_floor, pike's cp_hi_cache_entry,
        # compiler's fold_val_). Silent damage: Doxygen sees a well-formed block and says nothing.
        stripped = lines[decl].strip()
        if stripped.startswith(("//", "/*", "*")) or stripped == "*/":
            sys.exit(
                f"check_doc_style: {path}:{line} is reported as {kind} '{name}' but that line is a "
                f"COMMENT:\n    {lines[decl]}\n"
                "  The XML does not match this source. Refusing to continue -- rewriting from it would "
                "split doc comments. Run `doxygen Doxyfile`, or pass --refresh."
            )
        form = classify(lines, decl)
        forms[kind][form] += 1
        if form != "slash_bang":
            continue
        span = doc_block_above(lines, decl)
        if span is None:
            continue
        first, last = span
        if kind in ATTRIBUTE_KINDS:
            cand = trailing_candidate(lines, decl, first, last)
            if cand is None:
                allowed_block += 1  # multi-line or overlong: the convention permits the block
                continue
            todo[path].append((decl, kind, name, first, last, cand))
        elif kind in OBJECT_KINDS:
            todo[path].append((decl, kind, name, first, last, None))

    if args.stats:
        cols = ["block", "slash_bang", "trailing", "other"]
        print(f"{'kind':<10}" + "".join(f"{c:>13}" for c in cols))
        for kind in sorted(forms, key=lambda k: -sum(forms[k].values())):
            print(f"{kind:<10}" + "".join(f"{forms[kind][c]:>13}" for c in cols))
        print(f"\nattributes keeping a leading block by the convention: {allowed_block}")
        return 0

    # Orphaned blocks are never auto-fixed: choosing which of two \brief survives is a judgment
    # about which declaration the text belongs to, and guessing would silently delete real prose.
    orphans = orphan_blocks(args.only)
    for path, line, count in orphans:
        print(f"{path}:{line}: doc block carries {count} \\brief -- an orphaned or double-documented block")
    splits = split_blocks(args.only)
    for path, line, tail in splits:
        print(f"{path}:{line}: doc block stops mid-sentence (...{tail}) -- a split comment?")
    adjacent = adjacent_blocks(args.only)
    for path, line, brief in adjacent:
        print(f"{path}:{line}: doc block followed by another with no declaration between ({brief}) -- "
              "one of them documents nothing")
    n_prose = len(orphans) + len(splits) + len(adjacent)

    total = sum(len(v) for v in todo.values())
    if not total and not n_prose:
        print(
            f"check_doc_style: clean -- objects use /*! */, attributes use //!< where it fits "
            f"({allowed_block} attribute(s) keep a leading block, as the convention allows)"
        )
        return 0
    if not total:
        parts = []
        if orphans:
            parts.append(f"{len(orphans)} block(s) with more than one \\brief")
        if splits:
            parts.append(f"{len(splits)} block(s) stopping mid-sentence")
        if adjacent:
            parts.append(f"{len(adjacent)} stacked block pair(s)")
        print(
            f"\ncheck_doc_style: FAILED -- {', '.join(parts)}. Doxygen warns about none of these: it renders "
            "one \\brief and silently drops the rest, and a truncated or stacked block is still well formed. "
            "Each needs a human -- deciding which declaration a paragraph belongs to is a judgment, and "
            "guessing would delete real prose."
        )
        return 1

    generated = {p: items for p, items in todo.items() if GENERATED.search(p)}
    n_generated = sum(len(v) for v in generated.values())

    if not args.fix:
        by_kind = Counter(k for items in todo.values() for _, k, _, _, _, _ in items)
        for path in sorted(todo):
            tag = "  [GENERATED: fix in tools/gen_*.py]" if GENERATED.search(path) else ""
            for decl, kind, name, _, _, cand in sorted(todo[path]):
                want = "trailing //!<" if cand else "/*! */ block"
                print(f"{path}:{decl + 1}: {kind} '{name}' should use {want}{tag}")
        print(
            f"\ncheck_doc_style: FAILED -- {total} member(s) off the comment-form convention "
            f"({dict(by_kind)}). Run `python3 tools/check_doc_style.py --fix`, then `make format`."
        )
        if n_generated:
            print(
                f"  {n_generated} of them are in GENERATED headers: --fix skips those; edit the "
                f"emitting tools/gen_unicode_*.py and regenerate (tools/REGEN.md)."
            )
        if n_prose:
            print(
                f"  plus {n_prose} doc block(s) with a prose defect (double \\brief or split text), listed "
                "above -- those are NOT auto-fixable and need a human."
            )
        return 1

    for path in generated:
        del todo[path]
    total -= n_generated
    if n_generated:
        print(
            f"check_doc_style: skipping {n_generated} violation(s) in "
            f"{len(generated)} generated header(s) -- fix those in tools/gen_unicode_*.py."
        )
    if not total:
        print("check_doc_style: nothing left to rewrite in hand-written headers.")
        return 0

    # Apply per file, bottom-up, so earlier line indices stay valid.
    for path, items in todo.items():
        lines = src.lines(path)
        for decl, _kind, _name, first, last, cand in sorted(items, reverse=True):
            if cand is not None:
                lines[decl] = cand
                del lines[first : last + 1]
            else:
                lines[first : last + 1] = to_block(lines, first, last)
        src.dirty.add(path)

    written = src.flush()
    print(f"check_doc_style: rewrote {total} member(s) across {len(written)} file(s)")
    for path in written:
        print(f"  {path}")
    print("Now run `make format`, then `make doc-check`.")
    if n_prose:
        print(
            f"NOT fixed: {n_prose} doc block(s) with a prose defect (double \\brief or split text), listed "
            "above -- those need a human. Re-run after fixing them."
        )
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
