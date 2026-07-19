<!--
features.md -- doc-site P2b (3rd wagon): the Features matrix, the "executable claims"
pivot named in the doc-site synthesis. This page is deliberately thin: the real content
is the {features} directive below, which renders docs/site/data/features.yaml (see that
file's own header for the schema, and docs/site/conf.py's FeaturesDirective for the
render mechanism). Phase 1 (this wagon) is data + render only; a phase-2 wagon adds the
CI probe that consumes each row's `pattern:` -- not built here.

Categories/rows are curated from docs/COMPATIBILITY.md's "## Feature scorecard"
(table l.45-58) plus its "## What runs on real" baseline (l.85-93, the Core category)
and its excluded-by-design rows (the moat, l.41-48). COMPATIBILITY.md remains the
curated, per-construct rationale document and is untouched by this wagon -- this page
duplicates its status data on purpose, for now (see features.yaml's own header, "De-dup
DIFFERE"). Multi-engine comparison columns (REAL vs std/RE2/...) stay in README, and
how-to-write regex syntax stays out of scope for a future Syntax page -- this page is
status only.

doc-site P1 reorg: this page is the Features SECTION landing -- differences-from-re
nests in the toctree below (Features owns the "what does it support / how does it
differ" question, council synthesis Q1), and a future Syntax page will nest here too.
-->

# Features

Every construct REAL accepts, rejects, or extends beyond Python `re` — one row,
one status. **Excluded by design is a closed door, not a missing feature**: each
of those constructs makes matching super-linear and would reopen the ReDoS door
this engine exists to close — rationale on
[the divergences page](differences-from-re.md).

```{features}
```

```{toctree}
:hidden:
:maxdepth: 1

Differences from re <differences-from-re>
```
