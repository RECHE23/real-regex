#!/usr/bin/env python3
"""Fail if a docs/*.dox or docs/*.md file talks like a bench log, unless it is
a named journal.

This is the source-side twin of check_doc_voice.py. That one reads Doxyfile.site
XML, so design.dox (developer INPUT only) is invisible to it. This one reads
the files. It does not change Doxyfile.site.

docs/BENCHMARKS.md and docs/MEASUREMENT.md are journals: listed in
docs/voice-journals.yaml with a reason. They are scanned and counted, never a
pass by silence, but they do not fail the check. Everything else in the
non-recursive docs/*.dox + docs/*.md set is fail-closed.

Same vocabulary as check_doc_voice.PATTERNS -- one list.

Usage:
    python3 tools/check_doc_voice_source.py
    python3 tools/check_doc_voice_source.py --self-test
"""

from __future__ import annotations

import contextlib
import io
import os
import sys
import tempfile
from collections import Counter
from pathlib import Path

_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, _HERE)
from check_doc_voice import PATTERNS  # noqa: E402

_REPO = os.path.dirname(_HERE)
_DOCS = os.path.join(_REPO, "docs")
_JOURNALS = os.path.join(_DOCS, "voice-journals.yaml")


def _die(msg: str) -> None:
    print(f"check_doc_voice_source: FAIL -- {msg}", file=sys.stderr)
    sys.exit(1)


def load_journals(journals_path: str = _JOURNALS) -> dict[str, str]:
    """Minimal YAML: `docs/NAME.md: reason` per line."""
    if not os.path.isfile(journals_path):
        _die(f"{journals_path} not found")
    out: dict[str, str] = {}
    for lineno, raw in enumerate(open(journals_path, encoding="utf-8"), 1):
        line = raw.split("#", 1)[0].rstrip()
        if not line.strip():
            continue
        path, sep, reason = line.partition(":")
        if not sep or not path.strip() or not reason.strip():
            _die(f"{journals_path}:{lineno}: expected 'docs/FILE: reason'")
        out[path.strip()] = reason.strip()
    return out


def scan_files(docs_dir: str = _DOCS) -> list[Path]:
    docs = Path(docs_dir)
    return sorted(p for p in list(docs.glob("*.md")) + list(docs.glob("*.dox")) if p.is_file())


def hits_in(path: Path) -> list[tuple[int, str]]:
    found: list[tuple[int, str]] = []
    for i, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        for pat, label in PATTERNS:
            if pat.search(line):
                found.append((i, label))
                break
    return found


def judge(repo: str = _REPO) -> int:
    """Judges repo/docs against repo/docs/voice-journals.yaml; a refusal exits through _die."""
    docs_dir = os.path.join(repo, "docs")
    journals = load_journals(os.path.join(docs_dir, "voice-journals.yaml"))
    files = scan_files(docs_dir)
    if not files:
        _die("no docs/*.md or docs/*.dox -- nothing was scanned")
    rels = {p.relative_to(repo).as_posix(): p for p in files}
    for named in journals:
        if named not in rels:
            _die(f"{named} is in voice-journals.yaml but not in docs/*.md or docs/*.dox")

    journal_rows: list[str] = []
    fail_rows: list[str] = []
    fail_n = 0
    for rel, path in rels.items():
        hits = hits_in(path)
        if rel in journals:
            counts: Counter[str] = Counter(label for _ln, label in hits)
            summary = ", ".join(f"{k}={v}" for k, v in sorted(counts.items())) or "0"
            journal_rows.append(
                f"  {rel}  ({len(hits)} hits: {summary})\n    journal: {journals[rel]}"
            )
            continue
        for ln, label in hits:
            fail_rows.append(f"  {rel}:{ln}: {label}")
            fail_n += 1

    print("check_doc_voice_source: journals (counted, not a fail):")
    print("\n".join(journal_rows) if journal_rows else "  (none)")
    if fail_rows:
        print(
            f"check_doc_voice_source: FAILED -- {fail_n} hit(s) in files that "
            "are not journals:"
        )
        print("\n".join(fail_rows))
        print(
            "  Cut the journal voice, or name the file in docs/voice-journals.yaml "
            "with a reason."
        )
        return 1
    print(
        f"check_doc_voice_source: clean -- {len(files)} file(s) scanned, "
        "no route vocabulary outside named journals"
    )
    return 0


