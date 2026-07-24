---
orphan: true
---

<!--
{include} of docs/BENCHMARKS.md (canon -- copying it would duplicate the version
stamp `make version-check` reads there; that stamp lives in the FILE and is
unaffected by how this page slices it). `:start-after:` skips the source's own
H1 so this page's title promotes cleanly with a public summary above the full
ledger. `orphan: true`: footer-only page, in no toctree -- a direct
root-toctree entry would become a header item.
-->

# Performance

REAL is the linear-time, ReDoS-safe engine that is also fast: ahead of
`std::regex` everywhere and of RE2 on most general shapes, trading blows with
PCRE2-JIT and Rust's `regex` crate, and orders of magnitude ahead of every
backtracker on adversarial (ReDoS) inputs — safety it never trades for the
speed. The ledger below is the canonical, reproducible baseline: complete
methodology, per-engine duels, raw numbers, and the per-train impact notes,
with every ratio re-derived programmatically from the raw pairs.

```{include} ../../BENCHMARKS.md
:start-after: "# REAL — performance baseline"
```
