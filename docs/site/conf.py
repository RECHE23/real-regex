"""Sphinx configuration for docs/site — the bespoke landing (doc-site P1c) + the
Breathe-powered API reference scaffold for the inner pages (doc-site P1). Additive:
does not affect `make doc` (the live Doxygen HTML site, docs.yml) — see Doxyfile's
`EXCLUDE += docs/site` (P0).

Build via `make docs-site` (release) / `make docs-site-gate` (the site's own -W +
linkcheck + built-HTML-link net). Requires the pinned toolchain in
docs/requirements.txt, installed into an isolated venv (never the system interpreter).
"""

import re
import textwrap
from pathlib import Path

from pygments import highlight
from pygments.formatters import HtmlFormatter
from pygments.lexers import CppLexer, GoLexer, PythonLexer, RustLexer

_HERE = Path(__file__).resolve().parent  # docs/site
_ROOT = _HERE.parent.parent  # repository root

# -- Project information -----------------------------------------------------

project = "real::regex"
copyright = "2026, René Chenard"
author = "René Chenard"

# Version: DERIVED from pyproject.toml, never hardcoded here -- the same discipline
# `make version-check` enforces for CMakeLists.txt / version.hpp / Cargo.toml / etc.
# pyproject.toml's own header comment names it the single source of truth.
_pyproject_text = (_ROOT / "pyproject.toml").read_text(encoding="utf-8")
_version_match = re.search(r'(?m)^version\s*=\s*"([0-9]+\.[0-9]+\.[0-9]+)"', _pyproject_text)
if _version_match is None:
    raise RuntimeError(
        "docs/site/conf.py: could not derive the version from pyproject.toml "
        '(expected a top-level version = "X.Y.Z" line)'
    )
version = release = _version_match.group(1)

# -- General configuration ----------------------------------------------------

extensions = [
    "myst_parser",
    "breathe",
    "sphinx_design",
]

# root_doc is "contents", not "index" (doc-site P1c). The bespoke landing owns
# "index.html" via html_additional_pages below -- Sphinx cannot render a source
# document AND an additional page to the same output name, so the source root
# moved aside. contents.md carries no landing content of its own; its only job is
# to be a valid root document and hold the hidden toctree that keeps reference/index
# out of the orphan check -- see contents.md's own comment.
root_doc = "contents"
source_suffix = {
    ".rst": "restructuredtext",
    ".md": "markdown",
}

# _templates holds landing.html, the bespoke root template (doc-site P1c) --
# html_additional_pages below resolves against this search path.
templates_path = ["_templates"]

# colon_fence: sphinx-design containers (grid/card/tab-set) open with ::: fences so
# they don't collide with the ``` fences used by the code samples nested inside them.
myst_enable_extensions = [
    "colon_fence",
    "deflist",
]

exclude_patterns = ["_build", "Thumbs.db", ".DS_Store"]

# nitpicky: an unresolved cross-reference is a build failure here, not a silent
# warning -- the site's own version of the engine's WARN_AS_ERROR discipline.
nitpicky = True

# Four narrow, justified exceptions. real::basic_regex's own Doxygen comment
# (real.hpp) \ref-links real::regex, real::static_regex, and the two Storage
# policies (real::detail::dynamic_storage / real::detail::static_storage) --
# Breathe turns those into :ref: targets. docs/site/reference/index.rst intentionally
# renders only basic_regex itself (rendering the other four's concrete,
# template-instantiated signatures would pull in real::fixed_string / real::flags
# / the real::detail namespace transitively -- out of scope for this wagon's
# single proof page; see reference/index.rst's own comment and the doc-site fiche's
# P2-P4 deferral). Without these entries, nitpicky correctly fails the build
# on these dangling \ref targets; this is the "mark linkcheck_ignore explicit"
# pattern applied to the Sphinx xref net instead -- an explicit, reviewed
# exception, not a silently-skipped check.
#
# The two struct refids below are NAME-derived and stable across Doxygen
# versions. The namespace-MEMBER refids (real::regex / real::static_regex, the
# `using` aliases) carry a Doxygen-generated HASH that differs between Doxygen
# versions -- the first Docs-site CI run proved it (local Homebrew 1.17 vs CI
# apt 1.9.8 emitted different hashes, and pinning one version's hash reds the
# other). So those go through nitpick_ignore_regex on the anchor PATTERN,
# version-independent by construction. Scope stays narrow: only anchored
# members of `namespace real` itself, only until the real pages exist (P2-P4).
nitpick_ignore = [
    ("ref", "structreal_1_1detail_1_1dynamic__storage"),
    ("ref", "structreal_1_1detail_1_1static__storage"),
]
nitpick_ignore_regex = [
    ("ref", r"namespacereal_1a[0-9a-f]+"),  # real::regex / real::static_regex (hashed member anchors)
]

