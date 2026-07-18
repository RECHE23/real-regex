"""Sphinx configuration for docs/site — the themed landing + Breathe-powered API
reference scaffold (doc-site P1). Additive: does not affect `make doc` (the live
Doxygen HTML site, docs.yml) — see Doxyfile's `EXCLUDE += docs/site` (P0).

Build via `make docs-site` (release) / `make docs-site-gate` (the site's own -W +
linkcheck net). Requires the pinned toolchain in docs-requirements.txt, installed
into an isolated venv (never the system interpreter).
"""

import re
from pathlib import Path

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

root_doc = "index"
source_suffix = {
    ".rst": "restructuredtext",
    ".md": "markdown",
}

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
# P2-P4 deferral). Without these four entries, nitpicky correctly fails the build
# on these dangling \ref targets; this is the "mark linkcheck_ignore explicit"
# pattern applied to the Sphinx xref net instead -- an explicit, reviewed
# exception, not a silently-skipped check. Refids are Doxygen-generated (stable
# for a given header + Doxygen version, the same coupling doc-check already pins).
nitpick_ignore = [
    ("ref", "namespacereal_1a1ea221a8ba47a0e1e973f4f02dade160"),  # real::regex
    ("ref", "namespacereal_1aec53faf599938c0c619a995ecb60c5f1"),  # real::static_regex
    ("ref", "structreal_1_1detail_1_1dynamic__storage"),
    ("ref", "structreal_1_1detail_1_1static__storage"),
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
    # Without an image logo, the navbar brand still needs somewhere real to point
    # (the theme defaults an unset logo_link to "#") -- back to the landing itself.
    "logo_link": "index",
}

# -- linkcheck -------------------------------------------------------------
# Every hyperlink on the landing resolves to a real, reachable destination or an
# internal doc reference -- no stub `#` placeholders survive from the mockup into
# this content (see docs/site/index.md). Nothing is pre-emptively ignored here;
# linkcheck_ignore is added only if a specific, justified exception is found.
linkcheck_anchors = False
