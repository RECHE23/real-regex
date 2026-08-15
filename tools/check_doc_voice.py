#!/usr/bin/env python3
"""Fail if a Doxygen comment that the USER site publishes contains route vocabulary.

Published means present in ``build/doc/xml-site`` (Doxyfile.site: INTERNAL_DOCS=NO,
EXTRACT_PRIVATE=NO). Implementation notes belong in ``//`` comments, which that
XML never sees. The developer tree (``build/doc/xml``, INTERNAL_DOCS=YES) is
not this check's input -- that is how ``\\internal`` stays load-bearing.

Usage:
    python3 tools/check_doc_voice.py              # check, exit 1 on any hit
    python3 tools/check_doc_voice.py --refresh    # regenerate xml-site first
"""
from __future__ import annotations

import argparse
import glob
import os
import re
import sys
import xml.etree.ElementTree as ET

# Paths are anchored to the REPOSITORY, not to the caller's working directory: this script runs
# from the root through `make check-doc-voice` and from docs/ through `docs-site-gate`, and a
# relative path silently means two different places.
_REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
XML_DIR = os.path.join(_REPO, "build", "doc", "xml-site")
ROOT = os.path.join(_REPO, "include", "real") + os.sep

# A hit is a published comment talking like a bench log or a plan. User-facing
# words this does NOT ban: linear, DFA, fused, ReDoS, constexpr.
PATTERNS: list[tuple[re.Pattern[str], str]] = [
    (re.compile(r"\bTrailingLA\b"), "TrailingLA"),
    (re.compile(r"\bcascade_"), "cascade_"),
    (re.compile(r"\bP3c\b"), "P3c"),
    (re.compile(r"\bOPT-C\b"), "OPT-C"),
    (re.compile(r"\bO2r(?:-\d[a-z]*)?\b"), "O2r plan label"),
    (re.compile(r"\bD1a\b"), "D1a"),
    (re.compile(r"outlined and cold", re.I), "outlined and cold"),
    (re.compile(r"monomorphic walk", re.I), "monomorphic walk"),
    (re.compile(r"memchr-cascade", re.I), "memchr-cascade"),
    (re.compile(r"\bns/B\b"), "ns/B"),
    (re.compile(r"paired draws", re.I), "paired draws"),
    (re.compile(r"\bcallgrind\b", re.I), "callgrind"),
    (re.compile(r"[+\-−]\s*\d+(?:\.\d+)?\s*%"), "measured percentage"),
    (re.compile(r"\b\d+\s+of\s+\d+\s+draws\b", re.I), "draw count"),
]


def require_xml() -> None:
    if not os.path.isdir(XML_DIR):
        sys.exit(f"{XML_DIR} not found -- run `make doc-site-xml`, or pass --refresh.")
    index = os.path.join(XML_DIR, "index.xml")
    if not os.path.isfile(index):
        sys.exit(f"{XML_DIR} holds no index.xml -- run `make doc-site-xml`, or pass --refresh.")
    run_time = os.path.getmtime(index)
    stale = [
        h
        for h in glob.glob(os.path.join(ROOT, "**", "*.hpp"), recursive=True)
        if os.path.getmtime(h) > run_time
    ]
    if stale:
        sys.exit(
            f"check_doc_voice: {XML_DIR} is OLDER than {len(stale)} header(s), "
            f"e.g. {stale[0]}. This check reads the USER tree (Doxyfile.site -> "
            "build/doc/xml-site). `doxygen Doxyfile` refreshes the other profile. "
            "Run `make doc-site-xml`, `make doc-xml` (both trees), or pass --refresh."
        )


def refresh_xml() -> None:
    import shutil
    import subprocess

    print("check_doc_voice: refreshing build/doc/xml-site (doxygen Doxyfile.site) ...")
    shutil.rmtree(XML_DIR, ignore_errors=True)
    proc = subprocess.run(["doxygen", "Doxyfile.site"], capture_output=True, text=True)
    if proc.returncode != 0:
        sys.exit(f"doxygen Doxyfile.site failed:\n{(proc.stderr or proc.stdout)[-2000:]}")


def xml_text(el: ET.Element | None) -> str:
    if el is None:
        return ""
    return " ".join("".join(el.itertext()).split())


def published_comments() -> list[tuple[str, str, str]]:
    """(qualified-name, kind, comment-text) for every published member/compound."""
    out: list[tuple[str, str, str]] = []
    for path in glob.glob(os.path.join(XML_DIR, "*.xml")):
        if os.path.basename(path) == "index.xml":
            continue
        try:
            root = ET.parse(path).getroot()
        except ET.ParseError:
            continue
        for cd in root.iter("compounddef"):
            name = cd.findtext("compoundname") or "?"
            text = xml_text(cd.find("briefdescription")) + " " + xml_text(cd.find("detaileddescription"))
            if text.strip():
                out.append((name, cd.get("kind") or "compound", text))
            for md in cd.iter("memberdef"):
                # Doxygen's XML still lists documented private members when
                # EXTRACT_PRIVATE=NO. They are not published: Breathe allowlists
                # and the user pages never render them.
                if md.get("prot") not in {None, "public"}:
                    continue
                loc = md.find("location")
                if loc is not None and loc.get("file") and ROOT not in loc.get("file", "").replace("\\", "/"):
                    continue
                mname = (md.findtext("qualifiedname") or md.findtext("name") or "?").strip()
                mtext = (
                    xml_text(md.find("briefdescription"))
                    + " "
                    + xml_text(md.find("detaileddescription"))
                    + " "
                    + xml_text(md.find("inbodydescription"))
                )
                if mtext.strip():
                    out.append((mname, md.get("kind") or "member", mtext))
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--refresh", action="store_true", help="run `doxygen Doxyfile.site` first")
    args = ap.parse_args()
    if args.refresh:
        refresh_xml()
    require_xml()

    hits: list[str] = []
    for name, kind, text in published_comments():
        for pat, label in PATTERNS:
            if pat.search(text):
                snippet = text if len(text) <= 160 else text[:157] + "..."
                hits.append(f"  {name} ({kind}): {label}\n    {snippet}")
                break
    if hits:
        print(
            f"check_doc_voice: FAILED -- {len(hits)} published comment(s) still "
            "talk like a bench log or a plan:"
        )
        print("\n".join(hits))
        print(
            "  Move the measurement / route note to a // comment, or hide the "
            "member from Doxyfile.site (EXCLUDE_SYMBOLS, or "
            "#ifndef DOXYGEN_SHOULD_SKIP_THIS)."
        )
        return 1
    print("check_doc_voice: clean -- no route vocabulary in published Doxygen XML")
    return 0


if __name__ == "__main__":
    sys.exit(main())
