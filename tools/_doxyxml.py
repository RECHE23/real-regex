#!/usr/bin/env python3
"""The Doxygen XML trees the doc checks read: regenerating one, dating it, and what it names.

check_doc_style reads build/doc/xml (Doxyfile); check_doc_voice and check_curated_members read
build/doc/xml-site (Doxyfile.site). Each keeps its own refusal messages, which name its profile; the
mechanics they share live here, with this module's own --self-test (tools/blind_guard.py blinds the
conditions below through it, as it does a check's).
"""
from __future__ import annotations

import contextlib
import glob
import io
import os
import shutil
import subprocess
import sys
import tempfile
import types
import xml.etree.ElementTree as ET


def refresh(xml_dir: str, doxyfile: str, label: str, run=None) -> None:
    """Regenerates ``xml_dir`` from scratch with ``doxygen doxyfile``; exits naming doxygen on failure.

    The directory is REMOVED first: Doxygen's XML output is incremental and leaves a per-file XML
    untouched when it decides the compound is unchanged, so a refresh that only re-ran it could hand a
    check line numbers from a previous revision of a header just edited. ``run`` is the launcher
    (subprocess.run by default), substituted by the self-test.
    """
    run = run or subprocess.run
    print(f"{label}: refreshing {xml_dir} (clean doxygen {doxyfile}) ...")
    shutil.rmtree(xml_dir, ignore_errors=True)
    proc = run(["doxygen", doxyfile], capture_output=True, text=True)
    if proc.returncode != 0:
        sys.exit(f"doxygen {doxyfile} failed:\n{(proc.stderr or proc.stdout)[-2000:]}")


def named_headers(xml_dir: str) -> set[str]:
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


def stale_headers(xml_dir: str, header_root: str) -> list[str]:
    """The headers under ``header_root`` modified after Doxygen last wrote ``xml_dir`` (none when it is empty).

    Dated by when Doxygen last RAN, not by the oldest file it left: it rewrites only the XML whose content
    changed, so a `min()` reported every header stale forever. index.xml is regenerated on every run and
    dates it; the newest XML stands in should a Doxygen stop rewriting it.
    """
    xmls = glob.glob(os.path.join(xml_dir, "*.xml"))
    if not xmls:
        return []
    index_xml = os.path.join(xml_dir, "index.xml")
    if os.path.isfile(index_xml):
        run_time = os.path.getmtime(index_xml)
    else:
        run_time = max(os.path.getmtime(p) for p in xmls)
    return [h for h in glob.glob(os.path.join(header_root, "**", "*.hpp"), recursive=True)
            if os.path.getmtime(h) > run_time]


def self_test() -> int:
    """Drives every condition above: refresh's two outcomes, a named and an unnamed location, staleness
    dated by index.xml and, without one, by the newest XML."""
    fails: list[str] = []
    with tempfile.TemporaryDirectory() as tmp:
        target = os.path.join(tmp, "xml")
        for rc, want_exit in ((1, True), (0, False)):
            os.makedirs(target, exist_ok=True)
            open(os.path.join(target, "stale.xml"), "w").close()
            fake = lambda *a, **k: types.SimpleNamespace(returncode=rc, stderr="boom", stdout="")  # noqa: E731
            try:
                with contextlib.redirect_stdout(io.StringIO()):
                    refresh(target, "Doxyfile", "self-test", fake)
                exited = None
            except SystemExit as stop:
                exited = str(stop.code)
            if want_exit and (exited is None or "failed" not in exited):
                fails.append(f"refresh: a doxygen failure must exit naming it, got {exited!r}")
            if not want_exit and (exited is not None or os.path.exists(os.path.join(target, "stale.xml"))):
                fails.append(f"refresh: a success must clear the old XML and not exit, got {exited!r}")

        xml_dir = os.path.join(tmp, "named")
        os.makedirs(xml_dir)
        with open(os.path.join(xml_dir, "a.xml"), "w") as f:
            f.write('<doxygen><location file="/x/include/real/a.hpp"/><location file="/x/src/b.cpp"/></doxygen>')
        with open(os.path.join(xml_dir, "broken.xml"), "w") as f:
            f.write("<doxygen>")
        if named_headers(xml_dir) != {"include/real/a.hpp"}:
            fails.append(f"named_headers: want only include/real/a.hpp, got {sorted(named_headers(xml_dir))}")

        headers = os.path.join(tmp, "include", "real")
        os.makedirs(headers)
        header = os.path.join(headers, "h.hpp")
        open(header, "w").close()
        empty = os.path.join(tmp, "empty")
        os.makedirs(empty)
        try:
            if stale_headers(empty, headers):
                fails.append("stale_headers: a directory with no XML has nothing to be stale against")
        except ValueError as err:  # dating an empty run is the defect, reported as a refusal
            fails.append(f"stale_headers: a directory with no XML must not be dated ({err})")
        for with_index in (True, False):
            run_dir = os.path.join(tmp, f"run{int(with_index)}")
            os.makedirs(run_dir)
            for name in (("index.xml", "old.xml") if with_index else ("new.xml", "old.xml")):
                open(os.path.join(run_dir, name), "w").close()
            # With an index, an XML Doxygen left untouched can be NEWER than the run (copied, touched): the
            # index must still date it. Without one, the newest XML does.
            os.utime(os.path.join(run_dir, "old.xml"), (3000, 3000) if with_index else (1000, 1000))
            dating = "index.xml" if with_index else "new.xml"
            os.utime(os.path.join(run_dir, dating), (2000, 2000))
            os.utime(header, (1500, 1500))
            if stale_headers(run_dir, headers):
                fails.append(f"stale_headers ({dating}): a header older than the run is not stale")
            os.utime(header, (2500, 2500))
            if stale_headers(run_dir, headers) != [header]:
                fails.append(f"stale_headers ({dating}): a header newer than the run is stale")
    for line in fails:
        print(f"  FAIL {line}")
    print("_doxyxml: self-test " + ("FAILED" if fails else "OK"))
    return 1 if fails else 0


if __name__ == "__main__":
    if sys.argv[1:] == ["--self-test"]:
        sys.exit(self_test())
    sys.exit("usage: _doxyxml.py --self-test")
