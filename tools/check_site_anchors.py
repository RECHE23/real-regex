#!/usr/bin/env python3
"""Every `:start-after:` / `:end-before:` anchor under docs/site/ must resolve, exactly once, in the
file its directive includes.

WHAT IT READS. The site pages under docs/site/, and only the files those pages `{include}` /
`{literalinclude}`. After P1 those targets are examples and snippet files. It does **not** read
`docs/BENCHMARKS.md`: the site is forbidden from including that file (`docs-site-gate`'s
no-benchmarks-include witness), and no live page does. A docstring that still named the ledger
was a ghost consumer -- a coupling the include-ban had already deleted.

WHY THIS EXISTS. The site slices sources by literal text, so a rewrite in one file silently
breaks a page built from another. That happened: a re-stamp deleted the sentence
`docs/site/drop-in/regex.md` anchored on, and the failure surfaced only in CI's `Docs-site`
job because `full-local-gate`'s own docs-site step is SKIPPED whenever `sphinx-build` is
absent -- which it is on a plain dev machine. This check needs nothing but the standard library, so
it runs in the gate's cheap section and turns a CI-only red into a local one.

It also rejects an anchor that appears MORE than once. Sphinx's extractor is first-occurrence, so a
duplicated anchor silently slices the wrong region instead of failing.

  python tools/check_site_anchors.py [docs/site]
  python tools/check_site_anchors.py --self-test
"""
import contextlib
import io
import pathlib
import re
import sys
import tempfile

# `{include} <path>` / `{literalinclude} <path>` followed by its options, in MyST or reST spelling.
_DIRECTIVE = re.compile(
    # MyST: ```{include} path      reST: .. literalinclude:: path
    # The colons are part of the reST spelling only, so they must be optional -- requiring them made
    # this check silently blind to every MyST page, which is most of the site.
    r"(?:`{3,}\s*\{(include|literalinclude)\}|\.\.\s+(include|literalinclude)::)"
    r"\s*(?P<path>\S+)\s*\n"
    r"(?P<opts>(?:\s*:[a-z-]+:[^\n]*\n)+)",
    re.MULTILINE,
)
_OPTION = re.compile(r"^\s*:(start-after|end-before):\s*(?P<val>.+?)\s*$", re.MULTILINE)


def _unquote(value: str) -> str:
    if len(value) >= 2 and value[0] == value[-1] and value[0] in "\"'":
        return value[1:-1]
    return value


def check(root: pathlib.Path) -> int:
    if not root.is_dir():
        print(f"check-site-anchors: {root} absent, nothing to check")
        return 0
    problems = []
    checked = 0
    for page in sorted(root.rglob("*")):
        if page.suffix not in {".md", ".rst"}:
            continue
        text = page.read_text(encoding="utf-8")
        for m in _DIRECTIVE.finditer(text):
            target = (page.parent / m.group("path")).resolve()
            anchors = [(k, _unquote(v)) for k, v in
                       ((o.group(1), o.group("val")) for o in _OPTION.finditer(m.group("opts")))]
            if not anchors:
                continue
            if not target.is_file():
                problems.append(f"{page}: include target {m.group('path')} does not exist")
                continue
            body = target.read_text(encoding="utf-8")
            for kind, anchor in anchors:
                checked += 1
                n = body.count(anchor)
                if n == 0:
                    problems.append(f"{page}: :{kind}: {anchor!r} not found in {m.group('path')}")
                elif n > 1:
                    problems.append(
                        f"{page}: :{kind}: {anchor!r} appears {n}x in {m.group('path')} "
                        f"-- the extractor takes the FIRST, so the slice is not the one meant")
    if problems:
        print(f"check-site-anchors: FAILED -- {len(problems)} broken anchor(s):")
        for p in problems:
            print(f"  {p}")
        print("  Anchor on an explicit marker (an HTML comment) rather than on prose that can be reworded.")
        return 1
    print(f"check-site-anchors: clean -- {checked} anchor(s) resolve uniquely")
    return 0


_ARMS = {
    "notfound": "not found in",
    "duplicate": "the extractor takes the FIRST",
    "notarget": "does not exist",
    "clean": "resolve uniquely",
    "absent": "absent, nothing to check",
}


def self_test() -> int:
    """Drives each arm of ``check`` alone on a synthetic site; a case that raises is its failure.

    Both directive spellings are driven, because requiring reST's colons once made the check blind to
    every MyST page.
    """
    myst = "```{{literalinclude}} {path}\n:start-after: {a}\n:end-before: {b}\n```\n"
    rest = ".. literalinclude:: {path}\n   :start-after: {a}\n   :end-before: {b}\n"
    src = "// before\n// [begin]\nint x;\n// [end]\n"
    cases = [
        ("MyST: an anchor that is not in the target", {"p.md": myst.format(path="s.cpp", a="// [begin]", b="// [gone]"),
                                                       "s.cpp": src}, 1, "notfound"),
        ("reST: an anchor that is not in the target", {"p.rst": rest.format(path="s.cpp", a="// [gone]", b="// [end]"),
                                                       "s.cpp": src}, 1, "notfound"),
        ("an anchor present twice", {"p.md": myst.format(path="s.cpp", a="// [begin]", b="// [end]"),
                                     "s.cpp": src + src}, 1, "duplicate"),
        ("an include target that does not exist", {"p.md": myst.format(path="gone.cpp", a="x", b="y")}, 1, "notarget"),
        ("quoted anchors that resolve", {"p.md": myst.format(path="s.cpp", a="'// [begin]'", b='"// [end]"'),
                                         "s.cpp": src}, 0, "clean"),
        ("an include with no anchors is not judged", {"p.md": "```{include} gone.md\n:parser: myst\n```\n"},
         0, "clean"),
        ("a directive in a non-page file is not read", {"notes.txt": myst.format(path="gone.cpp", a="x", b="y")},
         0, "clean"),
        ("an absent site directory is announced", None, 0, "absent"),
    ]
    failures = 0
    for name, files, want_rc, arm in cases:
        with tempfile.TemporaryDirectory() as td:
            root = pathlib.Path(td) / "site"
            if files is not None:
                root.mkdir()
                for rel, content in files.items():
                    (root / rel).write_text(content, encoding="utf-8")
            out = io.StringIO()
            try:
                with contextlib.redirect_stdout(out):
                    rc = check(root)
            except Exception as exc:
                print(f"SELF-TEST FAILED: {name}: check raised {type(exc).__name__}: {exc}")
                failures += 1
                continue
        text = out.getvalue()
        wrong = [a for a, m in _ARMS.items() if a != arm and m in text]
        if rc != want_rc or _ARMS[arm] not in text or wrong:
            print(f"SELF-TEST FAILED: {name}: rc={rc} (want {want_rc}), arm {arm!r} "
                  f"{'present' if _ARMS[arm] in text else 'ABSENT'}, other arms {wrong}\n    {text.strip()}")
            failures += 1
    if failures:
        print(f"check-site-anchors: self-test FAILED ({failures} of {len(cases)} case(s))")
        return 1
    print(f"check-site-anchors: self-test OK — {len(cases)} cases: a missing anchor (MyST and reST), a duplicate "
          "and a missing target each refused alone; quoted anchors, anchorless includes and non-pages pass")
    return 0


def main() -> int:
    if sys.argv[1:] == ["--self-test"]:
        return self_test()
    return check(pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "docs/site"))


if __name__ == "__main__":
    raise SystemExit(main())
