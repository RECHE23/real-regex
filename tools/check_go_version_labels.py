#!/usr/bin/env python3
r"""The Go binding's living pages must carry the module's CURRENT minor, not a past one.

WHY THIS EXISTS. Three pages a Go visitor actually reads carried `v0.2` while the published module
was `v0.3.3` — two minors behind, for weeks. Nothing was wrong in what they SAID: the binding's
README documents every exported name, and the drop-in page is a distillation entitled to say less.
They were wrong about WHAT THEY WERE, which no content check can see. `check_doc_mirror` ties the
mirror pages to their canons and deliberately compares no prose on the distilled ones, and no gate
looked at a version label anywhere.

The label had no coupling to the thing it names because the module's version lives in git tags —
`make release` derives the next one with `git tag -l 'bindings/go/v0.*'` — and a tag cannot drift a
markdown file. So this reads the same source of truth the release recipe reads, and requires the
three pages to agree with it. When a train tags `v0.4.0`, these pages go red immediately, which is
the moment someone can still fix them.

WHAT IT CHECKS, per file:

  1. The current label `v0.<minor>` appears at least once.
  2. No OTHER `v0.<n>` minor appears — except inside an HTML comment.

The exception is not a loophole, it is the point: `docs/site/drop-in/go.md` records in an HTML
comment that its header said only "Canon =" *until the v0.2.0 arity break*. That is history, it is
correct, and it must stay correct — so the self-test asserts BOTH directions: a stale label in the
prose trips the check, and the historical `v0.2.0` in the comment does not. A guard that cannot tell
those apart would teach someone to falsify a changelog.

NO COUNTING. Nothing here asserts how many methods the binding offers. A closed count is what went
stale in the first place, one method at a time.
"""
from __future__ import annotations

import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]

#: The pages a Go visitor reads. The binding's README and the site's drop-in page are prose; the
#: package comment is what pkg.go.dev renders, which is the same claim to a different visitor.
PAGES = (
    ROOT / "bindings" / "go" / "README.md",
    ROOT / "bindings" / "go" / "real.go",
    ROOT / "docs" / "site" / "drop-in" / "go.md",
)

#: Any `v0.<minor>` label, with or without a patch. `v0` alone is a MAJOR-line statement ("out of
#: scope for v0") and is deliberately not matched: it does not go stale with a minor.
LABEL = re.compile(r"\bv0\.(\d+)(?:\.\d+)?\b")

#: HTML comments hold the pages' own history, which must not be rewritten to satisfy a label check.
HTML_COMMENT = re.compile(r"<!--.*?-->", re.S)


def module_minor() -> int | None:
    """The published module's current minor, from the same tags `make release` reads."""
    try:
        out = subprocess.run(["git", "tag", "-l", "bindings/go/v0.*"], cwd=ROOT,
                             capture_output=True, text=True, check=True).stdout
    except (OSError, subprocess.CalledProcessError):
        return None
    minors = [int(m.group(1)) for m in (re.fullmatch(r"bindings/go/v0\.(\d+)\.(\d+)", line.strip())
                                        for line in out.splitlines() if line.strip()) if m]
    return max(minors) if minors else None


def run(*, page_texts: dict[pathlib.Path, str] | None = None, minor: int | None = None,
        quiet: bool = False) -> int:
    """Every page carries the current minor, and no page carries a different one outside history."""
    current = minor if minor is not None else module_minor()
    if current is None:
        print("check_go_version_labels: FAIL — no `bindings/go/v0.<minor>.<patch>` tag is readable, "
              "so the label has nothing to be checked against. That is the source of truth "
              "`make release` itself derives the next module version from; without it this script "
              "checks nothing, which is worse than not existing.")
        return 1

    want = f"v0.{current}"
    texts = page_texts if page_texts is not None else {p: p.read_text(encoding="utf-8") for p in PAGES}
    bad: list[str] = []
    for path, text in texts.items():
        rel = path.relative_to(ROOT)
        prose = HTML_COMMENT.sub("", text)
        if want not in prose:
            bad.append(f"{rel}: never says `{want}`, the module's current minor. A visitor cannot "
                       f"tell which version the page describes.")
        stale = sorted({m.group(0) for m in LABEL.finditer(prose)
                        if int(m.group(1)) != current})
        if stale:
            bad.append(f"{rel}: carries {', '.join(repr(s) for s in stale)} in its prose while the "
                       f"module is at `{want}`.")

    if bad:
        if not quiet:
            print("check_go_version_labels: FAIL — a living Go page names the wrong minor.")
            for line in bad:
                print(f"    {line}")
            print(f"    The module's current minor is `{want}`, read from the highest "
                  f"`bindings/go/v0.*` tag. Move the label; do NOT touch a `v0.<n>` inside an HTML "
                  f"comment — that is the page's own history and it is supposed to name a past "
                  f"version.")
        return 1

    if not quiet:
        print(f"check_go_version_labels: OK — {len(texts)} living Go page(s) carry `{want}`, and "
              f"none names another minor outside its own history.")
    return 0