pygments_style = "tango"

# -- Breathe (Doxygen-XML -> Sphinx bridge) -----------------------------------
# Consumes the XML sidecar Doxyfile now produces (P0: GENERATE_XML/XML_OUTPUT).
# `make docs-site` runs `doxygen Doxyfile` before sphinx-build so this path exists.
breathe_projects = {"real": "../../build/doc/xml"}
breathe_default_project = "real"

# -- HTML output ---------------------------------------------------------------

html_theme = "pydata_sphinx_theme"
html_title = f"{project} {version}"
html_static_path = ["_static"]
html_css_files = ["real.css"]
html_js_files = ["curve.js"]

html_theme_options = {
    "github_url": "https://github.com/RECHE23/real-regex",
    "show_prev_next": False,
    "navigation_with_keys": False,
    "pygments_light_style": "tango",
    "pygments_dark_style": "monokai",
    # "index" is not a source document any more (root_doc is "contents", doc-site
    # P1c) but `pathto("index")` still resolves to "index.html" regardless --
    # that file exists, rendered by html_additional_pages below. So the inner
    # pages' pydata navbar brand still points at a real place: the bespoke landing.
    "logo_link": "index",
}

# The bespoke root landing (doc-site P1c): docs/site/_templates/landing.html is a
# self-contained template (its own <style>/<script>, no pydata layout) rendered to
# "index.html" -- NOT a source document, so it needs no entry in source_suffix and
# is invisible to the normal doctree/toctree machinery. The value MUST carry the
# ".html" suffix (Sphinx's Jinja2 template loader does no implicit extension
# lookup -- confirmed empirically; a bare "landing" 404s at build time with
# TemplateNotFound).
html_additional_pages = {"index": "landing.html"}

# -- linkcheck -------------------------------------------------------------
# Every hyperlink on the inner pages (contents.md, reference/index.rst) resolves to
# a real, reachable destination or an internal doc reference. linkcheck_ignore is
# added only if a specific, justified exception is found.
#
# linkcheck walks SOURCE documents only -- docs/site/_templates/landing.html is a
# Jinja2 template rendered via html_additional_pages above, never parsed as a
# document, so linkcheck cannot see a single href inside it (the P1c review gap:
# a broken link in the bespoke landing would stay invisible to this net forever).
# `tools/check_site_links.py`, wired into `docs-site-gate` (Makefile), closes that
# blind spot by parsing the BUILT build/site/html/index.html directly.
linkcheck_anchors = False

# -- quickstart injection (doc-site P1c: the gate-snippet invariant, preserved) ----
#
# landing.html has NO quickstart code of its own -- only {{ quickstart_cpp }} /
# {{ quickstart_py }} / {{ quickstart_rs }} / {{ quickstart_go }} placeholders. This
# hook reads the SAME 4 tested files, through the SAME region-marker comments, that
# doc-site P1b-A's {literalinclude} directives used on the old pydata-rendered
# index.md, so the landing keeps showing exactly the code the per-language CI jobs
# (c-test/example-check, python-test, rust-test, go-test) just compiled and ran --
# never a hand-copied illustration that can silently drift from the real API.
#
# Coloring goes through Pygments (highlight(code, lexer, HtmlFormatter)), not raw
# escaping -- the approved mockup shows colored code, so a flat <pre> would fail the
# "built root == mockup" fidelity bar on every code block. The matching CSS lives in
# landing.html's own <style> (Pygments' short token classes -- .k/.s/.c/.nf/...,
# NOT the mockup's original hand-authored .k/.s/.c/.t/.f/.n spans, which no longer
# exist once real syntax highlighting replaces them), tuned to the same
# --accent-ink/--danger/--ink/--ink-2/--ink-3 custom properties as the rest of the
# page so it re-tints for free on the landing's light/dark toggle.
_PYGMENTS_FORMATTER = HtmlFormatter(nowrap=True)


