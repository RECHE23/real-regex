---
title: "real::regex documentation"
---

<!--
The Sphinx root document -- NOT the landing. The bespoke landing template renders
to "index.html" via html_additional_pages (conf.py); Sphinx cannot render a source
document and an additional page to the same output name, so the root lives here.

THE TOCTREE BELOW IS THE SITE'S SINGLE NAV SOURCE. pydata_sphinx_theme builds its
header from the root document's direct toctree entries only (one entry = one flat
header link), so exactly these four may be direct:

  Features       -> features.md          (nests differences-from-re)
  Drop-in        -> drop-in/index.md     (nests the five target pages)
  API reference  -> reference/index.rst  (Breathe entry; /api Doxygen = bonus)
  Developer      -> developer/index.md   (Doxygen /api, architecture, coverage,
                                          bench methodology, testing)

The landing's nav is injected from this same toctree (conf.py) and
tools/check_site_links.py asserts built-landing-nav == built-inner-header on every
gate run. performance/index.md is deliberately `:orphan:` and footer-only --
re-adding it as a direct entry would resurface a header item.
-->

# real::regex documentation

This is the Sphinx root document, not the homepage.

```{raw} html
<p>The bespoke landing lives at <a href="index.html">the homepage</a>.</p>
```

<!--
Raw HTML, not a MyST link: MyST resolves `[text](index.html)` as a doc xref and
fails under `-W` ("index" is an html_additional_pages target, not a document).
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

```{toctree}
:hidden:
:maxdepth: 1
:caption: Developer

Developer <developer/index>
```
