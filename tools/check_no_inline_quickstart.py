#!/usr/bin/env python3
"""Landing template: every <pre> is a {{ quickstart_* }} injection point.

A pasted snippet lives in a <pre>. Prose in a <p> — including the word
static_regex on the constexpr card — must not trip this. The previous check
grepped a token list that collided with that card.

Fails closed: no <pre>, a <pre> whose body is not exactly one placeholder, a
missing language, or a duplicate.
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEFAULT = ROOT / "docs/site/_templates/landing.html"
NAMES = ("quickstart_cpp", "quickstart_py", "quickstart_rs", "quickstart_go")
PLACEHOLDER = re.compile(r"^\s*\{\{\s*(" + "|".join(NAMES) + r")\s*\}\}\s*$")
PRE = re.compile(r"<pre\b[^>]*>(.*?)</pre>", re.S | re.I)


def check(tmpl: Path) -> list[str]:
    if not tmpl.is_file():
        return [f"check-no-inline-quickstart: FAIL -- {tmpl} missing"]
    text = tmpl.read_text(encoding="utf-8")
    bodies = PRE.findall(text)
    errors: list[str] = []
    if not bodies:
        return ["check-no-inline-quickstart: FAIL -- no <pre> in the landing template"]
    seen: list[str] = []
    for i, body in enumerate(bodies, 1):
        m = PLACEHOLDER.match(body)
        if not m:
            preview = " ".join(body.split())
            if len(preview) > 80:
                preview = preview[:80] + "..."
            errors.append(
                "check-no-inline-quickstart: FAIL -- <pre> is not a "
                f"{{{{ quickstart_* }}}} placeholder:\n  pre #{i}: {preview}"
            )
            continue
        seen.append(m.group(1))
    missing = [n for n in NAMES if n not in seen]
    if missing:
        errors.append(
            "check-no-inline-quickstart: FAIL -- missing injection point(s): "
            + " ".join(missing)
        )
    if len(seen) != len(set(seen)):
        errors.append(
            "check-no-inline-quickstart: FAIL -- duplicate {{ quickstart_* }} in <pre>: "
            + " ".join(seen)
        )
    return errors


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("template", nargs="?", type=Path, default=DEFAULT)
    args = p.parse_args()
    errors = check(args.template)
    if errors:
        print("\n".join(errors))
        return 1
    n = len(PRE.findall(args.template.read_text(encoding="utf-8")))
    print(f"check-no-inline-quickstart: OK — {n} <pre> placeholder(s), no pasted snippet")
    return 0


if __name__ == "__main__":
    sys.exit(main())