def _extract_region(text: str, start_marker: str, end_marker: str) -> str:
    """Return the text strictly between a start-after marker LINE and an end-before
    marker LINE -- the same start-after/end-before contract Sphinx's own
    {literalinclude} directive used here before P1c (docs/site/index.md, doc-site
    P1b-A), so each snippet file's own region-marker comments stay the single
    source of truth for what the landing displays. `str.index` is a first-occurrence
    search, matching {literalinclude}'s own behavior (see examples/cpp/quickstart.cpp
    and bindings/rust/examples/quickstart.rs's header comments on why the marker
    pair is never spelled out a second time in-file).
    """
    start = text.index(start_marker)
    start = text.index("\n", start) + 1
    end = text.index(end_marker, start)
    return text[start:end]


def _highlight_region(path: Path, lexer, *, start_marker: str, end_marker: str) -> str:
    code = _extract_region(path.read_text(encoding="utf-8"), start_marker, end_marker)
    code = textwrap.dedent(code).strip("\n")
    return highlight(code, lexer, _PYGMENTS_FORMATTER)


def _highlight_go_quickstart(path: Path) -> str:
    # bindings/go/quickstart_example_test.go cannot use one start-after/end-before
    # pair (Go requires every import ahead of all other top-level declarations, so
    # the import line and the two body statements aren't contiguous) -- its own
    # header comment documents a `[quickstart-import]` / `[quickstart-body]` marker
    # PAIR instead of raw line numbers as the selector. Reading both markers here
    # (rather than hardcoding the line numbers the old {literalinclude} :lines:
    # option used) means a resequenced line inside the file can no longer go
    # silently stale on the page -- the exact fragility that file's own comment
    # warns a stale :lines: selector could not otherwise catch.
    text = path.read_text(encoding="utf-8")
    imp = _extract_region(text, "// [quickstart-import]", "// [/quickstart-import]").strip("\n")
    body = _extract_region(text, "// [quickstart-body]", "// [/quickstart-body]").strip("\n")
    return highlight(f"{imp}\n\n{body}", GoLexer(), _PYGMENTS_FORMATTER)


def _inject_quickstart(app, pagename, templatename, context, doctree):
    """`html-page-context` event hook: only the bespoke root ("index", rendered from
    landing.html via html_additional_pages) needs the quickstart regions -- every
    other page leaves context untouched.
    """
    if pagename != "index":
        return
    context["quickstart_cpp"] = _highlight_region(
        _ROOT / "examples" / "cpp" / "quickstart.cpp",
        CppLexer(),
        start_marker="// [quickstart]",
        end_marker="// [/quickstart]",
    )
    context["quickstart_py"] = _highlight_region(
        _ROOT / "bindings" / "python" / "examples" / "quickstart.py",
        PythonLexer(),
        start_marker="# [quickstart]",
        end_marker="# [/quickstart]",
    )
    context["quickstart_rs"] = _highlight_region(
        _ROOT / "bindings" / "rust" / "examples" / "quickstart.rs",
        RustLexer(),
        start_marker="// [quickstart]",
        end_marker="// [/quickstart]",
    )
    context["quickstart_go"] = _highlight_go_quickstart(
        _ROOT / "bindings" / "go" / "quickstart_example_test.go"
    )


def setup(app):
    app.connect("html-page-context", _inject_quickstart)
