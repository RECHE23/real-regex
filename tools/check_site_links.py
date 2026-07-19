#!/usr/bin/env python3
"""check_site_links.py — the built-HTML link check for docs/site (doc-site P1c;
extended site-wide for doc-site P2a).

Sphinx's own `linkcheck` builder only walks the SOURCE documents (docs/site/*.md,
*.rst) it parses into a doctree. docs/site/_templates/landing.html is a Jinja2
template rendered straight to HTML via `html_additional_pages` (conf.py) -- it is
never a document, so linkcheck never sees a single href/src inside it. Worse,
linkcheck never resolves a URL FRAGMENT at all (`linkcheck_anchors = False` in
conf.py) -- so even a source-document link into build/doc/html's Doxygen output
(`../api/divergences.html#div_rejected`, a forward-ref used by the doc-site P2a
Drop-in pages until those targets migrate into the site, see the doc-site fiche)
is invisible to it on BOTH counts: the target lives outside the doctree, and the
fragment is never checked regardless. This script closes both gaps by parsing the
BUILT output HTML directly -- the same files a visitor's browser would load -- and
checking each internal link's target file AND (when present) its #fragment via a
literal `id="..."` search in the target file's own text.

Site-wide (doc-site P2a): by default this walks every `*.html` file under
SITE_ROOT -- not just the bespoke landing -- EXCEPT it does not descend into the
`api/` (Doxygen, copied wholesale by `make docs-site`) or `coverage/` (llvm-cov)
subtrees to find pages to scan: their own internal links are that tool's own gate
(`make doc`/`doc-check`'s WARN_AS_ERROR, `make coverage-check`), not this script's.
A link FROM a site page INTO `api/`/`coverage/` is still checked like any other
internal target, file and fragment alike -- only the trees are excluded as a
*source* of pages to walk.

Usage:
    python3 tools/check_site_links.py [SITE_ROOT] [PAGE]

    SITE_ROOT defaults to build/site/html (docs-site's release output -- the exact
    tree `make docs-site` produces and docs.yml deploys).
    PAGE, if given, restricts the check to that one page (relative to SITE_ROOT) --
    the original doc-site P1c interface, preserved for a quick single-page check.
    Omitted (the default, and what docs-site-gate now invokes), every page under
    SITE_ROOT is walked (see above).

Every internal href/src (no scheme, not mailto:/tel:, not a bare `#fragment`) is
resolved against the LINKING PAGE's own directory (root-relative "/..." hrefs
resolve against SITE_ROOT instead, matching normal browser resolution) and MUST
exist as a file there; if the href also carries a `#fragment`, that fragment MUST
appear as `id="fragment"` somewhere in the target file's text. Either failure is a
missing target -- exits non-zero (docs-site-gate: FAIL). External links
(http(s)://, mailto:, ...) are listed for the record only, never fetched here --
flaky in CI, and every external link reachable from the SOURCE documents is
already covered by Sphinx's own linkcheck builder.
"""

from __future__ import annotations

import sys
from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import urlsplit

# Top-level directories that are never a *source* of pages to scan, only ever a
# possible *target*: `api`/`coverage` are copied wholesale from another tool's own
# gated build (Doxygen's WARN_AS_ERROR `make doc`/`doc-check`, llvm-cov's `make
# coverage-check`) -- their internal links are that tool's responsibility, not this
# script's. `_static` is Sphinx/theme asset space (html_static_path), never real
# site pages -- pydata_sphinx_theme itself ships a raw, unrendered Jinja2 macro
# partial there (webpack-macros.html, `{% macro %}`/`{{ pathto(...) }}` and all)
# that Sphinx copies verbatim as a static file; parsing it as if it were a page
# produces bogus "missing target" noise from its literal `{{ ... }}` placeholders.
# A link FROM a real site page INTO any of these trees is still checked like any
# other internal target, file and fragment alike -- only the trees themselves are
# excluded as a scan root.
_EXCLUDED_SCAN_ROOTS = {"api", "coverage", "_static"}


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


def _iter_site_pages(site_root: Path) -> list[Path]:
    """Every *.html file under site_root, except inside _EXCLUDED_SCAN_ROOTS."""
    pages = []
    for path in sorted(site_root.rglob("*.html")):
        rel_parts = path.relative_to(site_root).parts
        if rel_parts and rel_parts[0] in _EXCLUDED_SCAN_ROOTS:
            continue
        pages.append(path)
    return pages


