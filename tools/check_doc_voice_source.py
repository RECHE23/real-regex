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
"""

from __future__ import annotations

import os
import sys
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


def load_journals() -> dict[str, str]:
    """Minimal YAML: `docs/NAME.md: reason` per line."""
    if not os.path.isfile(_JOURNALS):
        _die(f"{_JOURNALS} not found")
    out: dict[str, str] = {}
    for lineno, raw in enumerate(open(_JOURNALS, encoding="utf-8"), 1):
        line = raw.split("#", 1)[0].rstrip()
        if not line.strip():
            continue
        path, sep, reason = line.partition(":")
        if not sep or not path.strip() or not reason.strip():
            _die(f"{_JOURNALS}:{lineno}: expected 'docs/FILE: reason'")
        out[path.strip()] = reason.strip()
    return out


def scan_files() -> list[Path]:
    docs = Path(_DOCS)
    return sorted(p for p in list(docs.glob("*.md")) + list(docs.glob("*.dox")) if p.is_file())


def hits_in(path: Path) -> list[tuple[int, str]]:
    found: list[tuple[int, str]] = []
    for i, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        for pat, label in PATTERNS:
            if pat.search(line):
                found.append((i, label))
                break
    return found


def main() -> int:
    journals = load_journals()
    files = scan_files()
    rels = {p.relative_to(_REPO).as_posix(): p for p in files}
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
    print("check_doc_voice_source: clean -- no route vocabulary outside named journals")
    return 0


if __name__ == "__main__":
    sys.exit(main())
