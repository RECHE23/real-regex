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


class _NavExtractor(HTMLParser):
    """Collect (label, href) for every <a> inside the FIRST element matching
    (container_tag, class-substring) -- the nav-equality net's parser (doc-site
    P1 reorg). First match only: pydata renders its primary nav twice per page
    (header + mobile drawer), identically by construction.
    """

    def __init__(self, container_tag: str, container_class: str) -> None:
        super().__init__()
        self._tag = container_tag
        self._cls = container_class
        self._depth = 0  # >0 while inside the (first) matching container
        self._done = False
        self._in_a = False
        self._href = ""
        self._label: list[str] = []
        self.items: list[tuple[str, str]] = []

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        a = dict(attrs)
        if self._depth == 0:
            if (
                not self._done
                and tag == self._tag
                and self._cls in _class_tokens(a)
            ):
                self._depth = 1
            return
        if tag == self._tag:
            self._depth += 1
        if tag == "a":
            self._in_a = True
            self._href = a.get("href") or ""
            self._label = []

    def handle_endtag(self, tag: str) -> None:
        if self._depth == 0:
            return
        if tag == "a" and self._in_a:
            label = " ".join("".join(self._label).replace("\xa0", " ").split())
            self.items.append((label, self._href))
            self._in_a = False
        if tag == self._tag:
            self._depth -= 1
            if self._depth == 0:
                self._done = True

    def handle_data(self, data: str) -> None:
        if self._in_a:
            self._label.append(data)


def _extract_primary_nav(
    page_path: Path, container_tag: str, container_class: str, *, site_root: Path
) -> list[tuple[str, str]]:
    """(label, canonical-target) list for a page's primary nav. Hrefs resolve
    against the page's own directory; the pydata ACTIVE item's href is "#"
    (self-reference) and canonicalizes to the page itself.
    """
    extractor = _NavExtractor(container_tag, container_class)
    extractor.feed(page_path.read_text(encoding="utf-8"))
    site_root_resolved = site_root.resolve()
    out = []
    for label, href in extractor.items:
        if href in ("", "#"):
            target = page_path.resolve()
        else:
            target = _resolve_internal(
                urlsplit(href).path, page_dir=page_path.parent, site_root=site_root
            )
        out.append((label, target.relative_to(site_root_resolved).as_posix()))
    return out


def _class_tokens(attrs_dict: dict[str, str | None]) -> set[str]:
    """The element's class attribute as a set of whole tokens. Substring
    matching is a trap here twice over (supervision catch, doc-site P2):
    `"cmd" in class` also matches the OUTER `hero__cmd` wrapper around the
    hero's real `.cmd` box, and `"copy" in class` matches a hypothetical
    `nocopy` -- token matching is what the browser's own class selector does.
    """
    return set((attrs_dict.get("class") or "").split())


class _LandingCommandChecker(HTMLParser):
    """Parse the landing page for command blocks: checks that all shell commands
    are in copyable <div class="cmd"> format (never in <pre>), and that each
    <div class="cmd"> contains a <button class="copy">.
    """

    def __init__(self) -> None:
        super().__init__()
        self.in_cmd = False
        self.has_copy_button = False
        self.pre_lines: list[str] = []
        self.current_pre = ""
        self.in_pre = False
        self.pre_texts: list[str] = []
        self.errors: list[str] = []
        self.cmd_count = 0

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        a = dict(attrs)
        if tag == "div" and "cmd" in _class_tokens(a):
            self.in_cmd = True
            self.has_copy_button = False
            self.cmd_count += 1
        elif tag == "button" and self.in_cmd and "copy" in _class_tokens(a):
            self.has_copy_button = True
        elif tag == "pre":
            self.in_pre = True
            self.current_pre = ""

    def handle_endtag(self, tag: str) -> None:
        if tag == "div" and self.in_cmd:
            if not self.has_copy_button:
                self.errors.append(
                    "landing-commands: FAIL -- <div class=\"cmd\"> without <button class=\"copy\">"
                )
            self.in_cmd = False
        elif tag == "pre" and self.in_pre:
            self.in_pre = False
            if self.current_pre:
                self.pre_texts.append(self.current_pre)

    def handle_data(self, data: str) -> None:
        if self.in_pre:
            self.current_pre += data


