#!/usr/bin/env python3
"""Fail if a curated /reference/ page silently drops a published member.

A class rendered with an explicit ``:members: a, b, c`` allowlist must name
every public, non-\\internal member that Doxyfile.site extracts, or list the
omission in ``docs/site/reference/unpublished.yaml`` with a reason.

Bare ``:members:`` (publish everything) is not a free pass: the class must
carry ``publish_all: <reason>`` in that yaml. Without the reason, switching a
page back to the nude form would walk out of this check without a sound.

This is the other half of the surface split: ``check_doc_voice.py`` guards what
a published comment may SAY; this guards which published symbols a page may
quietly leave out.

Usage:
    python3 tools/check_curated_members.py
    python3 tools/check_curated_members.py --refresh
"""
from __future__ import annotations

import argparse
import glob
import os
import re
import sys
import xml.etree.ElementTree as ET
from collections import defaultdict

# Paths are anchored to the REPOSITORY, not to the caller's working directory: this script runs
# from the root through `make check-curated-members` and from docs/ through `docs-site-gate`, and
# a relative path silently means two different places.
_REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
XML_DIR = os.path.join(_REPO, "build", "doc", "xml-site")
RST_DIR = os.path.join(_REPO, "docs", "site", "reference")
UNPUBLISHED = os.path.join(RST_DIR, "unpublished.yaml")

CLASS_DIR = re.compile(
    r"^\.\.\s+doxygen(?:class|struct)::\s+(?P<name>\S+)\s*\n(?P<opts>(?:[ \t]+.*\n)*)",
    re.MULTILINE,
)
MEMBERS_OPT = re.compile(r":members:(?P<body>[^\n]*(?:\n[ \t]+[^\n]+)*)")
PUBLISH_ALL = "publish_all"


def require_xml() -> None:
    if not os.path.isdir(XML_DIR) or not os.path.isfile(os.path.join(XML_DIR, "index.xml")):
        sys.exit(f"{XML_DIR} not found -- run `make doc-site-xml`, or pass --refresh.")


def refresh_xml() -> None:
    import shutil
    import subprocess

    print("check_curated_members: refreshing build/doc/xml-site ...")
    shutil.rmtree(XML_DIR, ignore_errors=True)
    proc = subprocess.run(["doxygen", "Doxyfile.site"], capture_output=True, text=True)
    if proc.returncode != 0:
        sys.exit(f"doxygen Doxyfile.site failed:\n{(proc.stderr or proc.stdout)[-2000:]}")


def allowlists() -> dict[str, set[str] | None]:
    """class name -> set of member names, or None if `:members:` publishes all."""
    found: dict[str, set[str] | None] = {}
    for path in glob.glob(os.path.join(RST_DIR, "*.rst")):
        text = open(path, encoding="utf-8").read()
        for m in CLASS_DIR.finditer(text):
            name = m.group("name")
            opts = m.group("opts")
            mm = MEMBERS_OPT.search(opts)
            if mm is None:
                continue
            body = mm.group("body").strip()
            if not body:
                found[name] = None
                continue
            names = {p.strip() for p in body.replace("\n", " ").split(",") if p.strip()}
            found[name] = names
    return found


def load_unpublished() -> dict[str, dict[str, str]]:
    """Minimal YAML subset: `Class:` then indented `name: reason`. No PyYAML."""
    if not os.path.isfile(UNPUBLISHED):
        sys.exit(f"{UNPUBLISHED} not found.")
    out: dict[str, dict[str, str]] = {}
    current: str | None = None
    for lineno, raw in enumerate(open(UNPUBLISHED, encoding="utf-8"), 1):
        line = raw.split("#", 1)[0].rstrip()
        if not line.strip():
            continue
        if not line.startswith((" ", "\t")):
            if not line.endswith(":"):
                sys.exit(f"{UNPUBLISHED}:{lineno}: expected 'Class:'")
            current = line[:-1].strip()
            out[current] = {}
            continue
        if current is None:
            sys.exit(f"{UNPUBLISHED}:{lineno}: member line with no class")
        name, sep, reason = line.strip().partition(":")
        if not sep or not name or not reason.strip():
            sys.exit(f"{UNPUBLISHED}:{lineno}: expected 'name: reason'")
        out[current][name] = reason.strip()
    return out


def published_members() -> dict[str, set[str]]:
    """compoundname -> set of public member names extracted by Doxyfile.site."""
    out: dict[str, set[str]] = defaultdict(set)
    for path in glob.glob(os.path.join(XML_DIR, "*.xml")):
        if os.path.basename(path) == "index.xml":
            continue
        try:
            root = ET.parse(path).getroot()
        except ET.ParseError:
            continue
        for cd in root.iter("compounddef"):
            if cd.get("kind") not in {"class", "struct"}:
                continue
            cname = cd.findtext("compoundname") or ""
            for md in cd.findall("sectiondef/memberdef"):
                if md.get("prot") not in {None, "public"}:
                    continue
                # Doxygen still emits the defining declaration; skip friends.
                if md.get("kind") == "friend":
                    continue
                name = (md.findtext("name") or "").strip()
                if name:
                    out[cname].add(name)
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--refresh", action="store_true")
    args = ap.parse_args()
    if args.refresh:
        refresh_xml()
    require_xml()

    lists = allowlists()
    unpublished = load_unpublished()
    extracted = published_members()

    problems: list[str] = []
    for cls, allow in lists.items():
        omit = unpublished.get(cls, {})
        if allow is None:
            if PUBLISH_ALL not in omit:
                problems.append(
                    f"{cls}: bare :members: (publishes everything) with no "
                    f"{PUBLISH_ALL} in unpublished.yaml -- that form leaves "
                    "this check without a field"
                )
            continue
        if PUBLISH_ALL in omit:
            problems.append(
                f"{cls}: unpublished.yaml says {PUBLISH_ALL} but :members: "
                "is an allowlist -- pick one"
            )
            continue
        have = extracted.get(cls, set())
        unknown_omit = set(omit) - have
        if unknown_omit:
            problems.append(
                f"{cls}: unpublished.yaml names {sorted(unknown_omit)} but "
                "Doxyfile.site does not extract them (already \\internal/private, or renamed)"
            )
        missing = have - allow - set(omit)
        if missing:
            problems.append(
                f"{cls}: published but neither in :members: nor unpublished.yaml: "
                f"{sorted(missing)}"
            )
        extra_allow = allow - have
        # Allowlist entries that Doxygen does not extract (e.g. a typo) are also silent.
        if extra_allow:
            problems.append(
                f"{cls}: :members: names {sorted(extra_allow)} but Doxyfile.site "
                "does not extract them"
            )

    if problems:
        print(f"check_curated_members: FAILED -- {len(problems)} allowlist gap(s):")
        for p in problems:
            print(f"  {p}")
        print(
            "  Allowlist: add the member to :members:, or list it in "
            f"{UNPUBLISHED} with a reason. Nude :members:: add "
            f"{PUBLISH_ALL}: <reason> there."
        )
        return 1
    n_allow = sum(1 for v in lists.values() if v is not None)
    n_all = sum(1 for v in lists.values() if v is None)
    print(
        f"check_curated_members: clean -- {n_allow} allowlist(s), "
        f"{n_all} publish_all, omissions explicit"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
