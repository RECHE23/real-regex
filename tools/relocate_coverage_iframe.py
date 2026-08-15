#!/usr/bin/env python3
"""Point /api/coverage.html's iframe at the single published /coverage tree.

``make doc`` embeds the llvm-cov report next to Doxygen HTML
(``coverage/index.html``). ``make docs-site`` copies that page to /api and
keeps one report at /coverage, so the iframe and the no-iframe fallback must
become ``../coverage/index.html``. A silent no-op is a 404 -- fail closed if
neither the local nor the relocated form is present.
"""

from __future__ import annotations

import sys
from pathlib import Path

OLD_SRC = 'src="coverage/index.html"'
NEW_SRC = 'src="../coverage/index.html"'
OLD_HREF = 'href="coverage/index.html"'
NEW_HREF = 'href="../coverage/index.html"'


def _die(msg: str) -> None:
    print(f"relocate_coverage_iframe: FAIL -- {msg}", file=sys.stderr)
    sys.exit(1)


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        _die("usage: relocate_coverage_iframe.py API_COVERAGE_HTML")
    path = Path(argv[1])
    if not path.is_file():
        _die(f"{path} missing -- Doxygen \\page coverage was not generated")
    text = path.read_text(encoding="utf-8")
    if NEW_SRC in text and NEW_HREF in text and OLD_SRC not in text:
        return 0
    if OLD_SRC not in text or OLD_HREF not in text:
        _die(
            f"{path} has no iframe src/href='coverage/index.html' to relocate "
            "(Doxygen page shape changed?)"
        )
    text = text.replace(OLD_SRC, NEW_SRC).replace(OLD_HREF, NEW_HREF)
    if OLD_SRC in text or "src=\"coverage/index.html\"" in text:
        _die(f"{path} still points at coverage/index.html after rewrite")
    path.write_text(text, encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
