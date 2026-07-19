<!--
performance/index.md -- doc-site P2a·Performance: single-sourced from
docs/BENCHMARKS.md via MyST {include} (not a copy), so the original -- including its
Version-stamp row, the one `make version-check`'s WARN check reads directly off
docs/BENCHMARKS.md -- stays canon and is never duplicated/drifted. See the doc-site
P2a fiche. docs/BENCHMARKS.md is plain Markdown, 0 `@ref`/Doxygen markup, 0
relative-internal link (every link is external http or absent), so it includes clean
under nitpicky `-W`.

This page is deliberately a bare {include}, no H1 of its own: docs/BENCHMARKS.md
already carries exactly one H1 ("# REAL — performance baseline"), so adding one here
would produce a double-titled document. The nav label ("Performance") comes from the
toctree's own custom title (`Performance <performance/index>` in contents.md), not
from this page's title -- see that file's own comment.

Visibly non-polished by design (P2a, not P2b): the Version row's Conditions table
carries a mega-cell changelog (every release note ever stamped into that one cell)
that renders here as an oversized table cell -- in-site non-polished beats a GitHub
redirect, and the polish pass (extracting the changelog into CHANGELOG, protecting
the version-check stamp during that extraction) is explicitly deferred to P2b.
-->

```{include} ../../BENCHMARKS.md
```
