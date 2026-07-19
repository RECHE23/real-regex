"""Sphinx configuration for docs/site — the bespoke landing (doc-site P1c) + the
Breathe-powered API reference scaffold for the inner pages (doc-site P1). Additive:
does not affect `make doc` (the live Doxygen HTML site, docs.yml) — see Doxyfile's
`EXCLUDE += docs/site` (P0).

Build via `make docs-site` (release) / `make docs-site-gate` (the site's own -W +
linkcheck + built-HTML-link net). Requires the pinned toolchain in
docs/requirements.txt, installed into an isolated venv (never the system interpreter).
"""

import re
import sys
import textwrap
from pathlib import Path

import yaml
from docutils import nodes
from pygments import highlight
from pygments.formatters import HtmlFormatter
from pygments.lexers import CppLexer, GoLexer, PythonLexer, RustLexer
from sphinx import addnodes
from sphinx.util.docutils import SphinxDirective

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
    "sphinx_copybutton",
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

copybutton_prompt_text = r"\$ "
copybutton_prompt_is_regexp = True

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
    # doc-site P1 reorg: the out-of-menu links (Performance/Coverage/Changelog)
    # live in the inner-page footer -- see _templates/footer-site-links.html.
    "footer_end": ["footer-site-links"],
    # No "logo_link" here: pydata_sphinx_theme's navbar-logo.html partial uses
    # `theme_logo_link` VERBATIM as the href (`{% set href = theme_logo_link %}`,
    # no `pathto()` call) -- a plain string here would be wrong on every page except
    # the ones sitting at the site root (site-relative "index.html" resolves to
    # e.g. "drop-in/index.html" from a nested page, a broken link). `_fix_logo_link`
    # below (the html-page-context hook) computes the correct page-depth-relative
    # path to the bespoke landing instead -- see its own docstring. (An earlier
    # revision set this to the literal string "index" on the mistaken assumption
    # that `pathto()` still ran on it; doc-site P2a's site-wide check_site_links.py
    # extension caught the resulting dead brand-link on every inner page and this
    # replaces that with the page-relative fix.)
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
# blind spot by parsing the BUILT build/site/html/*.html directly, site-wide (doc-site
# P2a) -- including this file's own `linkcheck_anchors = False` gap: it verifies each
# internal link's #fragment too (a literal `id="..."` search in the target file),
# which linkcheck never does regardless of anchors setting.
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


def _fix_logo_link(app, pagename, templatename, context, doctree):
    """`html-page-context` event hook: point the pydata navbar brand/logo at the
    bespoke landing ("index.html", the actual homepage -- root_doc is "contents",
    a near-empty placeholder, doc-site P1c) with a path that is correct from
    EVERY page's own directory, not just the ones at the site root.

    pydata_sphinx_theme's navbar-logo.html partial reads `theme_logo_link`
    (populated from `html_theme_options["logo_link"]`) and emits it as the href
    LITERALLY -- it never calls `pathto()` on it (unlike its own fallback branches
    for `root_doc` / `theme_logo.link`, which do). A single static config string
    therefore cannot be correct for both a depth-0 page (contents.html) and a
    depth-1 page (drop-in/std-regex-tour.html, reference/index.html): a value that
    resolves on one breaks on the other. Recomputing it here, per page, via the
    same `pathto(..., resource=1)` the theme's own asset links use (e.g.
    webpack-macros.html's `pathto('_static/styles/theme.css', 1)`), gets the
    right relative prefix ("index.html" at the root, "../index.html" one level
    down, ...) on every page. Overriding `context["theme_logo_link"]` here wins
    over the static `html_theme_options` value at render time, the same mechanism
    `_inject_quickstart` below already relies on for its own placeholders.
    """
    context["theme_logo_link"] = context["pathto"]("index.html", 1)


def _inject_primary_nav(app, pagename, templatename, context, doctree):
    """`html-page-context` event hook: inject the landing's primary nav from the
    ROOT TOCTREE -- the same source pydata_sphinx_theme walks for the inner
    header (doc-site P1 reorg: ONE nav source, two consumers). landing.html
    loops over `primary_nav`; its nav was hard-coded before this hook, which is
    exactly what let the two menus diverge for five wagons. The nav-equality
    net in tools/check_site_links.py asserts the two BUILT navs stay equal on
    every docs-site-gate run -- this hook is the source, the net is the proof.

    Entries: each direct entry of contents.md's toctree blocks, custom title
    first (env.titles fallback), href root-relative (the landing sits at the
    site root). Any exception here fails the -W build -- fail closed, never a
    silently empty nav.
    """
    if pagename != "index":
        return
    env = app.env
    items = []
    for toc in env.get_doctree(app.config.root_doc).findall(addnodes.toctree):
        for title, docname in toc["entries"]:
            if title is None:
                title = env.titles[docname].astext()
            items.append({"label": title, "href": docname + ".html"})
    if not items:
        raise RuntimeError("primary nav: no root-toctree entries found")
    context["primary_nav"] = items


