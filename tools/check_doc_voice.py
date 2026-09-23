#!/usr/bin/env python3
"""Fail if a Doxygen comment that the USER site publishes contains route vocabulary.

Published means present in ``build/doc/xml-site`` (Doxyfile.site: INTERNAL_DOCS=NO,
EXTRACT_PRIVATE=NO). Implementation notes belong in ``//`` comments, which that
XML never sees. The developer tree (``build/doc/xml``, INTERNAL_DOCS=YES) is
not this check's input -- that is how ``\\internal`` stays load-bearing.

A red on FRESHNESS is not a verdict on content. When ``build/doc/xml-site`` is
older than a header this check refuses before reading a single comment, so a
freshness failure means the prose was not judged at all: refresh
(``make doc-site-xml``, or ``--refresh``) and re-run before concluding anything.

Usage:
    python3 tools/check_doc_voice.py              # check, exit 1 on any hit
    python3 tools/check_doc_voice.py --refresh    # regenerate xml-site first
    python3 tools/check_doc_voice.py --self-test  # drive each arm on synthetic repositories
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


def _xml_dir(repo: str) -> str:
    return os.path.join(repo, "build", "doc", "xml-site")


def _root(repo: str) -> str:
    return os.path.join(repo, "include", "real") + os.sep


def require_xml(repo: str = _REPO) -> None:
    xml_dir = _xml_dir(repo)
    if not os.path.isdir(xml_dir):
        sys.exit(f"{xml_dir} not found -- run `make doc-site-xml`, or pass --refresh.")
    index = os.path.join(xml_dir, "index.xml")
    if not os.path.isfile(index):
        sys.exit(f"{xml_dir} holds no index.xml -- run `make doc-site-xml`, or pass --refresh.")
    run_time = os.path.getmtime(index)
    stale = [
        h
        for h in glob.glob(os.path.join(_root(repo), "**", "*.hpp"), recursive=True)
        if os.path.getmtime(h) > run_time
    ]
    if stale:
        sys.exit(
            f"check_doc_voice: {xml_dir} is OLDER than {len(stale)} header(s), "
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


def site_input_headers(repo: str = _REPO) -> list[str]:
    """Headers Doxyfile.site INPUT names, expanded (a directory becomes its *.hpp)."""
    doxy = os.path.join(repo, "Doxyfile.site")
    if not os.path.isfile(doxy):
        sys.exit(f"check_doc_voice: {doxy} not found")
    tokens: list[str] = []
    collecting = False
    with open(doxy, encoding="utf-8") as fh:
        for raw in fh:
            if not collecting:
                if raw.startswith("INPUT") and "=" in raw:
                    collecting = True
                    rest = raw.split("=", 1)[1]
                else:
                    continue
            else:
                rest = raw
            stripped = rest.rstrip("\n").rstrip()
            continued = stripped.endswith("\\")
            body = stripped[:-1] if continued else stripped
            tokens.extend(body.split())
            if not continued:
                break
    found: list[str] = []
    for tok in tokens:
        abs_p = tok if os.path.isabs(tok) else os.path.join(repo, tok)
        if os.path.isdir(abs_p):
            for dirpath, _dns, names in os.walk(abs_p):
                for name in names:
                    if name.endswith(".hpp"):
                        full = os.path.join(dirpath, name)
                        found.append(os.path.relpath(full, repo).replace("\\", "/"))
        elif os.path.isfile(abs_p):
            found.append(os.path.relpath(abs_p, repo).replace("\\", "/"))
        else:
            found.append(tok.replace("\\", "/"))
    return sorted(set(found))


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


def published_comments(repo: str = _REPO) -> list[tuple[str, str, str]]:
    """(qualified-name, kind, comment-text) for every published member/compound."""
    out: list[tuple[str, str, str]] = []
    root_dir = _root(repo).replace("\\", "/")
    for path in glob.glob(os.path.join(_xml_dir(repo), "*.xml")):
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
                if loc is not None and loc.get("file") and root_dir not in loc.get("file", "").replace("\\", "/"):
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


def judge(repo: str = _REPO) -> int:
    """Judges the published comments of ``repo``; a freshness or input refusal exits through sys.exit."""
    require_xml(repo)

    exp = set(site_input_headers(repo))
    got = xml_named_headers(_xml_dir(repo))
    comments = published_comments(repo)
    if not exp:
        print("check_doc_voice: FAIL -- Doxyfile.site INPUT expanded to 0 headers")
        return 1
    if got != exp or not comments:
        missing = sorted(exp - got)
        extra = sorted(got - exp)
        print(
            f"check_doc_voice: FAIL -- {len(got)} of {len(exp)} header(s) in "
            f"xml-site, {len(comments)} published comment(s)"
        )
        if missing:
            print("  missing from XML: " + ", ".join(missing))
        if extra:
            print("  extra in XML: " + ", ".join(extra))
        if not comments:
            print("  XML yielded no published comments")
        return 1

    hits: list[str] = []
    for name, kind, text in comments:
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
    print(
        f"check_doc_voice: clean -- {len(comments)} comment(s) in "
        f"{len(got)} of {len(exp)} header(s), no route vocabulary"
    )
    return 0


_ARMS = {
    "noxml": "not found -- run `make doc-site-xml`",
    "noindex": "holds no index.xml",
    "stale": "is OLDER than",
    "nodoxy": "Doxyfile.site not found",
    "noinput": "INPUT expanded to 0 headers",
    "missing": "missing from XML",
    "extra": "extra in XML",
    "nocomments": "XML yielded no published comments",
    "hit": "talk like a bench log or a plan",
    "clean": "no route vocabulary",
}


def _member(file: str, text: str, prot: str = "public", name: str = "f") -> str:
    return (f'<memberdef kind="function" prot="{prot}"><name>{name}</name>'
            f'<briefdescription><para>{text}</para></briefdescription>'
            f'<location file="{file}"/></memberdef>')


def self_test() -> int:
    """Drives each arm of ``judge`` alone on a synthetic repository (headers, Doxyfile.site, xml-site).

    A refusal leaves through sys.exit and is captured with its message; any other exception is that
    case's failure. The member reader's two skips are driven too: a private member and a member whose
    location lies outside include/real/ carry bench voice that must NOT be reported.
    """
    voice = "costs +12.5 % here"

    def build(repo: str, *, xml=True, index=True, doxy="INPUT = include/real\n", headers=("a.hpp",),
              located=("a.hpp",), members=None, stale=False, compound_brief="",
              index_text="<doxygenindex/>") -> None:
        inc = os.path.join(repo, "include", "real")
        os.makedirs(inc)
        for h in headers:
            with open(os.path.join(inc, h), "w", encoding="utf-8") as fh:
                fh.write("// header\n")
        if doxy is not None:
            with open(os.path.join(repo, "Doxyfile.site"), "w", encoding="utf-8") as fh:
                fh.write(doxy)
        if not xml:
            return
        xml_dir = _xml_dir(repo)
        os.makedirs(xml_dir)
        body = members if members is not None else "".join(
            _member(f"{repo}/include/real/{h}", "Plain text.") for h in located)
        brief = f"<briefdescription><para>{compound_brief}</para></briefdescription>" if compound_brief else ""
        with open(os.path.join(xml_dir, "classA.xml"), "w", encoding="utf-8") as fh:
            fh.write(f'<doxygen><compounddef kind="class"><compoundname>A</compoundname>{brief}'
                     f'<sectiondef>{body}</sectiondef></compounddef></doxygen>')
        if index:
            with open(os.path.join(xml_dir, "index.xml"), "w", encoding="utf-8") as fh:
                fh.write(index_text)
        if stale:  # the header is written after the index: the XML predates it
            past = time.time() - 100
            os.utime(os.path.join(xml_dir, "index.xml"), (past, past))

    def members(repo, *specs):
        return "".join(_member(f"{repo}/{f}", t, prot) for f, t, prot in specs)

    cases = [
        ("no xml-site directory", dict(xml=False), 1, "noxml"),
        ("no index.xml", dict(index=False), 1, "noindex"),
        ("a header newer than the XML", dict(stale=True), 1, "stale"),
        ("no Doxyfile.site", dict(doxy=None), 1, "nodoxy"),
        ("an INPUT that names nothing", dict(doxy="PROJECT_NAME = x\n"), 1, "noinput"),
        ("an INPUT header the XML never names", dict(headers=("a.hpp", "b.hpp")), 1, "missing"),
        ("the XML names a header INPUT does not",
         dict(doxy="INPUT = include/real/a.hpp\n", headers=("a.hpp", "b.hpp"), located=("a.hpp", "b.hpp")),
         1, "extra"),
        ("no published comment at all", dict(members=lambda r: members(r, ("include/real/a.hpp", "", "public"))),
         1, "nocomments"),
        ("bench voice in a published comment",
         dict(members=lambda r: members(r, ("include/real/a.hpp", voice, "public"))), 1, "hit"),
        ("bench voice in a class's own comment", dict(compound_brief=voice), 1, "hit"),
        # The line after a continued INPUT is another setting, not more INPUT.
        ("a continued INPUT, then another setting, clean comments",
         dict(doxy="INPUT = include/real/a.hpp \\\n        include/real/b.hpp\nEXCLUDE = include/real/zz.hpp\n",
              headers=("a.hpp", "b.hpp"), located=("a.hpp", "b.hpp")), 0, "clean"),
        ("an absolute INPUT path", dict(doxy=lambda r: f"INPUT = {r}/include/real/a.hpp\n"), 0, "clean"),
        ("bench voice only in a private member, outside include/real/, and in index.xml",
         dict(members=lambda r: members(r, ("include/real/a.hpp", "Plain.", "public"),
                                        ("include/real/a.hpp", voice, "private"),
                                        ("other/z.hpp", voice, "public")),
              index_text=("<doxygen><compounddef kind='class'><compoundname>I</compoundname>"
                          f"<briefdescription><para>{voice}</para></briefdescription></compounddef></doxygen>")),
         0, "clean"),
    ]
    failures = 0
    for name, spec, want_rc, arm in cases:
        with tempfile.TemporaryDirectory() as tmp:
            repo = os.path.join(tmp, "repo")
            spec = dict(spec)
            for key in ("members", "doxy"):
                if callable(spec.get(key)):
                    spec[key] = spec[key](repo)
            build(repo, **spec)
            out, err = io.StringIO(), io.StringIO()
            try:
                with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
                    rc = judge(repo)
            except SystemExit as stop:
                rc = 1
                err.write(str(stop.code))
            except Exception as exc:
                print(f"SELF-TEST FAILED: {name}: judge raised {type(exc).__name__}: {exc}")
                failures += 1
                continue
        text = out.getvalue() + err.getvalue()
        wrong = [a for a, m in _ARMS.items() if a != arm and m in text]
        if rc != want_rc or _ARMS[arm] not in text or wrong:
            print(f"SELF-TEST FAILED: {name}: rc={rc} (want {want_rc}), arm {arm!r} "
                  f"{'present' if _ARMS[arm] in text else 'ABSENT'}, other arms {wrong}\n    {text.strip()[:400]}")
            failures += 1
    if failures:
        print(f"check_doc_voice: self-test FAILED ({failures} of {len(cases)} case(s))")
        return 1
    print(f"check_doc_voice: self-test OK — {len(cases)} cases: three freshness refusals, four input/perimeter "
          "refusals and a bench-voice hit (member and class) each reached alone; a continued INPUT stops at the "
          "next setting, an absolute INPUT resolves, and private, out-of-tree and index.xml comments are not read")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--refresh", action="store_true", help="run `doxygen Doxyfile.site` first")
    ap.add_argument("--self-test", action="store_true", help="drive each arm on synthetic repositories")
    args = ap.parse_args()
    if args.self_test:
        return self_test()
    if args.refresh:
        refresh_xml()
    return judge()


if __name__ == "__main__":
    sys.exit(main())
