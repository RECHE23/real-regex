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
    python3 tools/check_curated_members.py --self-test
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


def require_xml(xml_dir: str = XML_DIR) -> None:
    if not os.path.isdir(xml_dir) or not os.path.isfile(os.path.join(xml_dir, "index.xml")):
        sys.exit(f"{xml_dir} not found -- run `make doc-site-xml`, or pass --refresh.")


def refresh_xml(xml_dir: str = XML_DIR, run=None) -> None:
    """Regenerates xml-site; ``run`` is the doxygen launcher (subprocess.run by default)."""
    import shutil
    import subprocess

    run = run or subprocess.run
    print("check_curated_members: refreshing build/doc/xml-site ...")
    shutil.rmtree(xml_dir, ignore_errors=True)
    proc = run(["doxygen", "Doxyfile.site"], capture_output=True, text=True)
    if proc.returncode != 0:
        sys.exit(f"doxygen Doxyfile.site failed:\n{(proc.stderr or proc.stdout)[-2000:]}")


def allowlists(rst_dir: str = RST_DIR) -> dict[str, set[str] | None]:
    """class name -> set of member names, or None if `:members:` publishes all."""
    found: dict[str, set[str] | None] = {}
    for path in glob.glob(os.path.join(rst_dir, "*.rst")):
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


def load_unpublished(path: str = UNPUBLISHED) -> dict[str, dict[str, str]]:
    """Minimal YAML subset: `Class:` then indented `name: reason`. No PyYAML."""
    if not os.path.isfile(path):
        sys.exit(f"{path} not found.")
    out: dict[str, dict[str, str]] = {}
    current: str | None = None
    for lineno, raw in enumerate(open(path, encoding="utf-8"), 1):
        line = raw.split("#", 1)[0].rstrip()
        if not line.strip():
            continue
        if not line.startswith((" ", "\t")):
            if not line.endswith(":"):
                sys.exit(f"{path}:{lineno}: expected 'Class:'")
            current = line[:-1].strip()
            out[current] = {}
            continue
        if current is None:
            sys.exit(f"{path}:{lineno}: member line with no class")
        name, sep, reason = line.strip().partition(":")
        if not sep or not name or not reason.strip():
            sys.exit(f"{path}:{lineno}: expected 'name: reason'")
        out[current][name] = reason.strip()
    return out


def published_members(xml_dir: str = XML_DIR) -> dict[str, set[str]]:
    """compoundname -> set of public member names extracted by Doxyfile.site."""
    out: dict[str, set[str]] = defaultdict(set)
    for path in glob.glob(os.path.join(xml_dir, "*.xml")):
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


def compare(lists: dict[str, set[str] | None], unpublished: dict[str, dict[str, str]],
            extracted: dict[str, set[str]]) -> list[str]:
    """Every gap between the pages' allowlists, the omissions file and what Doxygen extracts."""
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
    return problems