# -- {features} directive (doc-site P2b, 3rd wagon: features.yaml -> render) -------
#
# Reads docs/site/data/features.yaml (schema documented in that file's own header --
# read it first) and renders one categorized table per category, one row per
# construct, with a Cyanotype-tinted status badge (`.feature-status.<slug>`, styled
# in real.css) and -- when a row names a `link:` -- a Sphinx cross-reference resolved
# by TARGET NAME (never a raw `<a href>` fragment; see features.yaml's own header for
# why a raw href into differences-from-re.md's div_* anchors would 404). PyYAML is not
# separately pinned in docs/requirements.txt: it is already a hard transitive
# dependency of myst-parser (its own front-matter parser), confirmed present in the
# pinned toolchain's resolved venv, the same way docutils/jinja2/pygments are never
# separately pinned either.
#
# data/features.yaml is plain data, not a Sphinx source document: never `{include}`d
# and never listed in a toctree, so it cannot become an orphan-doc warning under -W,
# and it is already outside Doxygen's scan (Doxyfile's `EXCLUDE += docs/site` covers
# the whole tree, data/ included). If a future Sphinx version ever starts treating a
# stray non-source file under a scanned directory as noteworthy, add "data/*.yaml" to
# exclude_patterns above -- not needed as of sphinx==9.1.0 (verified empirically).

_FEATURES_YAML = _HERE / "data" / "features.yaml"

# The status-slug -> human-label vocabulary is canonical in tools/gen_features.py
# (STATUS_LABELS) -- doc-site P3b moved it there so this directive and
# generate_scorecard() (COMPATIBILITY.md's GENERATED scorecard table) share exactly ONE
# table, never two. tools/gen_features.py depends on nothing but yaml/stdlib (it also
# runs under the system Python in ci.yml's preflight and docs-site-gate's
# check-features-probe step, never this venv), so importing FROM it here is safe; the
# reverse (gen_features.py importing conf.py) is not even possible -- conf.py needs
# sphinx/breathe/docutils, all venv-only. sys.path is extended for this import only.
sys.path.insert(0, str(_ROOT / "tools"))
from gen_features import STATUS_LABELS as _FEATURE_STATUS_LABELS  # noqa: E402


def _feature_inline_nodes(text):
    """Split TEXT on Markdown-style single-backtick spans into docutils Text/literal
    nodes. features.yaml's construct/note strings use the same single-backtick-for-
    code convention as COMPATIBILITY.md's own prose (Markdown, not reST) -- a hand-
    rolled split, rather than `state.inline_text` (which would apply reST's title-
    reference backtick rule instead), is what actually renders a `` `\\p{...}` ``-
    style span as inline code here.
    """
    result = []
    for i, part in enumerate(text.split("`")):
        if not part:
            continue
        if i % 2 == 1:
            result.append(nodes.literal(part, part))
        else:
            result.append(nodes.Text(part))
    return result


def _feature_ref_node(target, text="details"):
    """Build the same `pending_xref` shape Sphinx's own `:ref:` / MyST `{ref}` role
    emits (`refdomain="std"`, `reftype="ref"`, `reftarget=<label>`) so a features.yaml
    `link:`'s "#target" half becomes a real, nitpicky-checked cross-reference resolved
    by MyST TARGET NAME -- the fiche's "cross-ref Sphinx par nom de cible, jamais un
    <a href> brut" requirement. This is the mechanism that sidesteps the div_* anchor
    trap: the MyST target `div_property` builds to the HTML id `div-property`
    (docutils' `nodes.make_id`, underscore -> hyphen unconditionally), so a raw
    `#div_property` href would be a dead fragment; resolving by name lets Sphinx's own
    resolver find the right (hyphenated) id at render time, and lets nitpicky (`-W`)
    catch a typo'd/renamed target as a build failure instead of a silent 404.
    """
    refnode = addnodes.pending_xref(
        "", refdomain="std", reftype="ref", reftarget=target, refexplicit=True, refwarn=True
    )
    refnode += nodes.inline(text, text, classes=["xref", "std", "std-ref"])
    return refnode


