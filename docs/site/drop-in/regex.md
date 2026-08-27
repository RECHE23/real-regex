<!--
The Drop-in target page for the Rust regex crate, on the shared per-target
template. Canon = bindings/rust/README.md (the docs.rs landing page) -- linked,
not copied; only the key divergences are condensed here.
-->

# Drop-in for the Rust `regex` crate

**Full drop-in, strict by default** — the `regex` crate's API on REAL's linear
engine; a pattern REAL cannot run raises `Error::Unsupported` instead of
silently backtracking.

## Adopt / swap

```rust
use real_regex::Regex;   // drop-in for the regex crate

let re = Regex::new(r"(\w+)@(\w+)")?;
let caps = re.captures("info@example.com").unwrap();
&caps[2];                // "example"
```

## API offered

`Regex` (`is_match` / `find` / `find_iter` / `captures` / `captures_iter` /
`replace` / `replace_all` / `split`, `Display` / `FromStr`), `Captures`,
`RegexBuilder`, `RegexSet` (which-matched), and the `bytes` module — the
`regex` crate surface, same signatures. Not offered: `Captures::expand`
(use `replace` / `replace_all`). The opt-in **`fallback` cargo feature** adds
`RegexBuilder::fallback(true)`: a rejected pattern delegates to the `regex`
crate for that pattern, trading its linear-time guarantee — `engine()` always
says which backend ran (`Real` / `Fallback`).

## Differences & limitations

Condensed — the full list with tables is the crate's own
[README / docs.rs page](https://docs.rs/real-regex):

- **Bounded lookarounds, a positive divergence** — `(?=…)` `(?<=a|bb)` match in
  linear time; the `regex` crate and RE2 reject lookarounds entirely.
- **`\p{…}` natively** — General_Category, Script, Script_Extensions and the
  standard binary properties run on REAL (`engine()` = `Real`); other UAX44
  namespaces raise `Error::Unsupported`. `fallback` delegates only what the
  `regex` crate actually has — `\p{Bidi_Class=L}` is refused on both sides.
- **CPython word/space/case semantics** — `\w` `\s` and `IGNORECASE` folding
  follow Python `re`, not UTS#18; class-set syntax (`[a[b]]`, `&&`) declines.
- **`\u{…}` is accepted** as `\x{…}` (the regex crate spelling). `\U{…}` is
  still eight-digit `\U`, not braces.
- **Possessive quantifiers, a positive divergence — but silent** — REAL reads
  `x?+` / `x*+` / `x++` as possessive (the Python 3.11+ grammar); the `regex`
  crate reads them as nested repetition. Both compile — check the README entry
  before migrating.
- **`shortest_match` is leftmost-first** (greedy end), not earliest completion.
- Empty-match iteration follows the `regex` crate's rule at the wrapper, so
  `find_iter` / `split` agree with it.

## Comparison

REAL's closest peer on the linear-time axis. The honest duel, both ISAs, lives
in the
[performance ledger](https://github.com/RECHE23/real-regex/blob/main/docs/BENCHMARKS.md);
the reading is {doc}`Performance <../performance/index>`.
