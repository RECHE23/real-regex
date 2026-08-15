#!/usr/bin/env python3
"""Remove Sphinx pages that are not pages, and retarget their inbound links.

``contents.html`` is the root_doc artefact (Sphinx cannot emit the landing
there). ``bindings/`` was an orphan stub. Neither is a document. After a
``sphinx-build`` they still exist, and every inner page's Home / rel=prev
points at contents.html. Rewrite those hrefs to the landing, then delete
the artefacts. A silent leftover is a published non-page -- fail closed
if contents.html is still there afterwards.
"""

from __future__ import annotations

import re
import shutil
import sys
from pathlib import Path

_HREF = re.compile(r"""(?P<pre>href=(?P<q>['"]))(?P<rel>(?:\.\./)*)contents\.html(?P<frag>#[^'"]*)?(?P<q2>['"])""")

_SKIP_ROOTS = {"api", "coverage", "_static", "_sphinx_design_static"}


def _die(msg: str) -> None:
    print(f"strip_sphinx_artefacts: FAIL -- {msg}", file=sys.stderr)
    sys.exit(1)


def _rewrite(text: str) -> str:
    def _sub(m: re.Match[str]) -> str:
        return f"{m.group('pre')}{m.group('rel')}index.html{m.group('frag') or ''}{m.group('q2')}"

    return _HREF.sub(_sub, text)


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        _die("usage: strip_sphinx_artefacts.py SITE_HTML")
    root = Path(argv[1])
    if not (root / "index.html").is_file():
        _die(f"{root}/index.html missing -- not a docs-site tree")

    for path in root.rglob("*"):
        if not path.is_file():
            continue
        rel = path.relative_to(root)
        if rel.parts and rel.parts[0] in _SKIP_ROOTS:
            continue
        if path.suffix not in {".html", ".js"}:
            continue
        text = path.read_text(encoding="utf-8")
        new = _rewrite(text)
        if new != text:
            path.write_text(new, encoding="utf-8")

    (root / "contents.html").unlink(missing_ok=True)
    shutil.rmtree(root / "bindings", ignore_errors=True)
    (root / "_sources" / "contents.md.txt").unlink(missing_ok=True)
    shutil.rmtree(root / "_sources" / "bindings", ignore_errors=True)

    if (root / "contents.html").exists():
        _die("contents.html still present after strip")
    leftover = [
        p.relative_to(root).as_posix()
        for p in root.rglob("*.html")
        if p.parts and p.relative_to(root).parts[0] not in _SKIP_ROOTS
        and "contents.html" in p.read_text(encoding="utf-8")
    ]
    if leftover:
        _die("contents.html href remains in: " + ", ".join(leftover[:8]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
