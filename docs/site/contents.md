---
title: "real::regex documentation"
---

<!--
contents.md is the Sphinx root document (root_doc = "contents", doc-site P1c) --
NOT the landing. The bespoke, pixel-faithful root lives at docs/site/_templates/
landing.html, served at "index.html" via html_additional_pages (conf.py); Sphinx
cannot render a source document AND an additional page to the same output name, so
the source root moved here to avoid that collision.

This page's only job is to be a valid root document and hold the hidden toctree
below, which keeps reference/index.rst (the Breathe API proof page) out of the
"document isn't included in any toctree" orphan warning `-W` would otherwise raise
-- the root document itself is exempt from that check by construction, so
contents.md needs no toctree entry pointing back at it.
-->

# real::regex documentation

This is the Sphinx root document, not the homepage.

```{raw} html
<p>The bespoke landing lives at <a href="index.html">the homepage</a>.</p>
```

<!--
The link above is raw HTML, not a MyST markdown link: MyST resolves a plain
`[text](index.html)` as an internal doc cross-reference and fails under `-W`
because "index" is not a source document (it is an html_additional_pages target,
see conf.py) -- confirmed empirically (myst.xref_missing).
-->

```{toctree}
:hidden:
:maxdepth: 1

reference/index
```
