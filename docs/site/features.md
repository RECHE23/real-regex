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
-->

# Features

The status of every construct REAL accepts, rejects, or extends beyond Python `re` --
one row per construct, one status per row. **Excluded by design is a closed door, not
a missing feature**: backreferences, recursion, and callouts each make matching
super-linear and would reopen the ReDoS door this engine exists to close, so they raise
a clear error rather than sitting on a roadmap (see the rationale on
[the divergences page](differences-from-re.md)).

For *how* to write a pattern, see the drop-in tour and the divergences page; for a
comparison against other regex engines (std::regex, RE2, the Rust `regex` crate, …),
see the project README.

```{features}
```
