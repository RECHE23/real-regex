<!--
bindings/index.md -- doc-site P2a·Bindings: single-sourced from bindings/README.md
via MyST {include} (not a copy), so the original stays canon and the site can never
drift from it -- see the doc-site P2a fiche. bindings/README.md is plain Markdown,
0 `@ref`/Doxygen markup, 0 relative-internal link (every link is external http or
absent), so it includes clean under nitpicky `-W`.

This page is deliberately a bare {include}, no H1 of its own: bindings/README.md
already carries exactly one H1 ("# Language bindings"), so adding one here would
produce a double-titled document. The nav label ("Bindings") comes from the
toctree's own custom title (`Bindings <bindings/index>` in contents.md), not from
this page's title -- see that file's own comment.
-->

```{include} ../../../bindings/README.md
```
