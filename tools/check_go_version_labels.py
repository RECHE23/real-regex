#!/usr/bin/env python3
r"""The Go binding's living pages must carry the module's CURRENT minor, not a past one — and the
perimeter is DECLARED, not hand-picked.

WHY THIS EXISTS. Three pages a Go visitor actually reads carried `v0.2` while the published module
was `v0.3.3` — two minors behind, for weeks. Nothing was wrong in what they SAID: the binding's
README documents every exported name, and the drop-in page is a distillation entitled to say less.
They were wrong about WHAT THEY WERE, which no content check can see. `check_doc_mirror` ties the
mirror pages to their canons and deliberately compares no prose on the distilled ones, and no gate
looked at a version label anywhere.

The label had no coupling to the thing it names because the module's version lives in git tags —
`make release` derives the next one with `git tag -l 'bindings/go/v0.*'` — and a tag cannot drift a
markdown file. So this reads the same source of truth the release recipe reads, and requires the
living pages to agree with it. When a train tags `v0.4.0`, these pages go red immediately, which is
the moment someone can still fix them.

THE PERIMETER IS SWEPT, NOT ENUMERATED. The first version of this guard listed three pages chosen
by hand — and `docs/site/drop-in/index.md` carried `v0.1` for three minors outside the list, while
`bindings/go/real_test.go` carried a legitimate historical `v0.2.0` inside it would have flagged.
A hand-picked list cannot miss a member silently, so a declared set of roots is swept for ANY
`v0.<minor>` label: the root `README.md` (the first impression), `bindings/go/` (the binding's own
prose and package comment), and `docs/site/` (the living site). Every file the sweep finds must be
in exactly one of two lists: PAGES — living pages, held to the two arms below — or EXEMPTIONS, each
with its reason written next to it. A labelled file in neither list is red and names itself; that
arm is the one that proves the perimeter is not just a longer hand-picked list.

Out of the perimeter BY CONSTRUCTION, and why that is right: `CHANGELOG.md` and
`docs/release-notes/` are dated history — naming past versions is their job, and they sit outside
the swept roots. `tools/` (this file's own docstring names `v0.<n>`s), the Makefile and the
workflows are the machinery that READS versions; a label there is code, not a claim to a visitor.

WHAT IT CHECKS, per living page:

  1. The current label `v0.<minor>` appears at least once.
  2. No OTHER `v0.<n>` minor appears — except inside an HTML comment.

The exception is not a loophole, it is the point: `docs/site/drop-in/go.md` records in an HTML
comment that its header said only "Canon =" *until the v0.2.0 arity break*. That is history, it is
correct, and it must stay correct — so the self-test asserts BOTH directions: a stale label in the
prose trips the check, and the historical `v0.2.0` in the comment does not. A guard that cannot tell
those apart would teach someone to falsify a changelog.

THE EXEMPTION RULE. An exemption protects history; it does not invite rewriting it, and it cannot
rot: an exempted file must still exist and still carry a `v0.<n>` label — one that stopped is a
stale exemption and goes red. `real_test.go` is the entry: a test comment recording the v0.2.0
arity change, the same class of record as go.md's HTML comment.

NO COUNTING. Nothing here asserts how many methods the binding offers. A closed count is what went
stale in the first place, one method at a time.
"""
from __future__ import annotations

import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]

#: The declared perimeter: swept for ANY `v0.<minor>` label. Everything found must be classified —
#: in PAGES or in EXEMPTIONS. Dated history (CHANGELOG.md, docs/release-notes/) and the version
#: machinery (tools/, Makefile, workflows) are outside these roots on purpose; see the docstring.
ROOTS = (
    ROOT / "README.md",
    ROOT / "bindings" / "go",
    ROOT / "docs" / "site",
)

#: The living pages a Go visitor reads. The binding's README and the site's drop-in pages are
#: prose; the package comment is what pkg.go.dev renders, which is the same claim to a different
#: visitor. drop-in/index.md is the section landing page — the one the hand-picked list missed.
PAGES = (
    ROOT / "bindings" / "go" / "README.md",
    ROOT / "bindings" / "go" / "real.go",
    ROOT / "docs" / "site" / "drop-in" / "go.md",
    ROOT / "docs" / "site" / "drop-in" / "index.md",
)

#: Files the sweep finds that are HISTORY, not living prose. Each carries its reason; each must
#: still carry a label, or the exemption is stale and goes red.
EXEMPTIONS = {
    ROOT / "bindings" / "go" / "real_test.go":
        "a test comment records the v0.2.0 arity change — dated history, the same class of "
        "record as go.md's HTML comment",
}

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


