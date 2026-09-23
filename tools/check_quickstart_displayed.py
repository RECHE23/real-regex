#!/usr/bin/env python3
"""Compile the C++ quickstart *rectangle* as its own TU.

The landing and Getting started inject the text between // [quickstart] and
// [/quickstart] in examples/cpp/quickstart.cpp. Compiling the whole file does
not prove those lines are autonomous — the first-hour paste failed on ""sv
while the file (which has <string_view> outside the markers) stayed green.

Fails if the file or its markers are missing, if the region is empty, or if
it does not compile. Prints the line count. CXX and -I come from the
environment the Makefile already uses. ``--self-test`` drives each arm alone.
"""
from __future__ import annotations

import argparse
import contextlib
import io
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "examples" / "cpp" / "quickstart.cpp"
START = "// [quickstart]"
END = "// [/quickstart]"


def extract(text: str) -> str | None:
    """The text between the markers, or None when either marker is absent."""
    # The region starts on the line after the start marker. A missing start marker, or one on the last
    # line, leaves nothing after that newline -- so the end-marker refusal below names those too.
    _, _, after_start = text.partition(START)
    _, _, rest = after_start.partition("\n")
    body, end, _ = rest.partition(END)
    if not end:
        return None
    return body


def judge(src: Path, cxx: str, include: str) -> int:
    if not src.is_file():
        print(f"check-quickstart-displayed: FAIL -- {src} missing")
        return 1
    body = extract(src.read_text(encoding="utf-8"))
    if body is None:
        print(f"check-quickstart-displayed: FAIL -- {START} / {END} markers not found in {src.name}")
        return 1
    n = len([ln for ln in body.splitlines() if ln.strip()])
    if n == 0:
        print("check-quickstart-displayed: FAIL -- 0 lines between the markers")
        return 1
    with tempfile.TemporaryDirectory() as td:
        tu = Path(td) / "displayed.cpp"
        tu.write_text(body, encoding="utf-8")
        r = subprocess.run(
            [cxx, "-std=c++20", "-fsyntax-only", "-I", include, str(tu)],
            capture_output=True,
            text=True,
        )
        if r.returncode != 0:
            print("check-quickstart-displayed: FAIL -- displayed rectangle does not compile")
            sys.stderr.write(r.stderr)
            return 1
    print(f"check-quickstart-displayed: OK — {n} lines compiled as their own TU")
    return 0


_ARMS = {
    "missing": "missing",
    "markers": "markers not found",
    "empty": "0 lines between the markers",
    "compile": "does not compile",
    "ok": "compiled as their own TU",
}


def self_test(cxx: str) -> int:
    """Drives each arm of ``judge`` alone on a synthetic source; a case that raises is its failure."""
    region = "{start}\n{body}{end}\n"
    cases = [
        ("the source file is missing", None, 1, "missing"),
        ("no markers", "int main() {}\n", 1, "markers"),
        ("a start marker without an end", f"{START}\nint main() {{}}\n", 1, "markers"),
        ("a start marker ending the file", f"int main() {{}}\n{START}", 1, "markers"),
        ("an empty region", region.format(start=START, body="\n  \n", end=END), 1, "empty"),
        # The first-hour failure: autonomous-looking lines that lean on an include outside the markers.
        ("a region leaning on an include outside it",
         "#include <string_view>\n" + region.format(
             start=START, body='using namespace std::literals;\nauto s = ""sv;\n', end=END), 1, "compile"),
        ("an autonomous region",
         region.format(start=START, body="#include <string_view>\nusing namespace std::literals;\n"
                                         'auto s = ""sv;\n', end=END), 0, "ok"),
    ]
    failures = 0
    for name, content, want_rc, arm in cases:
        with tempfile.TemporaryDirectory() as td:
            src = Path(td) / "quickstart.cpp"
            if content is not None:
                src.write_text(content, encoding="utf-8")
            out, err = io.StringIO(), io.StringIO()
            try:
                with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
                    rc = judge(src, cxx, str(ROOT / "include"))
            except Exception as exc:
                print(f"SELF-TEST FAILED: {name}: judge raised {type(exc).__name__}: {exc}")
                failures += 1
                continue
        text = out.getvalue()
        wrong = [a for a, m in _ARMS.items() if a != arm and m in text]
        if rc != want_rc or _ARMS[arm] not in text or wrong:
            print(f"SELF-TEST FAILED: {name}: rc={rc} (want {want_rc}), arm {arm!r} "
                  f"{'present' if _ARMS[arm] in text else 'ABSENT'}, other arms {wrong}\n    {text.strip()}")
            failures += 1
    if failures:
        print(f"check-quickstart-displayed: self-test FAILED ({failures} of {len(cases)} case(s))")
        return 1
    print(f"check-quickstart-displayed: self-test OK — {len(cases)} cases: a missing file, absent markers "
          "(three ways), an empty region and a region leaning on an outside include each refused alone")
    return 0


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--cxx", default=os.environ.get("CXX", "c++"))
    p.add_argument("--self-test", action="store_true")
    args = p.parse_args()
    if args.self_test:
        return self_test(args.cxx)
    return judge(SRC, args.cxx, str(ROOT / "include"))


if __name__ == "__main__":
    sys.exit(main())
