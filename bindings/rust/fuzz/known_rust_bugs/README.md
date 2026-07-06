# Known upstream `regex`-crate bugs

Repros the REAL differential fuzzer (`bindings/rust/fuzz/`) found where the **`regex` crate is wrong** — it
disagrees with Python `re` (the neutral oracle) and with REAL, which both agree. They are not REAL divergences;
recorded here.

**Detection is now SEMANTIC, not by pattern shape.** The leftmost class below was chased through four
manifestations by form predicates (top-level `|`, empty-branch `(|`, …) — a losing whack-a-mole, since the real
class is "any alternation where the crate's meta-engine picks its reverse-suffix search". `differential.rs`
instead catches it at the `find_iter` comparison: on a span mismatch, if a span REAL found (that the crate's list
lacks) is confirmed by the crate ITSELF when anchored (`^(?:pat)`), the crate's unanchored search dropped a match
it agrees exists — logged (`UPSTREAM-#1345 skip`; its count is the wake signal) and skipped. This covers every
past and future manifestation and cannot swallow a real REAL bug (a REAL span the anchored crate does not confirm
still panics). The old form predicates are retired, so alternation is fully differentiated again.

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

## 3. Empty-branch alternation, inside a group — the same #1373 class, a third door

The differential fuzzer found the same leftmost-first violation a third time, now through an alternation with an
**empty branch inside a group** — the minimized pattern is `.(|\x02;)().\0` (the group `(|\x02;)` opens with an
empty branch). REAL agrees with Python `re`; the `regex` crate does not (the devbox arbiter confirmed re == REAL
exactly, so the leftmost match at the earlier start must win). The upstream issue
[rust-lang/regex#1373](https://github.com/rust-lang/regex/pull/1373) documents only the top-level form; this
extends it to an empty branch anywhere.

The `has_top_level_alternation` skip (§1/§2) only catches a top-level `|`, so the differential now also skips
`has_empty_alternation_branch` — a `|` with an empty branch on either side (`(|`, `|)`, `||`, or at the pattern
boundary), wherever it sits. Non-empty in-group alternations (`(a|b)`) stay differentiated; the coverage loss is
bounded to empty-branch alternations. Reproducer pinned as `corpus/differential/rd3_empty_branch_alternation_in_group`.