def sweep(roots: tuple[pathlib.Path, ...] = ROOTS) -> set[pathlib.Path]:
    """Every file under ``roots`` whose raw text carries a `v0.<n>` label. Undecodable files are not
    prose a visitor reads; they are skipped."""
    found: set[pathlib.Path] = set()
    for root in roots:
        if root.is_file():
            candidates = [root]
        elif root.is_dir():
            candidates = sorted(p for p in root.rglob("*") if p.is_file())
        else:
            continue
        for path in candidates:
            try:
                if LABEL.search(path.read_text(encoding="utf-8")):
                    found.add(path)
            except (OSError, UnicodeDecodeError):
                continue
    return found


def run(*, page_texts: dict[pathlib.Path, str] | None = None,
        exemption_texts: dict[pathlib.Path, str] | None = None,
        found: set[pathlib.Path] | None = None,
        minor: int | None = None, quiet: bool = False, minor_of=module_minor) -> int:
    """Every living page carries the current minor and no other outside history; every labelled
    file under ROOTS is classified; every exemption still earns its place."""
    current = minor if minor is not None else minor_of()
    if current is None:
        print("check_go_version_labels: FAIL — no `bindings/go/v0.<minor>.<patch>` tag is readable, "
              "so the label has nothing to be checked against. That is the source of truth "
              "`make release` itself derives the next module version from; without it this script "
              "checks nothing, which is worse than not existing.")
        return 1

    want = f"v0.{current}"
    if page_texts is not None:
        texts = page_texts
    else:
        texts = {p: p.read_text(encoding="utf-8") for p in PAGES if p.is_file()}
    if exemption_texts is not None:
        ext_texts = exemption_texts
    else:
        ext_texts = {p: p.read_text(encoding="utf-8") for p in EXEMPTIONS if p.is_file()}
    labelled = sweep() if found is None else found

    bad: list[str] = []
    for path in PAGES:
        rel = path.relative_to(ROOT)
        text = texts.get(path)
        if text is None:
            bad.append(f"{rel}: a living page in PAGES is gone — put it back or drop it from the "
                       f"list. A missing entry must not pass as a quiet skip.")
            continue
        prose = HTML_COMMENT.sub("", text)
        if want not in prose:
            bad.append(f"{rel}: never says `{want}`, the module's current minor. A visitor cannot "
                       f"tell which version the page describes.")
        stale = sorted({m.group(0) for m in LABEL.finditer(prose)
                        if int(m.group(1)) != current})
        if stale:
            bad.append(f"{rel}: carries {', '.join(repr(s) for s in stale)} in its prose while the "
                       f"module is at `{want}`.")

    for path, reason in EXEMPTIONS.items():
        rel = path.relative_to(ROOT)
        text = ext_texts.get(path)
        if text is None:
            bad.append(f"{rel}: exempted ({reason}) but the file is gone — drop the exemption, "
                       f"or the list rots.")
        elif not LABEL.search(text):
            bad.append(f"{rel}: exempted ({reason}) but carries no `v0.<n>` label any more — "
                       f"the exemption is stale; drop it.")

    classified = set(PAGES) | set(EXEMPTIONS)
    for path in sorted(labelled - classified):
        bad.append(f"{path.relative_to(ROOT)}: carries a `v0.<n>` label and is in neither PAGES "
                   f"nor EXEMPTIONS — a living page joins PAGES, dated history joins EXEMPTIONS "
                   f"with its reason written.")

    if bad:
        if not quiet:
            print("check_go_version_labels: FAIL — a living Go page names the wrong minor, or the "
                  "perimeter met a file nobody classified.")
            for line in bad:
                print(f"    {line}")
            print(f"    The module's current minor is `{want}`, read from the highest "
                  f"`bindings/go/v0.*` tag. Move the label; do NOT touch a `v0.<n>` inside an HTML "
                  f"comment or an exempted file — that is the page's own history and it is "
                  f"supposed to name a past version.")
        return 1

    if not quiet:
        print(f"check_go_version_labels: OK — {len(texts)} living Go page(s) carry `{want}`, none "
              f"names another minor outside its own history, and every labelled file under the "
              f"swept roots is classified.")
    return 0


