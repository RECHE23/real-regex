---
orphan: true
---

<!--
performance/index.md -- doc-site P2a·Performance: single-sourced from
docs/BENCHMARKS.md via MyST {include} (not a copy), so the original -- including its
Version-stamp row, the one `make version-check`'s WARN check reads directly off
docs/BENCHMARKS.md -- stays canon and is never duplicated/drifted.
docs/BENCHMARKS.md is plain Markdown, 0 `@ref`/Doxygen markup, 0 relative-internal
link, so it includes clean under nitpicky `-W`.

Bare {include}, no H1 of its own: docs/BENCHMARKS.md already carries exactly one H1.

`orphan: true` (doc-site P1 reorg): this page left the primary nav -- comparative
benchmarks have no place in the menu (decision Rene, council synthesis) -- and
belongs to NO toctree, so the frontmatter keeps `-W` green. It stays reachable
from the footers (landing + inner pages). Do NOT re-add it as a direct entry of
contents.md: any direct root-toctree entry becomes a pydata header item.
-->

```{include} ../../BENCHMARKS.md
```
