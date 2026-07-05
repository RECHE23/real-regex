# Known upstream `regex`-crate bugs

Repros the REAL differential fuzzer (`bindings/rust/fuzz/`) found where the **`regex` crate is wrong** — it
disagrees with Python `re` (the neutral oracle) and with REAL, which both agree. These are skipped in
`differential.rs` (they are not REAL divergences) and recorded here.

## 1. Reverse-suffix leftmost-first violation (`regex` 1.12.x) — [rust-lang/regex#1373](https://github.com/rust-lang/regex/pull/1373)

**Repro (4-byte ASCII):**

| pattern | haystack | Python `re` | REAL | `regex` 1.12.4 |
| --- | --- | --- | --- | --- |
| `A\|.AA` | `"\n#AA"` | `[(1,4)]` | `[(1,4)]` | `[(2,3),(3,4)]` ✗ |
| `a\|.aa` | `"-#aa"`  | `[(1,4)]` | `[(1,4)]` | `[(2,3),(3,4)]` ✗ |

**Anatomy.** The leftmost match `[1,4)` begins with `.` (a non-literal) matching `#` at position 1, then
`AA`. Root cause (per the upstream fix): the reverse-suffix optimization orders candidates by their END, but
when the exact literal of one alternation branch is a proper suffix of another branch's match, ordering by end
breaks the leftmost-by-START rule. Fixed in [#1373](https://github.com/rust-lang/regex/pull/1373) (a
conservative decline in `ReverseSuffix::new`), found by REAL's differential fuzzer. Its own
leftmost-first semantics require `[1,4)`; the prefilter resume violates them. (A match anchored at 0 is fine —
it is the prefilter *resume* that misses the leftmost match.)

**Why it matters here.** This is precisely the failure class our IL.2 inner-literal loop must avoid — a
literal-prefilter that decides where the leftmost match starts. We read `regex`'s `ReverseInner` loop before
writing ours *because* this exact trap is easy to fall into; the upstream crate fell into a sibling of it.
Our loop keeps leftmost correct by bounding the reverse at the previous literal's end and confirming forward,
gated by a routed==core differential.

## 2. Literal-branch prefilter resume (`regex` 1.x) — the same class, a literal branch

Bug 1's skip (`|.` / `|[`) matched only when the branch after `|` began with `.` or `[`. The fuzzer then found
the same leftmost violation with a branch that begins with a **literal byte**, which that substring check let
through:

| pattern | haystack | Python `re` | REAL | `regex` |
| --- | --- | --- | --- | --- |
| `\0*\0\|\u{8}\u{c}\0\0` | `"\0\0\0\|~\u{8}\u{c}\0\0\0"` | `[(0,3),(5,9),(9,10)]` | `[(0,3),(5,9),(9,10)]` | `[(0,3),(7,10)]` ✗ |

**Anatomy.** After the match `[0,3)`, the leftmost match from position 3 is the second branch `\u{8}\u{c}\0\0`
at `[5,9)`. The `regex` crate's prefilter, built from the first branch's `\0`, resumes at the `\0` run starting
at 7 and reports `[7,10)` — skipping `[5,9)` entirely. Same root as bug 1 (a literal prefilter deciding the
leftmost start over a top-level alternation), just triggered by a literal-starting branch. The skip is now
`has_top_level_alternation(pattern)` — the top-level `|` is the trigger, independent of the branch's first
token. REAL and Python `re` agree on `[(0,3),(5,9),(9,10)]`.
