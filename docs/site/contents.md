---
title: "real::regex documentation"
---

<!--
contents.md is the Sphinx root document (root_doc = "contents", doc-site P1c) --
NOT the landing. The bespoke root lives at docs/site/_templates/landing.html,
served at "index.html" via html_additional_pages (conf.py); Sphinx cannot render
a source document AND an additional page to the same output name.

THE TOCTREE BELOW IS THE SITE'S SINGLE NAV SOURCE (doc-site P1 reorg, council
synthesis docsite-reorg-purge-synthesis.md). Three direct entries = the three
header items -- pydata_sphinx_theme walks only the root document's own toc
(toctree.py's `_generate_nav_info`), so every direct entry here becomes a flat
header link, and ONLY these three may be direct:

  Features       -> features.md         (nests differences-from-re)
  Drop-in        -> drop-in/index.md    (nests std-regex-tour [-> std-regex-reference], bindings/index)
  API reference  -> reference/index.rst (Breathe entry page; /api Doxygen = bonus)

The landing's nav is INJECTED from this same toctree (conf.py's
_inject_primary_nav, html-page-context hook) -- never hard-coded -- and
tools/check_site_links.py asserts built-landing-nav == built-inner-header on
every docs-site-gate run (the nav-equality net: the two menus diverged for five
wagons precisely because no gate watched them).

Out of the menu by design: performance/index.md is `:orphan:` (footer link only
-- comparative benchmarks have no place in the primary nav, decision Rene).
Adding it back as a direct entry here would resurface a 4th header item. A
future Syntax page nests under Features when it lands (P5).
-->

# real::regex documentation

This is the Sphinx root document, not the homepage.

```{raw} html
<p>The bespoke landing lives at <a href="index.html">the homepage</a>.</p>
```

<!--
Raw HTML, not a MyST link: MyST resolves `[text](index.html)` as an internal doc
xref and fails under `-W` ("index" is an html_additional_pages target, not a
source document) -- confirmed empirically (myst.xref_missing).
-->

```{toctree}
:hidden:
:maxdepth: 1
:caption: Features

features
```

```{toctree}
:hidden:
:maxdepth: 1
:caption: Drop-in

Drop-in <drop-in/index>
```

```{toctree}
:hidden:
:maxdepth: 1
:caption: API reference

API reference <reference/index>
```