_ARMS = {
    "nojournals": "voice-journals.yaml not found",
    "badline": "expected 'docs/FILE: reason'",
    "empty": "nothing was scanned",
    "ghost": "is in voice-journals.yaml but not in",
    "hit": "hit(s) in files that are not journals",
    "clean": "no route vocabulary outside named journals",
}


def self_test() -> int:
    """Drives each arm of ``judge`` alone on a synthetic docs/ tree.

    A refusal leaves through _die (SystemExit), which is captured with the printed text; any other
    exception is that case's failure. A journal's hits must be COUNTED, never a pass by silence.
    """
    bench_voice = "The walk costs +12.5 % here.\n"  # a measured percentage: bench-log voice
    journal = "docs/LOG.md: the measurement journal\n"
    cases = [
        ("no journals file", {"docs/a.md": "plain\n"}, None, 1, "nojournals"),
        ("a journals line without a reason", {"docs/a.md": "plain\n"}, "docs/LOG.md:\n", 1, "badline"),
        ("no docs/*.md or *.dox at all", {"docs/sub/a.md": "plain\n"}, "", 1, "empty"),
        ("a journal named but absent", {"docs/a.md": "plain\n"}, journal, 1, "ghost"),
        ("bench voice outside a journal", {"docs/a.md": bench_voice, "docs/LOG.md": "x\n"}, journal, 1, "hit"),
        ("bench voice in a .dox outside a journal", {"docs/d.dox": bench_voice, "docs/LOG.md": "x\n"}, journal, 1,
         "hit"),
        # The journals file's comment and blank lines are skipped, not read as malformed entries.
        ("bench voice inside a named journal is counted, not failed",
         {"docs/a.md": "plain\n", "docs/LOG.md": bench_voice}, "# journals\n\n" + journal, 0, "clean"),
        ("bench voice below docs/ is out of scope (non-recursive)",
         {"docs/a.md": "plain\n", "docs/sub/x.md": bench_voice, "docs/LOG.md": "x\n"}, journal, 0, "clean"),
    ]
    failures = 0
    for name, files, journals_text, want_rc, arm in cases:
        with tempfile.TemporaryDirectory() as tmp:
            for rel, content in files.items():
                path = Path(tmp) / rel
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(content, encoding="utf-8")
            if journals_text is not None:
                (Path(tmp) / "docs").mkdir(exist_ok=True)
                (Path(tmp) / "docs" / "voice-journals.yaml").write_text(journals_text, encoding="utf-8")
            out, err = io.StringIO(), io.StringIO()
            try:
                with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
                    rc = judge(tmp)
            except SystemExit as stop:
                rc = stop.code if isinstance(stop.code, int) else 1
            except Exception as exc:
                print(f"SELF-TEST FAILED: {name}: judge raised {type(exc).__name__}: {exc}")
                failures += 1
                continue
        text = out.getvalue() + err.getvalue()
        wrong = [a for a, m in _ARMS.items() if a != arm and m in text]
        counted = arm != "clean" or "journal" not in name or "(1 hits:" in text
        if rc != want_rc or _ARMS[arm] not in text or wrong or not counted:
            print(f"SELF-TEST FAILED: {name}: rc={rc} (want {want_rc}), arm {arm!r} "
                  f"{'present' if _ARMS[arm] in text else 'ABSENT'}, other arms {wrong}, counted={counted}"
                  f"\n    {text.strip()}")
            failures += 1
    if failures:
        print(f"check_doc_voice_source: self-test FAILED ({failures} of {len(cases)} case(s))")
        return 1
    print(f"check_doc_voice_source: self-test OK — {len(cases)} cases: a missing or malformed journals file, an "
          "empty scan, a ghost journal and bench voice in .md and .dox each refused alone; a journal's hits counted")
    return 0


def main() -> int:
    if sys.argv[1:] == ["--self-test"]:
        return self_test()
    return judge()


if __name__ == "__main__":
    sys.exit(main())
