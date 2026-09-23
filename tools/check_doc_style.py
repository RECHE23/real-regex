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

A red on FRESHNESS is not a verdict on content. When ``build/doc/xml`` is older than a
header this check refuses before reading a byte of it -- a stale tree would turn skipped
entries into a false clean. So a freshness failure means the comments were not judged at
all: refresh (``make doc-xml``) and re-run before concluding anything about the prose.

Usage:
    python3 tools/check_doc_style.py                 # check, exit 1 on any violation
    python3 tools/check_doc_style.py --fix           # rewrite in place
    python3 tools/check_doc_style.py --stats         # form distribution, no verdict
    python3 tools/check_doc_style.py --only pike.hpp # restrict to matching paths
    python3 tools/check_doc_style.py --self-test     # drive each arm on synthetic repositories
"""
from __future__ import annotations

import argparse
import contextlib
import glob
import io
import os
import re
import sys
import tempfile
import time
import xml.etree.ElementTree as ET
from collections import Counter, defaultdict

# Every path below is repository-relative, so the process runs FROM the repository: invoked from
# docs/ with a relative ROOT, the glob matched nothing and the verdict was a vacuous "clean" --
# the false clean this file exists to prevent, arriving through the working directory.
os.chdir(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

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
    # When Doxygen last RAN, not the oldest file it left behind: it rewrites only the XML whose
    # content changed, so most files keep an old mtime after a clean run and `min()` here reported
    # every header as stale forever -- a false STALE, the mirror of the false clean this guard was
    # added to prevent, and just as useless. index.xml is regenerated on every run, so it dates the
    # run; max() over the directory is the fallback if a future Doxygen stops rewriting it.
    index_xml = os.path.join(XML_DIR, "index.xml")
    run_time = (
        os.path.getmtime(index_xml)
        if os.path.isfile(index_xml)
        else max(os.path.getmtime(p) for p in xmls)
    )
    stale = [
        h
        for h in glob.glob(os.path.join(ROOT, "**", "*.hpp"), recursive=True)
        if os.path.getmtime(h) > run_time
    ]
    if stale:
        sys.exit(
            f"check_doc_style: the Doxygen XML in {XML_DIR} is OLDER than "
            f"{len(stale)} header(s), e.g. {stale[0]}.\n"
            "  Line numbers would not match the source, entries would be silently skipped, and "
            "the verdict would be a false clean.\n"
            "  This check reads the DEVELOPER tree (Doxyfile -> build/doc/xml). "
            "`make doc-site-xml` only refreshes build/doc/xml-site -- a different "
            "profile, for Breathe / check-doc-voice. Refresh this one with "
            "`doxygen Doxyfile` or `make doc-xml` (both trees), or pass --refresh.\n"
            "  `make doc-check` runs in Docker and does not refresh this."
        )


def refresh_xml(xml_dir: str = XML_DIR, run=None) -> None:
    """Regenerate the XML from scratch so it matches the working tree.

    The directory is REMOVED first, because Doxygen's XML output is incremental: it leaves a
    per-file XML untouched when it decides the compound is unchanged. A refresh that only re-ran
    doxygen could therefore hand this script line numbers from a previous revision of a header it
    had just edited -- which is how it came to report a declaration sitting on a comment line.
    Deleting first costs a few seconds and makes `--refresh` mean what it says.
    """
    import shutil
    import subprocess

    run = run or subprocess.run
    print("check_doc_style: refreshing build/doc/xml (clean doxygen Doxyfile) ...")
    shutil.rmtree(xml_dir, ignore_errors=True)
    proc = run(["doxygen", "Doxyfile"], capture_output=True, text=True)
    if proc.returncode != 0:
        sys.exit(f"doxygen failed:\n{proc.stderr[-2000:]}")


def xml_named_headers(xml_dir: str) -> set[str]:
    """include/real/*.hpp paths the XML location tags actually name."""
    got: set[str] = set()
    for path in glob.glob(os.path.join(xml_dir, "*.xml")):
        try:
            root = ET.parse(path).getroot()
        except ET.ParseError:
            continue
        for loc in root.iter("location"):
            f = (loc.get("file") or "").replace("\\", "/")
            idx = f.find("include/real/")
            if idx >= 0 and f.endswith(".hpp"):
                got.add(f[idx:])
    return got


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
        return None  # j ran below 0 (or past eight hops) without meeting a //! line
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
            stripped = lines[i].strip()
            if stripped.startswith("//!") and not stripped.startswith("//!<"):
                j = i  # a `//!` run: consume it, then look at what follows
                while j + 1 < len(lines) and lines[j + 1].strip().startswith("//!") \
                        and not lines[j + 1].strip().startswith("//!<"):
                    j += 1
                k = j + 1
                while k < len(lines) and not lines[k].strip():
                    k += 1
                if k < len(lines) and lines[k].strip().startswith("/*!"):
                    found.append((path, i + 1, stripped[3:].strip()[:60]))
                i = j + 1
                continue
            if not stripped.startswith("/*!"):
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


# A compound declaration: these are Doxygen COMPOUNDS, not members, so `memberdef` kinds never
# describe them and OBJECT_KINDS cannot reach them. They are objects by the project's convention.
COMPOUND = re.compile(r"^\s*(template\s*<.*>\s*)?(struct|class|enum(\s+class)?|union|namespace)\s+\w+")
# A line that continues into the declaration below it rather than ending a statement.
DECL_FRAGMENT = re.compile(r"^\s*(template\s*<|\[\[)")


def compound_attribute_form(only: str | None) -> list[tuple[str, int, str]]:
    """Compounds documented with a leading ``//!`` run -- the attribute form on an object.

    Two blind spots meet here. The kinds this script reads come from ``memberdef`` entries, and a
    struct, class, enum, union or namespace is a ``compounddef``: no member kind ever describes one,
    so OBJECT_KINDS cannot reach it. And any declaration behind ``#if defined(__ARM_NEON)``,
    ``__SSE2__`` or ``__GNUC__ && !__clang__`` is absent from the XML entirely, because the Doxyfile
    predefines no vector ISA and no compiler macro. Both are checked from the SOURCE here, which needs
    neither. Whole files drifted through the gap: simd.hpp's primitives, both cpclass_gcc splices, and
    six structs in ast.hpp.
    """
    found: list[tuple[str, int, str]] = []
    for path in sorted(glob.glob(os.path.join(ROOT, "**", "*.hpp"), recursive=True)):
        if only and only not in path:
            continue
        with open(path, encoding="utf-8") as fh:
            lines = fh.read().split("\n")
        for i, line in enumerate(lines):
            if not COMPOUND.match(line) or line.rstrip().endswith(";"):
                continue  # a forward declaration ends in `;` and carries no body to document
            j = i - 1
            while j >= 0 and (not lines[j].strip() or DECL_FRAGMENT.match(lines[j])
                              or lines[j].lstrip().startswith("#")):
                j -= 1
            if j < 0:
                continue
            prev = lines[j].strip()
            if prev.startswith("//!") and not prev.startswith("//!<"):
                found.append((path, j + 1, line.strip()[:60]))
    return found


def wedged_blocks(only: str | None) -> list[tuple[str, int, str]]:
    """Doc blocks sitting BETWEEN a declaration's own lines, as (file, line, fragment).

    A ``/*! */`` block whose preceding line is a declaration FRAGMENT -- a template header, or a
    return type on its own line -- splits the declaration in half. Doxygen still attaches the block to
    what follows, so nothing warns; the source is what suffers, and so does anyone editing the
    signature. Three shapes of it were found in one pass: a block between ``[[nodiscard]] inline
    <return type>`` and the function name, and two between ``template <typename OutSlots>`` and the
    declaration it parameterises.
    """
    found: list[tuple[str, int, str]] = []
    for path in sorted(glob.glob(os.path.join(ROOT, "**", "*.hpp"), recursive=True)):
        if only and only not in path:
            continue
        with open(path, encoding="utf-8") as fh:
            lines = fh.read().split("\n")
        for i, line in enumerate(lines):
            if not line.strip().startswith("/*!"):
                continue
            j = i - 1
            while j >= 0 and not lines[j].strip():
                j -= 1
            if j < 0:
                continue
            prev = lines[j].strip()
            if prev.startswith(("//", "*", "/*", "#")):
                continue
            # A trailing `//!<` (or any line comment) hides the code's own punctuation: strip it before
            # asking whether the statement ended, or every documented attribute reads as a fragment.
            code = re.split(r"//", prev, maxsplit=1)[0].rstrip()
            if not code or code.endswith((";", "{", "}", ")", ",", ":")):
                continue
            found.append((path, j + 1, code[:60]))
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


def judge(fix: bool = False, stats: bool = False, only: str | None = None) -> int:
    """Checks (or, with ``fix``, rewrites) the repository in the current directory."""
    require_fresh_xml()

    members = xml_members(only)
    exp = {
        p.replace("\\", "/")
        for p in glob.glob(os.path.join(ROOT, "**", "*.hpp"), recursive=True)
        if not only or only in p
    }
    got = xml_named_headers(XML_DIR)
    if only:
        got = {p for p in got if only in p}
    if not exp:
        print(
            f"check_doc_style: FAIL -- no headers under {ROOT}"
            + (f" matching --only {only}" if only else "")
        )
        return 1
    if got != exp or not members:
        missing = sorted(exp - got)
        extra = sorted(got - exp)
        print(
            f"check_doc_style: FAIL -- {len(got)} of {len(exp)} header(s) in XML, "
            f"{len(members)} member(s)"
        )
        if missing:
            print("  missing from XML: " + ", ".join(missing))
        if extra:
            print("  extra in XML: " + ", ".join(extra))
        if not members:
            print("  XML yielded no members")
        return 1
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
        first, last = doc_block_above(lines, decl)  # never None: classify() asked the same question
        if kind in ATTRIBUTE_KINDS:
            cand = trailing_candidate(lines, decl, first, last)
            if cand is None:
                allowed_block += 1  # multi-line or overlong: the convention permits the block
                continue
            todo[path].append((decl, kind, name, first, last, cand))
        elif kind in OBJECT_KINDS:
            todo[path].append((decl, kind, name, first, last, None))

    if stats:
        cols = ["block", "slash_bang", "trailing", "other"]
        print(f"{'kind':<10}" + "".join(f"{c:>13}" for c in cols))
        for kind in sorted(forms, key=lambda k: -sum(forms[k].values())):
            print(f"{kind:<10}" + "".join(f"{forms[kind][c]:>13}" for c in cols))
        print(f"\nattributes keeping a leading block by the convention: {allowed_block}")
        return 0

    # Orphaned blocks are never auto-fixed: choosing which of two \brief survives is a judgment
    # about which declaration the text belongs to, and guessing would silently delete real prose.
    orphans = orphan_blocks(only)
    for path, line, count in orphans:
        print(f"{path}:{line}: doc block carries {count} \\brief -- an orphaned or double-documented block")
    splits = split_blocks(only)
    for path, line, tail in splits:
        print(f"{path}:{line}: doc block stops mid-sentence (...{tail}) -- a split comment?")
    adjacent = adjacent_blocks(only)
    for path, line, brief in adjacent:
        print(f"{path}:{line}: doc block followed by another with no declaration between ({brief}) -- "
              "one of them documents nothing")
    compounds = compound_attribute_form(only)
    for path, line, decl in compounds:
        print(f"{path}:{line}: `//!` run on a compound ({decl}) -- an object takes a /*! */ block")
    wedged = wedged_blocks(only)
    for path, line, frag in wedged:
        print(f"{path}:{line}: declaration split by its own doc block (after `{frag}`) -- "
              "the block belongs above the whole declaration")
    n_prose = len(orphans) + len(splits) + len(adjacent) + len(compounds) + len(wedged)

    total = sum(len(v) for v in todo.values())
    if not total and not n_prose:
        print(
            f"check_doc_style: clean -- {len(members)} member(s) in {len(got)} of "
            f"{len(exp)} header(s); objects use /*! */, attributes use //!< where it fits "
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
        if compounds:
            parts.append(f"{len(compounds)} compound(s) in the attribute form")
        if wedged:
            parts.append(f"{len(wedged)} declaration(s) split by their own doc block")
        print(
            f"\ncheck_doc_style: FAILED -- {', '.join(parts)}. Doxygen warns about none of these: it renders "
            "one \\brief and silently drops the rest, and a truncated or stacked block is still well formed. "
            "Each needs a human -- deciding which declaration a paragraph belongs to is a judgment, and "
            "guessing would delete real prose."
        )
        return 1

    generated = {p: items for p, items in todo.items() if GENERATED.search(p)}
    n_generated = sum(len(v) for v in generated.values())

    if not fix:
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


def _self_test_pure() -> list[str]:
    """The line-level helpers, on literal lines."""
    fails: list[str] = []

    def expect(name, got, want):
        if got != want:
            fails.append(f"{name}: got {got!r}, want {want!r}")

    expect("classify: a trailing //!< on the declaration", classify(["int x; //!< doc"], 0), "trailing")
    expect("classify: a //! run above", classify(["//! doc", "int x;"], 1), "slash_bang")
    expect("classify: a //! run above a template header", classify(["//! doc", "template <typename T>", "T f();"], 2),
           "slash_bang")
    expect("classify: a /*! */ block above", classify(["/*!", " * doc", " */", "void f();"], 3), "block")
    expect("classify: a /*! */ block above a template header",
           classify(["/*!", " * doc", " */", "template <typename T>", "T f();"], 4), "block")
    expect("classify: nothing above", classify(["int y;", "int x;"], 1), "other")
    expect("doc_block_above: the run's span", doc_block_above(["//! a", "//! b", "int x;"], 2), (0, 1))
    expect("doc_block_above: code between", doc_block_above(["//! a", "int y;", "int x;"], 2), None)
    expect("doc_block_above: more than eight intervening lines",
           doc_block_above(["//! a"] + ["#if X"] * 9 + ["int x;"], 10), None)
    expect("trailing_candidate: one line", trailing_candidate(["//! \\brief The count.", "int n;"], 1, 0, 0),
           "int n; //!< The count.")
    expect("trailing_candidate: multi-line rationale", trailing_candidate(["//! a", "//! b", "int n;"], 2, 0, 1), None)
    expect("trailing_candidate: the declaration already has a comment",
           trailing_candidate(["//! a", "int n /* legacy */;"], 1, 0, 0), None)
    expect("trailing_candidate: the declaration does not end here",
           trailing_candidate(["//! a", "int n = {"], 1, 0, 0), None)
    expect("trailing_candidate: an empty doc line", trailing_candidate(["//!", "int n;"], 1, 0, 0), None)
    expect("to_block: every line kept, at the same indent",
           to_block(["  //! \\brief F.", "  //! More."], 0, 1), ["  /*!", "   * \\brief F.", "   * More.", "   */"])
    return fails


def _self_test_scanners() -> list[str]:
    """Each prose-defect scanner on a synthetic include/real/ tree: its defect found, its clean form not."""
    fails: list[str] = []
    cases = [
        (orphan_blocks, "/*!\n * \\brief A.\n * \\brief B.\n */\nvoid f();\n", True, "two \\brief"),
        (orphan_blocks, "/*!\n * \\brief A.\n */\nvoid f();\n", False, "one \\brief"),
        # A //! line is not the start of a block: its \brief and the block's are in different comments.
        (orphan_blocks, "//! \\brief A.\n/*!\n * \\brief B.\n */\nvoid f();\n", False, "a //! run above a block"),
        (split_blocks, "/*!\n * \\brief Stops mid\n */\nvoid f();\n", True, "a block ending on a word"),
        (split_blocks, "/*!\n * \\brief Ends.\n */\nvoid f();\n", False, "a finished sentence"),
        (split_blocks, "/* a plain comment\n * that stops mid\n */\nvoid f();\n", False,
         "a plain /* */ comment is not a doc block"),
        (adjacent_blocks, "//! Stranded.\n/*!\n * \\brief F.\n */\nvoid f();\n", True, "a //! run then a block"),
        (adjacent_blocks, "/*!\n * \\brief A.\n */\n\n/*!\n * \\brief F.\n */\nvoid f();\n", True,
         "two blocks stacked"),
        (adjacent_blocks, "/*!\n * \\brief F.\n */\nvoid f();\n", False, "a block then its declaration"),
        # A code line that merely ENDS in `*/` is not a block, so the block below it is not "stacked".
        (adjacent_blocks, "int a; /* note */\n/*!\n * \\brief F.\n */\nvoid f();\n", False,
         "a block after code ending in a comment"),
        (compound_attribute_form, "//! A struct.\nstruct S {\n};\n", True, "a //! run on a struct"),
        (compound_attribute_form, "//! Forward.\nstruct S;\n", False, "a forward declaration"),
        (compound_attribute_form, "/*!\n * \\brief S.\n */\nstruct S {\n};\n", False, "a block on a struct"),
        # A compound on the first line has nothing above it; the LAST line of the file is not "above".
        (compound_attribute_form, "struct S {\n};\n//! trailing note", False, "a compound on the first line"),
        (wedged_blocks, "template <typename T>\n/*!\n * \\brief F.\n */\nT f();\n", True,
         "a block after a template header"),
        (wedged_blocks, "int g(); //!< g\n/*!\n * \\brief F.\n */\nvoid f();\n", False,
         "a block after a finished statement"),
        (wedged_blocks, "template <typename T>\nT f();\n", False, "a template header and its declaration, no block"),
        (wedged_blocks, "#endif\n/*!\n * \\brief F.\n */\nvoid f();\n", False,
         "a block after a preprocessor line"),
        (wedged_blocks, "/*!\n * \\brief F.\n */\nvoid f();\nint x", False, "a block on the first line"),
    ]
    previous = os.getcwd()
    try:
        for scanner, text, flagged, name in cases:
            with tempfile.TemporaryDirectory() as tmp:
                os.chdir(tmp)
                os.makedirs(ROOT)
                with open(os.path.join(ROOT, "a.hpp"), "w", encoding="utf-8") as fh:
                    fh.write(text)
                try:
                    found = scanner(None)
                except Exception as exc:
                    fails.append(f"{scanner.__name__}, {name}: raised {type(exc).__name__}: {exc}")
                    continue
                finally:
                    os.chdir(previous)
                if bool(found) != flagged:
                    fails.append(f"{scanner.__name__}, {name}: {'missed' if flagged else 'flagged'} ({found})")
    finally:
        os.chdir(previous)
    return fails


def _self_test_judge() -> list[str]:
    """``judge`` on synthetic repositories: freshness, perimeter, each violation, --fix and --stats."""
    fails: list[str] = []
    obj = "//! \\brief Does it.\nvoid f();\n"          # an object in the attribute form
    attr = "struct S {\n  //! The count.\n  int n;\n};\n"  # an attribute that fits on one line
    long_attr = "struct S {\n  //! First line of the rationale.\n  //! Second line.\n  int n;\n};\n"
    # One of each prose defect, for the --only case: a double \brief, a sentence cut mid-word, a stacked
    # pair, the attribute form on a compound, and a block wedged after a template header.
    prose_defects = ("/*!\n * \\brief A.\n * \\brief B.\n */\nvoid g();\n"
                     "/*!\n * \\brief Stops mid\n */\nvoid h();\n"
                     "//! Stranded.\n/*!\n * \\brief K.\n */\nvoid k();\n"
                     "//! A struct.\nstruct T {\n};\n"
                     "template <typename U>\n/*!\n * \\brief W.\n */\nU w();\n")

    def member(line, kind, name, path="include/real/a.hpp"):
        return (f'<memberdef kind="{kind}"><name>{name}</name>'
                f'<location file="{path}" line="{line}"/></memberdef>')

    def repo(tmp, header=None, members=(), xml=True, has_xml=True, stale=False, extra_headers=(), path="a.hpp",
             outside=None):
        os.makedirs(os.path.join(tmp, ROOT, os.path.dirname(path)), exist_ok=True)
        if header is not None:
            with open(os.path.join(tmp, ROOT, path), "w", encoding="utf-8") as fh:
                fh.write(header)
        for extra in extra_headers:
            with open(os.path.join(tmp, ROOT, extra), "w", encoding="utf-8") as fh:
                fh.write("int z;\n")
        if outside is not None:  # a header OUTSIDE include/real/, which the check must never judge
            os.makedirs(os.path.join(tmp, "other"))
            with open(os.path.join(tmp, "other", "x.hpp"), "w", encoding="utf-8") as fh:
                fh.write(outside)
        if not xml:
            return
        os.makedirs(os.path.join(tmp, XML_DIR))
        if not has_xml:
            return
        with open(os.path.join(tmp, XML_DIR, "a.xml"), "w", encoding="utf-8") as fh:
            fh.write("<doxygen><compounddef>" + "".join(members) + "</compounddef></doxygen>")
        with open(os.path.join(tmp, XML_DIR, "index.xml"), "w", encoding="utf-8") as fh:
            fh.write("<doxygenindex/>")
        if stale:
            past = time.time() - 100
            os.utime(os.path.join(tmp, XML_DIR, "index.xml"), (past, past))

    cases = [
        # (name, repo kwargs, judge kwargs, want rc, marker, file check)
        ("no XML directory", dict(header="int z;\n", xml=False), {}, 1, "not found -- run `doxygen Doxyfile`", None),
        ("an XML directory with no XML", dict(header="int z;\n", has_xml=False), {}, 1, "holds no XML", None),
        ("a header newer than the XML", dict(header=obj, members=[member(2, "function", "f")], stale=True), {}, 1,
         "is OLDER than", None),
        ("no headers under include/real/", dict(members=[member(2, "function", "f")]), {}, 1, "no headers under",
         None),
        ("a header the XML never names", dict(header=obj, members=[member(2, "function", "f")],
                                              extra_headers=("b.hpp",)), {}, 1, "missing from XML", None),
        ("the XML names a header that does not exist",
         dict(header=long_attr, members=[member(4, "variable", "n"), member(1, "variable", "q", "include/real/c.hpp")]),
         {}, 1, "extra in XML", None),
        ("no members in the XML", dict(header="int z;\n", members=[]), {}, 1, "XML yielded no members", None),
        ("a reported declaration that is a comment line", dict(header=obj, members=[member(1, "function", "f")]),
         {}, 1, "but that line is a COMMENT", None),
        ("an object in the //! form", dict(header=obj, members=[member(2, "function", "f")]), {}, 1,
         "should use /*! */ block", None),
        ("an attribute whose //! fits one line", dict(header=attr, members=[member(3, "variable", "n")]), {}, 1,
         "should use trailing //!<", None),
        ("an attribute with multi-line rationale keeps its block",
         dict(header=long_attr, members=[member(4, "variable", "n")]), {}, 0, "clean --", None),
        # Not judged: a member with no location, and a member located outside include/real/ even though its
        # file exists and is off the convention.
        ("members with no location or outside include/real/ are not judged",
         dict(header=long_attr, outside=obj,
              members=[member(4, "variable", "n"), '<memberdef kind="function"><name>g</name></memberdef>',
                       member(2, "function", "f", "other/x.hpp"), member(999, "variable", "past_eof")]),
         {}, 0, "clean --", None),
        # A trailing //!< on the declaration is its form, whatever sits above it.
        ("a declaration with a trailing //!< is not re-judged by the run above it",
         dict(header="//! Also here.\nvoid f(); //!< Does it.\n", members=[member(2, "function", "f")]), {}, 0,
         "clean --", None),
        ("--only restricts the verdict, prose scanners included, to matching headers",
         dict(header=obj + prose_defects, extra_headers=("b.hpp",),
              members=[member(2, "function", "f"), member(1, "variable", "z", "include/real/b.hpp")]),
         dict(only="b.hpp"), 0, "clean --", None),
        ("a prose defect alone", dict(header="/*!\n * \\brief A.\n * \\brief B.\n */\nvoid f();\n",
                                      members=[member(5, "function", "f")]), {}, 1, "block(s) with more than one",
         None),
        ("each prose defect is named in the summary",
         dict(header=prose_defects + "/*!\n * \\brief F.\n */\nvoid f();\n", members=[member(26, "function", "f")]),
         {}, 1, ("block(s) stopping mid-sentence", "stacked block pair(s)", "compound(s) in the attribute form",
                 "declaration(s) split by their own doc block"), None),
        ("form violations and a prose defect are both reported",
         dict(header=obj + "/*!\n * \\brief A.\n * \\brief B.\n */\nvoid g();\n", members=[member(2, "function", "f")]),
         {}, 1, ("should use /*! */ block", "plus 1 doc block(s) with a prose defect"), None),
        ("a violation in a generated header is tagged",
         dict(header=obj, members=[member(2, "function", "f", "include/real/unicode/unicode_props.hpp")],
              path="unicode/unicode_props.hpp"), {}, 1,
         ("[GENERATED: fix in tools/gen_*.py]", "of them are in GENERATED headers"), None),
        ("--stats prints the distribution and passes", dict(header=obj, members=[member(2, "function", "f")]),
         dict(stats=True), 0, "attributes keeping a leading block", None),
        ("--fix rewrites an object into a block and an attribute into a trailing comment",
         dict(header=obj + attr, members=[member(2, "function", "f"), member(5, "variable", "n")]), dict(fix=True),
         0, "rewrote 2 member(s)",
         ("a.hpp", "/*!\n * \\brief Does it.\n */\nvoid f();\nstruct S {\n  int n; //!< The count.\n};\n")),
        ("--fix leaves a generated header alone",
         dict(header=obj, members=[member(2, "function", "f", "include/real/unicode/unicode_props.hpp")],
              path="unicode/unicode_props.hpp"), dict(fix=True), 0,
         ("skipping 1 violation(s)", "nothing left to rewrite"), ("unicode/unicode_props.hpp", obj)),
        ("--fix rewrites the form but refuses to call a prose defect fixed",
         dict(header=obj + "/*!\n * \\brief A.\n * \\brief B.\n */\nvoid g();\n", members=[member(2, "function", "f")]),
         dict(fix=True), 1, ("rewrote 1 member(s)", "NOT fixed: 1 doc block(s)"), None),
    ]
    previous = os.getcwd()
    try:
        for name, spec, kwargs, want_rc, marker, file_check in cases:
            with tempfile.TemporaryDirectory() as tmp:
                repo(tmp, **spec)
                os.chdir(tmp)
                out, err = io.StringIO(), io.StringIO()
                try:
                    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
                        rc = judge(**kwargs)
                except SystemExit as stop:
                    rc = 1
                    err.write(str(stop.code))
                except Exception as exc:
                    fails.append(f"judge, {name}: raised {type(exc).__name__}: {exc}")
                    os.chdir(previous)
                    continue
                text = out.getvalue() + err.getvalue()
                content = None
                if file_check is not None:
                    with open(os.path.join(ROOT, file_check[0]), encoding="utf-8") as fh:
                        content = fh.read()
                os.chdir(previous)
            markers = (marker,) if isinstance(marker, str) else marker
            absent = [m for m in markers if m not in text]
            if rc != want_rc or absent or (file_check is not None and content != file_check[1]):
                fails.append(f"judge, {name}: rc={rc} (want {want_rc}), absent markers {absent}"
                             + (f", file {content!r}" if file_check is not None and content != file_check[1] else "")
                             + f"\n    {text.strip()[:400]}")
    finally:
        os.chdir(previous)
    return fails


def _self_test_refresh() -> list[str]:
    """refresh_xml with doxygen substituted: a failure exits naming doxygen, a success clears the directory."""
    import types

    fails: list[str] = []
    with tempfile.TemporaryDirectory() as tmp:
        target = os.path.join(tmp, "xml")
        for rc, want_exit in ((1, True), (0, False)):
            os.makedirs(target, exist_ok=True)
            open(os.path.join(target, "stale.xml"), "w").close()
            fake = lambda *a, **k: types.SimpleNamespace(returncode=rc, stderr="boom", stdout="")  # noqa: E731
            out = io.StringIO()
            try:
                with contextlib.redirect_stdout(out):
                    refresh_xml(target, fake)
                exited = None
            except SystemExit as stop:
                exited = str(stop.code)
            if want_exit and (exited is None or "failed" not in exited):
                fails.append(f"refresh_xml: a doxygen failure must exit naming it, got {exited!r}")
            if not want_exit and (exited is not None or os.path.exists(os.path.join(target, "stale.xml"))):
                fails.append(f"refresh_xml: a success must clear the old XML and not exit, got {exited!r}")
    return fails


def self_test() -> int:
    """Drives the line-level helpers, each prose scanner, and ``judge`` (check, --stats, --fix) alone.
    """
    fails = _self_test_pure() + _self_test_scanners() + _self_test_judge() + _self_test_refresh()
    for line in fails:
        print(f"SELF-TEST FAILED: {line}")
    if fails:
        print(f"check_doc_style: self-test FAILED ({len(fails)} case(s))")
        return 1
    print("check_doc_style: self-test OK — the form helpers, each prose scanner on its defect and its clean form, "
          "and judge's freshness, perimeter, comment-line, violation, generated, --stats and --fix arms")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--fix", action="store_true", help="rewrite violations in place")
    ap.add_argument("--stats", action="store_true", help="print the form distribution and exit 0")
    ap.add_argument("--only", metavar="SUBSTR", help="restrict to files whose path contains SUBSTR")
    ap.add_argument("--refresh", action="store_true", help="run `doxygen Doxyfile` first")
    ap.add_argument("--self-test", action="store_true", help="drive each arm on synthetic repositories")
    args = ap.parse_args()
    if args.self_test:
        return self_test()
    if args.refresh:
        refresh_xml()
    return judge(args.fix, args.stats, args.only)


if __name__ == "__main__":
    sys.exit(main())
