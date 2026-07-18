#!/usr/bin/env python3
"""check_site_links.py — the built-HTML link check for docs/site's bespoke landing
(doc-site P1c).

Sphinx's own `linkcheck` builder only walks the SOURCE documents (docs/site/*.md,
*.rst) it parses into a doctree. docs/site/_templates/landing.html is a Jinja2
template rendered straight to HTML via `html_additional_pages` (conf.py) -- it is
never a document, so linkcheck never sees a single href/src inside it. That left a
real gap: a broken link in the bespoke landing (the site's actual front door) could
ship and stay green forever under `-W --keep-going` + linkcheck. This script closes
it by parsing the BUILT output HTML directly, the same file a visitor's browser
would load.

Usage:
    python3 tools/check_site_links.py [SITE_ROOT] [PAGE]

    SITE_ROOT defaults to build/site/html (docs-site's release output -- the exact
    tree `make docs-site` produces and docs.yml deploys).
    PAGE defaults to index.html (the bespoke landing; every other page IS a Sphinx
    source document, already covered by linkcheck).

Every internal href/src (no scheme, not mailto:/tel:, not a bare `#fragment`) is
resolved against SITE_ROOT and MUST exist as a file there -- a missing target exits
non-zero (docs-site-gate: FAIL). External links (http(s)://, mailto:, ...) are
listed for the record only, never fetched here -- flaky in CI, and every external
link reachable from the SOURCE documents is already covered by Sphinx's own
linkcheck builder.
"""

from __future__ import annotations

import sys
from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import urlsplit


class _LinkCollector(HTMLParser):
    def __init__(self) -> None:
        super().__init__()
        self.links: list[str] = []

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        for name, value in attrs:
            if name in ("href", "src") and value:
                self.links.append(value)


def _is_internal(url: str) -> bool:
    if not url or url.startswith("#"):
        return False
    if url.startswith(("mailto:", "tel:", "javascript:")):
        return False
    parts = urlsplit(url)
    return not parts.scheme and not parts.netloc


def main(argv: list[str]) -> int:
    site_root = Path(argv[1]) if len(argv) > 1 else Path("build/site/html")
    page = argv[2] if len(argv) > 2 else "index.html"
    page_path = site_root / page

    if not page_path.is_file():
        print(
            f"check_site_links: FAIL -- {page_path} does not exist "
            "(run `make docs-site` first to produce the release build)"
        )
        return 1

    collector = _LinkCollector()
    collector.feed(page_path.read_text(encoding="utf-8"))

    site_root_resolved = site_root.resolve()
    seen: set[str] = set()
    external: list[str] = []
    missing: list[str] = []

    internal_count = 0
    for url in collector.links:
        if url in seen:
            continue
        seen.add(url)

        if url.startswith("#"):
            continue  # same-page anchor -- neither an internal file target nor external

        if not _is_internal(url):
            external.append(url)
            continue

        target = urlsplit(url).path  # drop any ?query / #fragment
        if not target:
            continue  # a same-page "?x=y"-only URL, nothing to resolve

        internal_count += 1
        resolved = (site_root / target.lstrip("/")).resolve()
        try:
            resolved.relative_to(site_root_resolved)
        except ValueError:
            missing.append(f"{url}  (escapes {site_root})")
            continue
        if not resolved.is_file():
            missing.append(url)

    print(
        f"check_site_links: {page} -- {len(seen)} unique href/src "
        f"({internal_count} internal checked, {len(external)} external listed only)"
    )
    if external:
        print("  external (report only, not fetched):")
        for url in sorted(external):
            print(f"    {url}")

    if missing:
        print(f"check_site_links: FAIL -- {len(missing)} internal target(s) missing under {site_root}:")
        for url in missing:
            print(f"    {url}")
        return 1

    print(f"check_site_links: PASS -- every internal href/src in {page} resolves under {site_root}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