def _resolve_internal(target_path: str, *, page_dir: Path, site_root: Path) -> Path:
    """Resolve an internal href's path component (no query/fragment) the way a
    browser would: relative to the LINKING page's own directory, or -- for a
    root-relative "/..." href -- relative to site_root.
    """
    if target_path.startswith("/"):
        return (site_root / target_path.lstrip("/")).resolve()
    return (page_dir / target_path).resolve()


def _check_page(
    page_path: Path,
    *,
    site_root: Path,
    site_root_resolved: Path,
    anchor_cache: dict[Path, str | None],
) -> tuple[int, int, list[str], list[str]]:
    """Check every internal href/src on one page.

    Returns (unique_link_count, internal_checked_count, missing, external).
    `missing` entries are already-formatted, human-readable strings.
    """
    collector = _LinkCollector()
    collector.feed(page_path.read_text(encoding="utf-8"))

    seen: set[str] = set()
    external: list[str] = []
    missing: list[str] = []
    internal_count = 0

    for url in collector.links:
        if url in seen:
            continue
        seen.add(url)

        if url.startswith("#"):
            continue  # same-page anchor: Sphinx/MyST's own build already
            # guarantees these resolve (heading permalinks, in-page toc) --
            # not a cross-file target, not this script's concern.

        if not _is_internal(url):
            external.append(url)
            continue

        split = urlsplit(url)
        target = split.path
        if not target:
            continue  # a same-page "?x=y"-only URL, nothing to resolve

        internal_count += 1
        resolved = _resolve_internal(target, page_dir=page_path.parent, site_root=site_root)
        try:
            resolved.relative_to(site_root_resolved)
        except ValueError:
            missing.append(f"{url}  (escapes {site_root})")
            continue

        if not resolved.is_file():
            missing.append(f"{url}  (target file missing: {resolved.relative_to(site_root_resolved)})")
            continue

        fragment = split.fragment
        if fragment:
            if resolved not in anchor_cache:
                try:
                    anchor_cache[resolved] = resolved.read_text(encoding="utf-8", errors="replace")
                except OSError:
                    anchor_cache[resolved] = None
            text = anchor_cache[resolved]
            if text is None or f'id="{fragment}"' not in text:
                missing.append(
                    f"{url}  (anchor #{fragment} not found in "
                    f"{resolved.relative_to(site_root_resolved)})"
                )

    return len(seen), internal_count, missing, external


def main(argv: list[str]) -> int:
    site_root = Path(argv[1]) if len(argv) > 1 else Path("build/site/html")
    explicit_page = argv[2] if len(argv) > 2 else None

    if not site_root.is_dir():
        print(
            f"check_site_links: FAIL -- {site_root} does not exist "
            "(run `make docs-site` first to produce the release build)"
        )
        return 1

    site_root_resolved = site_root.resolve()

    if explicit_page is not None:
        pages = [site_root / explicit_page]
        if not pages[0].is_file():
            print(
                f"check_site_links: FAIL -- {pages[0]} does not exist "
                "(run `make docs-site` first to produce the release build)"
            )
            return 1
    else:
        pages = _iter_site_pages(site_root)

    anchor_cache: dict[Path, str | None] = {}
    total_internal = 0
    all_external: set[str] = set()
    fail_count = 0

    for page_path in pages:
        rel_page = page_path.relative_to(site_root)
        unique_count, internal_count, missing, external = _check_page(
            page_path,
            site_root=site_root,
            site_root_resolved=site_root_resolved,
            anchor_cache=anchor_cache,
        )
        total_internal += internal_count
        all_external.update(external)

        if missing:
            fail_count += len(missing)
            print(
                f"check_site_links: FAIL -- {rel_page} -- {unique_count} unique href/src "
                f"({internal_count} internal checked) -- {len(missing)} missing:"
            )
            for m in missing:
                print(f"    {m}")
        else:
            print(
                f"check_site_links: {rel_page} -- {unique_count} unique href/src "
                f"({internal_count} internal checked, {len(external)} external listed only)"
            )

    print(
        f"check_site_links: scanned {len(pages)} page(s) under {site_root} -- "
        f"{total_internal} internal href/src checked, {len(all_external)} distinct "
        "external link(s) listed only (not fetched)"
    )

    if fail_count:
        print(f"check_site_links: FAIL -- {fail_count} internal target(s)/anchor(s) missing (see above)")
        return 1

    print(
        f"check_site_links: PASS -- every internal href/src across {len(pages)} "
        f"page(s) resolves (file + #anchor where present) under {site_root}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
