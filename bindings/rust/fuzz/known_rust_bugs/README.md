# Known upstream `regex`-crate bugs

Repros the REAL differential fuzzer (`bindings/rust/fuzz/`) found where the **`regex` crate is wrong** — it
disagrees with Python `re` (the neutral oracle) and with REAL, which both agree. These are skipped in
`differential.rs` (they are not REAL divergences) and recorded here.

## 1. Literal-prefilter leftmost-first violation (`regex` 1.12.x)

**Repro (4-byte ASCII):**

| pattern | haystack | Python `re` | REAL | `regex` 1.12.4 |
| --- | --- | --- | --- | --- |
| `A\|.AA` | `"\n#AA"` | `[(1,4)]` | `[(1,4)]` | `[(2,3),(3,4)]` ✗ |
| `a\|.aa` | `"-#aa"`  | `[(1,4)]` | `[(1,4)]` | `[(2,3),(3,4)]` ✗ |

**Anatomy.** The leftmost match `[1,4)` begins with `.` (a non-literal) matching `#` at position 1, then
`AA`. The `regex` crate extracts a literal prefilter from the pattern and, on resume, jumps ahead to a literal
occurrence — skipping the start of a leftmost match that begins at the non-literal alternation branch. Its own
leftmost-first semantics require `[1,4)`; the prefilter resume violates them. (A match anchored at 0 is fine —
it is the prefilter *resume* that misses the leftmost match.)

**Why it matters here.** This is precisely the failure class our IL.2 inner-literal loop must avoid — a
literal-prefilter that decides where the leftmost match starts. We read `regex`'s `ReverseInner` loop before
writing ours *because* this exact trap is easy to fall into; the upstream crate fell into a sibling of it.
Our loop keeps leftmost correct by bounding the reverse at the previous literal's end and confirming forward,
gated by a routed==core differential.
