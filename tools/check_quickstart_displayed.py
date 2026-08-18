#!/usr/bin/env python3
"""Compile the C++ quickstart *rectangle* as its own TU.

The landing and Getting started inject the text between // [quickstart] and
// [/quickstart] in examples/cpp/quickstart.cpp. Compiling the whole file does
not prove those lines are autonomous — the first-hour paste failed on ""sv
while the file (which has <string_view> outside the markers) stayed green.

Fails if the region is empty. Prints the line count. CXX and -I come from
the environment the Makefile already uses.
"""
from __future__ import annotations

import argparse
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "examples" / "cpp" / "quickstart.cpp"
START = "// [quickstart]"
END = "// [/quickstart]"


def extract(text: str) -> str:
    start = text.index(START)
    start = text.index("\n", start) + 1
    end = text.index(END, start)
    return text[start:end]


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--cxx", default=os.environ.get("CXX", "c++"))
    args = p.parse_args()

    if not SRC.is_file():
        print(f"check-quickstart-displayed: FAIL -- {SRC} missing")
        return 1
    body = extract(SRC.read_text(encoding="utf-8"))
    n = len([ln for ln in body.splitlines() if ln.strip()])
    if n == 0:
        print("check-quickstart-displayed: FAIL -- 0 lines between the markers")
        return 1
    include = str(ROOT / "include")
    with tempfile.TemporaryDirectory() as td:
        tu = Path(td) / "displayed.cpp"
        tu.write_text(body, encoding="utf-8")
        r = subprocess.run(
            [args.cxx, "-std=c++20", "-fsyntax-only", "-I", include, str(tu)],
            capture_output=True,
            text=True,
        )
        if r.returncode != 0:
            print("check-quickstart-displayed: FAIL -- displayed rectangle does not compile")
            sys.stderr.write(r.stderr)
            return 1
    print(f"check-quickstart-displayed: OK — {n} lines compiled as their own TU")
    return 0


if __name__ == "__main__":
    sys.exit(main())
