"""Sphinx configuration for docs/site: the bespoke landing + the pydata inner
pages. Does not affect `make doc` (Doxyfile excludes docs/site).

Build via `make docs-site` / `make docs-site-gate`. Requires the pinned toolchain
in docs/requirements.txt, installed into an isolated venv (never the system
interpreter).
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

# The bespoke landing owns "index.html" via html_additional_pages -- Sphinx
# cannot render a source document and an additional page to the same output
# name, so the root document lives at contents.md.
root_doc = "contents"
source_suffix = {
    ".rst": "restructuredtext",
    ".md": "markdown",
}

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

# Four justified exceptions: real::basic_regex's Doxygen comment \ref-links four
# symbols reference/index.rst deliberately does not render (their instantiated
# signatures would pull in fixed_string/flags/detail transitively). The two
# struct refids are name-derived and stable; the namespace-MEMBER refids carry a
# Doxygen-version-dependent hash (measured: 1.17 vs 1.9.8 emit different hashes),
# hence the anchor-pattern regex instead of pinned values.
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
    "footer_end": ["footer-site-links"],
    # Per-page dict: "**" must restate theme.conf's stock default verbatim or
    # every other page silently loses its right-hand rail; an empty list drops
    # that page's secondary-sidebar <div> from the markup. The left sidebar is
    # html_sidebars below -- two config surfaces, not one.
    "secondary_sidebar_items": {
        "**": ["page-toc", "edit-this-page", "sourcelink"],
        "features": [],
    },
    # No "logo_link": the theme emits it VERBATIM (no pathto()), so a static
    # string breaks on every page not at the site root. _fix_logo_link below
    # computes it per page instead.
}

# An empty per-page list takes the left column out of the flow on that page
# only (layout.html's hide-on-wide); unnamed pages keep the theme default.
html_sidebars = {"features": []}

# The bespoke landing: a self-contained template, not a source document --
# invisible to the doctree/toctree machinery. The ".html" suffix is required:
# the template loader does no implicit extension lookup.
html_additional_pages = {"index": "landing.html"}

# linkcheck walks SOURCE documents only: the landing template and every
# #fragment are invisible to it. tools/check_site_links.py (docs-site-gate)
# closes both gaps by parsing the built HTML directly.
linkcheck_anchors = False

# -- quickstart injection ------------------------------------------------------
#
# landing.html holds only {{ quickstart_* }} placeholders. This hook reads the
# four CI-tested snippet files through their own region markers, so the landing
# always shows code the per-language CI jobs just compiled and ran -- never a
# hand copy that can drift. Pygments emits short token classes (.k/.s/.c/...)
# styled by the landing's own <style> through the site's custom properties, so
# code re-tints with the theme toggle.
_PYGMENTS_FORMATTER = HtmlFormatter(nowrap=True)


def _extract_region(text: str, start_marker: str, end_marker: str) -> str:
    """Text strictly between a start-after marker line and an end-before marker
    line ({literalinclude}'s contract): each snippet file's own region markers
    stay the single source of truth for what the landing displays. `str.index`
    is first-occurrence, so the marker pair must never appear twice in-file.
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
    # Go requires imports ahead of all other top-level declarations, so the
    # import and the body aren't contiguous: two marker pairs instead of one.
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
    """Point the navbar brand at the landing with a per-page-depth path: the
    theme emits `theme_logo_link` literally (no pathto()), so one static string
    cannot be correct at both depth 0 and depth 1. Overriding the context value
    here wins over html_theme_options at render time.
    """
    context["theme_logo_link"] = context["pathto"]("index.html", 1)


def _inject_primary_nav(app, pagename, templatename, context, doctree):
    """Inject the landing's nav from the root toctree -- the same source the
    theme walks for the inner header, so the two menus cannot diverge
    (tools/check_site_links.py asserts their built equality on every gate run).
    Custom toctree titles win, env.titles is the fallback; hrefs are
    root-relative (the landing sits at the site root). Any exception fails the
    -W build -- never a silently empty nav.
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


# -- {features} directive: renders data/features.yaml as the status matrix ------
#
# Schema in the YAML's own header. PyYAML is a hard transitive dependency of
# myst-parser, hence not separately pinned -- same as docutils/jinja2/pygments.

_FEATURES_YAML = _HERE / "data" / "features.yaml"

# STATUS_LABELS is canonical in tools/gen_features.py so this directive and the
# generated COMPATIBILITY scorecard share ONE vocabulary table. Importing FROM
# the tool is safe (yaml/stdlib only); the reverse is impossible -- conf.py
# needs venv-only sphinx, and the tool runs under the system Python in the gates.
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
    """The same `pending_xref` shape `:ref:`/{ref} emits, so a `link:` target
    becomes a nitpicky-checked cross-reference resolved by NAME. Never a raw
    href: the MyST target `div_property` builds to the id `div-property`
    (make_id turns underscores into hyphens), so a literal `#div_property`
    fragment would be dead; name resolution finds the real id and `-W` catches
    a typo'd target at build time.
    """
    refnode = addnodes.pending_xref(
        "", refdomain="std", reftype="ref", reftarget=target, refexplicit=True, refwarn=True
    )
    refnode += nodes.inline(text, text, classes=["xref", "std", "std-ref"])
    return refnode


class FeaturesDirective(SphinxDirective):
    """`{features}`: renders data/features.yaml as the categorized status
    matrix. Schema in the YAML's own header.
    """

    has_content = False
    required_arguments = 0
    optional_arguments = 0

    def run(self):
        # The yaml is data, not a source document: without this, an incremental
        # build after a yaml-only edit serves a stale page (measured).
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
                # "since" is deliberately not rendered here: version history is
                # CHANGELOG.md's job; only the generated COMPATIBILITY scorecard
                # keeps a Since column.

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