def judge(rst_dir: str = RST_DIR, unpublished_path: str = UNPUBLISHED, xml_dir: str = XML_DIR) -> int:
    """Prints the verdict over the pages in ``rst_dir``; a missing XML or yaml exits through sys.exit."""
    require_xml(xml_dir)
    lists = allowlists(rst_dir)
    problems = compare(lists, load_unpublished(unpublished_path), published_members(xml_dir))
    if problems:
        print(f"check_curated_members: FAILED -- {len(problems)} allowlist gap(s):")
        for p in problems:
            print(f"  {p}")
        print(
            "  Allowlist: add the member to :members:, or list it in "
            f"{unpublished_path} with a reason. Nude :members:: add "
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


_GAPS = {
    "bare": "bare :members: (publishes everything)",
    "both": "pick one",
    "stale_omit": "unpublished.yaml names",
    "missing": "published but neither in :members: nor unpublished.yaml",
    "typo": ":members: names",
}


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
    """Drives each gap of ``compare`` alone, then each reader on synthetic files, then ``judge``.

    A reader's refusal leaves through sys.exit and is captured with its message; any other exception
    is that case's failure.
    """
    failures = 0
    ext = {"C": {"a", "b"}}
    gaps = [
        ("bare :members: with no publish_all", {"C": None}, {}, "bare"),
        ("bare :members: with publish_all", {"C": None}, {"C": {PUBLISH_ALL: "whole surface"}}, None),
        ("an allowlist AND publish_all", {"C": {"a", "b"}}, {"C": {PUBLISH_ALL: "x"}}, "both"),
        ("an omission Doxygen no longer extracts", {"C": {"a", "b"}}, {"C": {"gone": "renamed"}}, "stale_omit"),
        ("a published member neither listed nor omitted", {"C": {"a"}}, {}, "missing"),
        ("an allowlist entry Doxygen does not extract", {"C": {"a", "b", "tpyo"}}, {}, "typo"),
        ("an omission covers what the allowlist leaves out", {"C": {"a"}}, {"C": {"b": "experimental"}}, None),
    ]
    for name, lists, unpublished, gap in gaps:
        try:
            problems = compare(lists, unpublished, ext)
        except Exception as exc:
            print(f"SELF-TEST FAILED: {name}: compare raised {type(exc).__name__}: {exc}")
            failures += 1
            continue
        text = "\n".join(problems)
        wrong = [g for g, m in _GAPS.items() if g != gap and m in text]
        if (gap is None and problems) or (gap is not None and (_GAPS[gap] not in text or wrong)):
            print(f"SELF-TEST FAILED: {name}: want {gap!r}, got {problems}")
            failures += 1

    def refused(fn, *args) -> str | None:
        err = io.StringIO()
        try:
            with contextlib.redirect_stderr(err):
                fn(*args)
        except SystemExit as stop:
            return str(stop.code)
        except Exception as exc:  # a removed guard surfaces as a crash: not the refusal asked for
            return f"raised {type(exc).__name__}: {exc}"
        return None

    def read(fn, *args):
        try:
            return fn(*args)
        except (Exception, SystemExit) as exc:
            return f"raised {type(exc).__name__}: {exc}"

    with tempfile.TemporaryDirectory() as tmp:
        rst = os.path.join(tmp, "page.rst")
        with open(rst, "w", encoding="utf-8") as fh:
            fh.write(".. doxygenclass:: real::A\n   :project: real\n   :members: one,\n      two\n\n"
                     ".. doxygenstruct:: real::B\n   :members:\n\n"
                     ".. doxygenclass:: real::C\n   :project: real\n")
        lists = read(allowlists, tmp)
        if lists != {"real::A": {"one", "two"}, "real::B": None}:
            print(f"SELF-TEST FAILED: allowlists: continuation / bare / absent :members: read as {lists}")
            failures += 1

        yaml = os.path.join(tmp, "u.yaml")
        for name, text, want in [
            ("a class line without its colon", "real::A\n", "expected 'Class:'"),
            ("a member line before any class", "  x: reason\n", "member line with no class"),
            ("a member without a reason", "real::A:\n  x:\n", "expected 'name: reason'"),
        ]:
            with open(yaml, "w", encoding="utf-8") as fh:
                fh.write(text)
            got = refused(load_unpublished, yaml)
            if got is None or want not in got:
                print(f"SELF-TEST FAILED: unpublished.yaml, {name}: refusal {got!r}, want {want!r}")
                failures += 1
        with open(yaml, "w", encoding="utf-8") as fh:
            fh.write("# omissions\n\nreal::A:\n  x: experimental  # why\n")
        if read(load_unpublished, yaml) != {"real::A": {"x": "experimental"}}:
            print("SELF-TEST FAILED: unpublished.yaml: comments and blank lines not skipped")
            failures += 1
        got = refused(load_unpublished, os.path.join(tmp, "absent.yaml"))
        if got is None or "not found" not in got or got.startswith("raised"):
            print(f"SELF-TEST FAILED: unpublished.yaml: a missing file must be refused by name, got {got!r}")
            failures += 1

        xml_dir = os.path.join(tmp, "xml")
        os.mkdir(xml_dir)
        with open(os.path.join(xml_dir, "classreal_1_1A.xml"), "w", encoding="utf-8") as fh:
            fh.write('<doxygen><compounddef kind="class"><compoundname>real::A</compoundname>'
                     '<sectiondef><memberdef kind="function" prot="public"><name>shown</name></memberdef>'
                     '<memberdef kind="function" prot="private"><name>hidden</name></memberdef>'
                     '<memberdef kind="friend" prot="public"><name>pal</name></memberdef></sectiondef>'
                     '</compounddef><compounddef kind="namespace"><compoundname>real</compoundname>'
                     '<sectiondef><memberdef kind="function" prot="public"><name>free</name></memberdef>'
                     '</sectiondef></compounddef></doxygen>')
        with open(os.path.join(xml_dir, "index.xml"), "w", encoding="utf-8") as fh:
            fh.write('<doxygen><compounddef kind="class"><compoundname>real::Z</compoundname>'
                     '<sectiondef><memberdef kind="function" prot="public"><name>idx</name></memberdef>'
                     '</sectiondef></compounddef></doxygen>')
        with open(os.path.join(xml_dir, "broken.xml"), "w", encoding="utf-8") as fh:
            fh.write("<doxygen><unclosed>")
        got = read(published_members, xml_dir)
        got = dict(got) if not isinstance(got, str) else got
        if got != {"real::A": {"shown"}}:
            print(f"SELF-TEST FAILED: published_members: private, friend, namespace, index.xml and an unparsable "
                  f"file must all be skipped; got {got}")
            failures += 1

        page = os.path.join(tmp, "pages")
        os.mkdir(page)
        with open(os.path.join(page, "a.rst"), "w", encoding="utf-8") as fh:
            fh.write(".. doxygenclass:: real::A\n   :members: shown\n")
        with open(yaml, "w", encoding="utf-8") as fh:
            fh.write("# none\n")
        for name, xml_arg, want_rc, marker in [
            ("judge: a clean tree", xml_dir, 0, "clean -- 1 allowlist(s)"),
            ("judge: an XML directory without index.xml", page, 1, "not found -- run `make doc-site-xml`"),
        ]:
            out, err = io.StringIO(), io.StringIO()
            try:
                with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
                    rc = judge(page, yaml, xml_arg)
            except SystemExit as stop:
                rc = 1
                err.write(str(stop.code))
            except Exception as exc:
                rc, err = -1, io.StringIO(f"raised {type(exc).__name__}: {exc}")
            text = out.getvalue() + err.getvalue()
            if rc != want_rc or marker not in text:
                print(f"SELF-TEST FAILED: {name}: rc={rc} (want {want_rc}), {marker!r} absent\n    {text.strip()}")
                failures += 1
        with open(os.path.join(page, "a.rst"), "w", encoding="utf-8") as fh:
            fh.write(".. doxygenclass:: real::A\n   :members: shown, typo\n")
        out = io.StringIO()
        try:
            with contextlib.redirect_stdout(out):
                rc = judge(page, yaml, xml_dir)
        except (Exception, SystemExit) as exc:
            rc = -1
            out.write(f"raised {type(exc).__name__}: {exc}")
        if rc != 1 or "FAILED -- 1 allowlist gap(s)" not in out.getvalue():
            print(f"SELF-TEST FAILED: judge: a gap must fail and be counted, got rc={rc}\n    {out.getvalue().strip()}")
            failures += 1

    for line in _self_test_refresh():
        print(f"SELF-TEST FAILED: {line}")
        failures += 1
    total = len(gaps) + 11
    if failures:
        print(f"check_curated_members: self-test FAILED ({failures} of {total} case(s))")
        return 1
    print(f"check_curated_members: self-test OK — {total} cases: each gap reached alone, and each reader's "
          "refusals and skips (continuation lines, malformed yaml, private/friend/namespace members)")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--refresh", action="store_true")
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()
    if args.self_test:
        return self_test()
    if args.refresh:
        refresh_xml()
    return judge()


if __name__ == "__main__":
    sys.exit(main())