def _check_landing_commands(site_root: Path) -> list[str]:
    """Check landing page commands are all copyable (in <div class="cmd"> with
    <button class="copy">), never in <pre> matching prompt/install patterns.
    Returns human-readable error strings; empty = PASS.
    """
    landing = site_root / "index.html"
    if not landing.is_file():
        return [f"landing-commands: landing index.html not found at {landing}"]

    checker = _LandingCommandChecker()
    try:
        checker.feed(landing.read_text(encoding="utf-8"))
    except Exception as e:
        return [f"landing-commands: FAIL -- error parsing {landing}: {e}"]

    # Check for <pre> blocks with shell command patterns (prompt or install keywords)
    import re

    prompt_pattern = re.compile(r"^\s*\$ ")
    install_pattern = re.compile(r"^\s*(brew|pip|cargo|go get)\s")

    errors = list(checker.errors)
    for pre_text in checker.pre_texts:
        for line in pre_text.split("\n"):
            if prompt_pattern.match(line) or install_pattern.match(line):
                errors.append(
                    f"landing-commands: FAIL -- found shell command in <pre>: {line.strip()}"
                )
                break

    # A landing with ZERO .cmd boxes is not "clean", it is missing (empty file,
    # botched render, restructure) -- an empty page must never PASS vacuously
    # (supervision catch, doc-site P2: a truncated index.html sailed through this
    # check while nav-equality correctly failed closed on it).
    if not checker.cmd_count:
        errors.append(
            "landing-commands: FAIL -- no <div class=\"cmd\"> found on the landing "
            "(the hero install box alone should give >= 1; empty or broken page?)"
        )

    return errors


def _check_nav_equality(site_root: Path) -> list[str]:
    """The nav-equality net (doc-site P1 reorg): the landing's injected nav and
    the pydata inner header MUST render the same {label -> target} list, same
    order. The two menus diverged for five wagons precisely because no gate
    watched them -- this assertion is why the single-source refactor can't rot.
    Returns human-readable error strings; empty = PASS.
    """
    landing = site_root / "index.html"
    # The inner witness is the ROOT DOC's own page (root_doc = "contents",
    # conf.py): it exists on every build by construction and can never be
    # renamed by a content wagon -- unlike a hardcoded content page (Opus
    # review note, doc-site P1). pydata renders the same header on every page.
    inner = site_root / "contents.html"
    for page in (landing, inner):
        if not page.is_file():
            return [f"nav-equality: {page} missing (run `make docs-site` first)"]

    landing_nav = _extract_primary_nav(
        landing, "div", "nav__links", site_root=site_root
    )
    inner_nav = _extract_primary_nav(
        inner, "ul", "bd-navbar-elements", site_root=site_root
    )

    errors = []
    if not landing_nav:
        errors.append("nav-equality: no nav found on the landing (div.nav__links)")
    if not inner_nav:
        errors.append(
            "nav-equality: no nav found on the inner page (ul.bd-navbar-elements)"
        )
    if errors:
        return errors

    if landing_nav != inner_nav:
        errors.append(
            "nav-equality: landing nav != inner header nav "
            "(same labels, same targets, same order required):"
        )
        errors.append(f"    landing: {landing_nav}")
        errors.append(f"    inner:   {inner_nav}")
    return errors


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

    nav_errors: list[str] = []
    landing_cmd_errors: list[str] = []
    if explicit_page is None:  # site-wide mode only: the gate's invocation
        nav_errors = _check_nav_equality(site_root)
        if nav_errors:
            fail_count += len(nav_errors)
            for e in nav_errors:
                print(f"check_site_links: FAIL -- {e}")
        else:
            print(
                "check_site_links: nav-equality PASS -- landing nav == inner "
                "header nav (single toctree source, doc-site P1 reorg)"
            )

        landing_cmd_errors = _check_landing_commands(site_root)
        if landing_cmd_errors:
            fail_count += len(landing_cmd_errors)
            for e in landing_cmd_errors:
                print(f"check_site_links: FAIL -- {e}")
        else:
            print(
                "check_site_links: landing-commands PASS -- all shell commands "
                "in copyable <div class=\"cmd\"> format (no <pre> with prompt/install patterns)"
            )

    if fail_count:
        print(f"check_site_links: FAIL -- {fail_count} internal target(s)/anchor(s)/nav item(s) missing (see above)")
        return 1

    print(
        f"check_site_links: PASS -- every internal href/src across {len(pages)} "
        f"page(s) resolves (file + #anchor where present) under {site_root}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