def self_test() -> int:
    """Exercise the COMPARISON on inputs this function controls, not on whatever is on disk.

    The first version of this read the other two pages from the working tree, and that made it lie:
    with one page dirty, every `run()` already returned 1, so direction 1's "the injection must trip
    it" could never fail -- a self-test that stops testing the moment the tree changes -- and
    direction 2 blamed the HTML comment for a failure the dirty page caused, which is an instruction
    to go and rewrite a changelog. Synthetic texts remove both confounds; `run()` on the real tree is
    a separate statement, made right after this one.

    The check has TWO arms and replacing the label exercises both at once (it removes the wanted one
    and adds a stale one), so each is driven on its own below.
    """
    current = module_minor()
    if current is None:
        print("check_go_version_labels: SELF-TEST INCONCLUSIVE — no module tag to compare against.")
        return 1
    want = f"v0.{current}"
    stale = f"v0.{current - 1}" if current > 0 else "v0.99"

    def pages(**overrides: str) -> dict[pathlib.Path, str]:
        """All three pages carrying the correct label, minus whatever a case overrides by stem."""
        out = {p: f"the {want} subset\n" for p in PAGES}
        for stem, text in overrides.items():
            out[next(p for p in PAGES if p.stem == stem)] = text
        return out

    cases: list[tuple[str, dict[pathlib.Path, str], int]] = [
        # The base must PASS, or every refusal below proves nothing about what it names.
        ("three correct pages", pages(), 0),
    ]
    for page in PAGES:
        # Arm 1: the label is absent and no other minor is present.
        cases.append((f"{page.relative_to(ROOT)}: label missing",
                      pages(**{page.stem: "no version anywhere\n"}), 1))
        # Arm 2: the current label is present AND a past one sits beside it in the prose.
        cases.append((f"{page.relative_to(ROOT)}: stale label beside the current one",
                      pages(**{page.stem: f"the {want} subset, was {stale}\n"}), 1))
        # The drift as it actually happened: the label rolled back wholesale.
        cases.append((f"{page.relative_to(ROOT)}: rolled back to {stale}",
                      pages(**{page.stem: f"the {stale} subset\n"}), 1))

    # The direction a careless fix would break: a past minor inside an HTML comment is the page's
    # history. If the check tripped on it, the only route to green would be falsifying a record.
    cases.append(("a past minor inside an HTML comment",
                  pages(go=f"the {want} subset\n<!-- said less until the {stale}.0 break -->\n"), 0))
    cases.append(("an HTML comment spanning lines",
                  pages(go=f"the {want} subset\n<!--\nhistory: {stale}.0\n-->\n"), 0))

    for label, texts, expected in cases:
        got = run(page_texts=texts, minor=current, quiet=True)
        if got != expected:
            verb = "did NOT trip" if expected == 1 else "TRIPPED"
            print(f"check_go_version_labels: SELF-TEST FAILED — {label} {verb} the check "
                  f"(expected exit {expected}, got {got}).")
            if expected == 0:
                print("    A guard that cannot tell a stale label from a page's own changelog "
                      "entry would teach someone to rewrite the record.")
            return 1

    # What the cases above prove is that the COMPARISON reacts; they say nothing about the message a
    # reader acts on. An exit status cannot distinguish a clean refusal from a traceback, and this
    # file's failure path names files and versions -- so the printing branch is executed once.
    import contextlib, io
    out, err = io.StringIO(), io.StringIO()
    target = PAGES[0]
    try:
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = run(page_texts=pages(**{target.stem: f"the {stale} subset\n"}),
                       minor=current, quiet=False)
    except Exception as exc:  # noqa: BLE001 - any exception here IS the defect
        print(f"check_go_version_labels: SELF-TEST FAILED — the failure path raised "
              f"{type(exc).__name__}: {exc}. A guard that refuses and then crashes reports nothing "
              f"actionable, and an exit-status check cannot tell the two apart.")
        return 1
    printed, stderr = out.getvalue(), err.getvalue()
    problems = [f"exit {code}, expected 1"] if code != 1 else []
    if stderr.strip():
        problems.append(f"wrote to stderr: {stderr.strip()[:80]!r}")
    for token in (str(target.relative_to(ROOT)), want, stale, "HTML comment"):
        if token not in printed:
            problems.append(f"the message never names {token!r}")
    if problems:
        print("check_go_version_labels: SELF-TEST FAILED — the verbose failure path is wrong:")
        for problem in problems:
            print(f"    {problem}")
        for line in printed.strip().split("\n"):
            print(f"      {line}")
        return 1

    print(f"check_go_version_labels: self-test OK — {len(cases)} synthetic case(s) over "
          f"{len(PAGES)} pages: a missing label, a stale one beside the current one and a wholesale "
          f"rollback each trip the check; a past minor inside an HTML comment (one line or many) "
          f"does not; and the failure path names the file, both versions and the comment rule, on "
          f"stdout alone.")
    return 0


if __name__ == "__main__":
    sys.exit(self_test() if "--self-test" in sys.argv[1:] else run())