def self_test() -> int:
    """Exercise the COMPARISON on inputs this function controls, not on whatever is on disk.

    The first version of this read the other two pages from the working tree, and that made it lie:
    with one page dirty, every `run()` already returned 1, so direction 1's "the injection must trip
    it" could never fail -- a self-test that stops testing the moment the tree changes -- and
    direction 2 blamed the HTML comment for a failure the dirty page caused, which is an instruction
    to go and rewrite a changelog. Synthetic texts remove both confounds; `run()` on the real tree is
    a separate statement, made right after this one.

    The page check has TWO arms and replacing the label exercises both at once (it removes the
    wanted one and adds a stale one), so each is driven on its own below. The perimeter has its own
    arms: a labelled file in neither list, an exemption that stopped carrying a label, an exemption
    whose file is gone, and a living page that is gone. Each case asserts the exit status; the two
    verbose passes at the end assert what the message NAMES, because an exit status cannot
    distinguish a clean refusal from a traceback.

    A case that raises is that case's failure: blinding a gone-file guard lets `text = None` flow
    into `LABEL.search`, and the resulting TypeError is reported as the case it broke, so the two
    None-guards redden by refusal like every other arm. The sweep over the roots, an unreadable module
    tag and the clean verdict's own line are driven at the end.
    """
    current = module_minor()
    if current is None:
        print("check_go_version_labels: SELF-TEST INCONCLUSIVE — no module tag to compare against.")
        return 1
    want = f"v0.{current}"
    stale = f"v0.{current - 1}" if current > 0 else "v0.99"

    def pages(**overrides: str) -> dict[pathlib.Path, str]:
        """All living pages carrying the correct label, minus whatever a case overrides by stem."""
        out = {p: f"the {want} subset\n" for p in PAGES}
        for stem, text in overrides.items():
            out[next(p for p in PAGES if p.stem == stem)] = text
        return out

    def exemptions(**overrides: str) -> dict[pathlib.Path, str]:
        """Every exemption still carrying its historical label, minus what a case overrides."""
        out = {p: f"// {stale}.0 gave FindAllIndex the n every other FindAll* took\n"
                  for p in EXEMPTIONS}
        for stem, text in overrides.items():
            out[next(p for p in EXEMPTIONS if p.stem == stem)] = text
        return out

    classified = set(PAGES) | set(EXEMPTIONS)
    stray = ROOT / "docs" / "site" / "drop-in" / "stray.md"

    # (label, page_texts, exemption_texts, found, expected exit)
    cases: list[tuple[str, dict[pathlib.Path, str], dict[pathlib.Path, str],
                      set[pathlib.Path], int]] = [
        # The base must PASS, or every refusal below proves nothing about what it names.
        ("every labelled file classified, every page correct",
         pages(), exemptions(), classified, 0),
    ]
    for page in PAGES:
        rel = page.relative_to(ROOT)
        # Arm 1: the label is absent and no other minor is present.
        cases.append((f"{rel}: label missing",
                      pages(**{page.stem: "no version anywhere\n"}), exemptions(), classified, 1))
        # Arm 2: the current label is present AND a past one sits beside it in the prose.
        cases.append((f"{rel}: stale label beside the current one",
                      pages(**{page.stem: f"the {want} subset, was {stale}\n"}),
                      exemptions(), classified, 1))
        # The drift as it actually happened: the label rolled back wholesale.
        cases.append((f"{rel}: rolled back to {stale}",
                      pages(**{page.stem: f"the {stale} subset\n"}), exemptions(), classified, 1))
        # A living page that is gone must be red, not a quiet skip.
        gone = pages()
        del gone[page]
        cases.append((f"{rel}: the file is gone", gone, exemptions(), classified, 1))

    # The direction a careless fix would break: a past minor inside an HTML comment is the page's
    # history. If the check tripped on it, the only route to green would be falsifying a record.
    cases.append(("a past minor inside an HTML comment",
                  pages(go=f"the {want} subset\n<!-- said less until the {stale}.0 break -->\n"),
                  exemptions(), classified, 0))
    cases.append(("an HTML comment spanning lines",
                  pages(go=f"the {want} subset\n<!--\nhistory: {stale}.0\n-->\n"),
                  exemptions(), classified, 0))

    # The perimeter arms — the reason this version of the guard exists.
    cases.append(("a labelled file in neither PAGES nor EXEMPTIONS",
                  pages(), exemptions(), classified | {stray}, 1))
    only_page = next(iter(EXEMPTIONS))
    cases.append(("an exemption that stopped carrying a label",
                  pages(), {only_page: "no version here any more\n"}, classified, 1))
    cases.append(("an exemption whose file is gone", pages(), {}, classified, 1))

    for label, page_texts, exemption_texts, found, expected in cases:
        try:
            got = run(page_texts=page_texts, exemption_texts=exemption_texts, found=found,
                      minor=current, quiet=True)
        except Exception as exc:  # noqa: BLE001 - a removed guard surfaces as a crash: this case's failure
            got = f"raised {type(exc).__name__}: {exc}"
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
    # file's failure path names files and versions -- so the printing branch is executed twice:
    # once for a page arm, once for the perimeter arm this version of the guard exists for.
    import contextlib, io
    for case_label, kwargs, tokens in [
        ("a rolled-back page",
         dict(page_texts=pages(**{PAGES[0].stem: f"the {stale} subset\n"}),
              exemption_texts=exemptions(), found=classified),
         (str(PAGES[0].relative_to(ROOT)), want, stale, "HTML comment")),
        ("a labelled file in neither list",
         dict(page_texts=pages(), exemption_texts=exemptions(), found=classified | {stray}),
         (str(stray.relative_to(ROOT)), "neither PAGES nor EXEMPTIONS")),
    ]:
        out, err = io.StringIO(), io.StringIO()
        try:
            with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
                code = run(minor=current, quiet=False, **kwargs)
        except Exception as exc:  # noqa: BLE001 - any exception here IS the defect
            print(f"check_go_version_labels: SELF-TEST FAILED — the failure path for "
                  f"{case_label} raised {type(exc).__name__}: {exc}. A guard that refuses and "
                  f"then crashes reports nothing actionable, and an exit-status check cannot "
                  f"tell the two apart.")
            return 1
        printed, stderr = out.getvalue(), err.getvalue()
        problems = [f"exit {code}, expected 1"] if code != 1 else []
        if stderr.strip():
            problems.append(f"wrote to stderr: {stderr.strip()[:80]!r}")
        for token in tokens:
            if token not in printed:
                problems.append(f"the message never names {token!r}")
        if problems:
            print(f"check_go_version_labels: SELF-TEST FAILED — the verbose failure path for "
                  f"{case_label} is wrong:")
            for problem in problems:
                print(f"    {problem}")
            for line in printed.strip().split("\n"):
                print(f"      {line}")
            return 1

    import tempfile
    with tempfile.TemporaryDirectory() as tmp:
        base = pathlib.Path(tmp)
        (base / "dir" / "sub").mkdir(parents=True)
        labelled_file = base / "README.md"
        labelled_file.write_text("the v0.3 binding\n", encoding="utf-8")
        inside = base / "dir" / "sub" / "page.md"
        inside.write_text("since v0.2\n", encoding="utf-8")
        (base / "dir" / "plain.md").write_text("no version here\n", encoding="utf-8")
        (base / "dir" / "blob.bin").write_bytes(b"\xff\xfe v0.1 \x80")
        try:
            got_sweep = sweep((labelled_file, base / "dir", base / "absent"))
        except Exception as exc:  # noqa: BLE001
            got_sweep = f"raised {type(exc).__name__}: {exc}"
    if got_sweep != {labelled_file, inside}:
        print(f"check_go_version_labels: SELF-TEST FAILED — sweep over a file root, a directory root "
              f"(a labelled page, an unlabelled one, an undecodable blob) and an absent root found "
              f"{got_sweep!r}.")
        return 1
    for case_label, kwargs, want_code, sentence in [
        ("no readable module tag", dict(minor_of=lambda: None), 1, "no `bindings/go/v0.<minor>.<patch>` tag"),
        ("the clean verdict", dict(page_texts=pages(), exemption_texts=exemptions(), found=classified,
                                   minor=current), 0, "check_go_version_labels: OK"),
    ]:
        out = io.StringIO()
        try:
            with contextlib.redirect_stdout(out):
                code = run(**kwargs)
        except Exception as exc:  # noqa: BLE001
            code = f"raised {type(exc).__name__}"
        if code != want_code or sentence not in out.getvalue():
            print(f"check_go_version_labels: SELF-TEST FAILED — {case_label}: exit {code} (want {want_code}), "
                  f"{sentence!r} {'printed' if sentence in out.getvalue() else 'NOT printed'}.")
            return 1

    print(f"check_go_version_labels: self-test OK — {len(cases)} synthetic case(s) over "
          f"{len(PAGES)} pages and {len(EXEMPTIONS)} exemption(s): a missing label, a stale one "
          f"beside the current one, a wholesale rollback and a gone page each trip the check; a "
          f"past minor inside an HTML comment (one line or many) does not; a labelled file in "
          f"neither list, a stale exemption and a gone exemption each trip; and the failure path "
          f"names the file, the versions and the rule, on stdout alone.")
    return 0


if __name__ == "__main__":
    sys.exit(self_test() if "--self-test" in sys.argv[1:] else run())