class FeaturesDirective(SphinxDirective):
    """`{features}` -- renders docs/site/data/features.yaml as the Features-matrix
    page (docs/site/features.md). See that YAML file's own header for the schema and
    the doc-site P2b fiche for scope: data + render only here -- the phase-2 CI probe
    that will consume each row's `pattern:` is a follow-up wagon, not built by this
    directive.
    """

    has_content = False
    required_arguments = 0
    optional_arguments = 0

    def run(self):
        # note_dependency: features.yaml is read as plain data, never parsed as a
        # Sphinx source document, so Sphinx's own incremental-build change detection
        # (mtime/content hash of *source documents*) has no way to know this page
        # depends on it. Without this call, editing only features.yaml (not
        # features.md, not conf.py) leaves an incremental `sphinx-build` believing
        # features.html is still up to date -- a stale-page bug, not just a slow-
        # rebuild inconvenience (confirmed empirically: a second incremental build
        # after a features.yaml-only edit re-wrote 0 pages until this call was added).
        self.env.note_dependency(_FEATURES_YAML)

        if not _FEATURES_YAML.is_file():
            raise self.error(f"{{features}}: {_FEATURES_YAML} not found")
        with _FEATURES_YAML.open(encoding="utf-8") as fh:
            data = yaml.safe_load(fh)

        categories = data.get("categories") if isinstance(data, dict) else None
        if not categories:
            raise self.error("{features}: features.yaml has no 'categories'")

        output = []
        for category in categories:
            name = category["name"]

            heading = nodes.rubric(name, name, classes=["feature-category__title"])
            output.append(heading)

            table = nodes.table(classes=["feature-table"])
            tgroup = nodes.tgroup(cols=3)
            table += tgroup
            for colwidth in (32, 14, 54):
                tgroup += nodes.colspec(colwidth=colwidth)

            thead = nodes.thead()
            tgroup += thead
            header_row = nodes.row()
            for label in ("Construct", "Status", "Notes"):
                entry = nodes.entry()
                entry += nodes.paragraph(text=label)
                header_row += entry
            thead += header_row

            tbody = nodes.tbody()
            tgroup += tbody

            for feature in category["features"]:
                construct = feature["construct"]
                status = feature["status"]
                if status not in _FEATURE_STATUS_LABELS:
                    raise self.error(
                        f"{{features}}: {name!r} / {construct!r} has status "
                        f"{status!r}, not one of {sorted(_FEATURE_STATUS_LABELS)}"
                    )
                note = feature.get("note", "")
                link = feature.get("link")
                pattern = feature.get("pattern")
                # feature.get("since") is deliberately NOT read here (doc-site P3b): the
                # site stays laconic (no per-row version column) and the version history
                # lives in CHANGELOG.md; GitHub's COMPATIBILITY.md scorecard is the one
                # render that keeps "Since / target" (tools/gen_features.py's
                # generate_scorecard()). One source (features.yaml), two audience-tuned
                # renders -- this directive simply tolerates the extra key.

                row = nodes.row(classes=["feature-row", f"feature-row--{status}"])

                construct_entry = nodes.entry()
                construct_p = nodes.paragraph()
                construct_p += _feature_inline_nodes(construct)
                construct_entry += construct_p
                row += construct_entry

                status_entry = nodes.entry()
                status_p = nodes.paragraph()
                status_p += nodes.inline(
                    _FEATURE_STATUS_LABELS[status],
                    _FEATURE_STATUS_LABELS[status],
                    classes=["feature-status", status],
                )
                status_entry += status_p
                row += status_entry

                notes_entry = nodes.entry()
                notes_p = nodes.paragraph()
                notes_p += _feature_inline_nodes(note)
                if pattern:
                    notes_p += nodes.Text(" — ")
                    notes_p += nodes.literal(pattern, pattern, classes=["feature-pattern"])
                if link:
                    _, _, target = link.partition("#")
                    if not target:
                        raise self.error(
                            f"{{features}}: {name!r} / {construct!r} link {link!r} "
                            "has no '#target' (only PAGE#target cross-refs are "
                            "supported by this directive)"
                        )
                    notes_p += nodes.Text(" (")
                    notes_p += _feature_ref_node(target)
                    notes_p += nodes.Text(")")
                notes_entry += notes_p
                row += notes_entry

                tbody += row

            output.append(table)

        return output


def setup(app):
    app.add_directive("features", FeaturesDirective)
    app.connect("html-page-context", _fix_logo_link)
    app.connect("html-page-context", _inject_quickstart)
    app.connect("html-page-context", _inject_primary_nav)
