#!/usr/bin/env python3
"""Landing template: every <pre> is a {{ quickstart_* }} injection point.

Assumption, not a fact: a pasted snippet lives in a <pre>. A paste into a
<div class="codepanel"> with no <pre> would not trip this. Prose in a <p> —
including the word static_regex on the constexpr card — must not. The previous
check grepped a token list that collided with that card.

Fails closed: no template, no <pre>, a <pre> whose body is not exactly one
placeholder, a language whose placeholder is missing, or a duplicate.
``--self-test`` drives each arm alone.
"""
from __future__ import annotations

import argparse
import re
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEFAULT = ROOT / "docs/site/_templates/landing.html"
NAMES = ("quickstart_cpp", "quickstart_py", "quickstart_rs", "quickstart_go")
PLACEHOLDER = re.compile(r"^\s*\{\{\s*(" + "|".join(NAMES) + r")\s*\}\}\s*$")
PRE = re.compile(r"<pre\b[^>]*>(.*?)</pre>", re.S | re.I)


def check(tmpl: Path) -> list[str]:
    if not tmpl.is_file():
        return [f"check-no-inline-quickstart: FAIL -- {tmpl} not found"]
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


_ARMS = {
    "file": "not found",
    "nopre": "no <pre> in the landing template",
    "pasted": "is not a {{ quickstart_* }} placeholder",
    "missing": "missing injection point",
    "duplicate": "duplicate {{ quickstart_* }}",
}


def self_test() -> int:
    """Drives each arm of ``check`` alone on a synthetic template; an empty error list is a pass."""
    def pre(name: str) -> str:
        return f'<pre class="codepanel">{{{{ {name} }}}}</pre>\n'
    full = "".join(pre(n) for n in NAMES)
    cases = [
        ("the template is missing", None, None),
        ("no <pre> at all", "<p>Welcome</p>\n", "nopre"),
        ("a pasted snippet in a <pre>", full + "<pre>auto m = re.search(text);</pre>\n", "pasted"),
        ("a language's placeholder is missing", "".join(pre(n) for n in NAMES[:-1]), "missing"),
        ("a placeholder appears twice", full + pre(NAMES[0]), "duplicate"),
        # The collision the previous token grep had: prose naming the API outside any <pre> is not a paste.
        ("prose naming static_regex outside any <pre>", "<p>Try static_regex for constexpr.</p>\n" + full, "ok"),
    ]
    failures = 0
    for name, content, arm in cases:
        with tempfile.TemporaryDirectory() as td:
            tmpl = Path(td) / "landing.html"
            if content is not None:
                tmpl.write_text(content, encoding="utf-8")
            try:
                errors = check(tmpl)
            except Exception as exc:  # a removed guard surfaces as a crash; report it as this case's failure
                print(f"SELF-TEST FAILED: {name}: check raised {type(exc).__name__}: {exc}")
                failures += 1
                continue
        text = "\n".join(errors)
        want = "file" if content is None else arm
        if want == "ok":
            ok = not errors
            wrong = [a for a, m in _ARMS.items() if m in text]
        else:
            wrong = [a for a, m in _ARMS.items() if a != want and m in text]
            ok = bool(errors) and _ARMS[want] in text and not wrong
        if not ok:
            print(f"SELF-TEST FAILED: {name}: want {want!r}, errors={errors}, other arms {wrong}")
            failures += 1
    if failures:
        print(f"check-no-inline-quickstart: self-test FAILED ({failures} of {len(cases)} case(s))")
        return 1
    print(f"check-no-inline-quickstart: self-test OK — {len(cases)} cases: a missing template, no <pre>, a pasted "
          "snippet, a missing language and a duplicate each refused alone; prose naming the API passes")
    return 0


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("template", nargs="?", type=Path, default=DEFAULT)
    p.add_argument("--self-test", action="store_true")
    args = p.parse_args()
    if args.self_test:
        return self_test()
    errors = check(args.template)
    if errors:
        print("\n".join(errors))
        return 1
    n = len(PRE.findall(args.template.read_text(encoding="utf-8")))
    print(f"check-no-inline-quickstart: OK — {n} <pre> placeholder(s), no pasted snippet")
    return 0


if __name__ == "__main__":
    sys.exit(main())
