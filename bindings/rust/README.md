# real-regex

Linear-time, ReDoS-safe regular expressions with **bounded lookarounds** — Rust bindings to the
[REAL](https://github.com/RECHE23/real-regex) C++ engine.

Every pattern that compiles matches in time linear in the input. There is no backtracking, so no
catastrophic blow-up: the pathological `(a+)+b` that hangs a backtracking engine runs in microseconds here.
The engine is **strict by design** — a construct it cannot run linearly (a backreference, an unbounded
lookaround) is rejected at compile time, never silently made non-linear.

```rust
use real_regex::Regex;

let re = Regex::new(r"(\w+)@(\w+)").unwrap();
for caps in re.captures_iter("a@b cd@ef") {
    println!("{:?} / {:?}", &caps[1], &caps[2]);
}
```

Unlike RE2 and the `regex` crate, REAL supports bounded lookahead and lookbehind — in linear time. The
engine and this crate share one calendar version.

## The API

The crate mirrors the [`regex`](https://docs.rs/regex) crate: `Regex` with `find` / `find_iter` / `captures`
/ `captures_iter` / `is_match` / `replace` / `replace_all` / `replacen` (with `$`-templates, `NoExpand`, and
closures) / `split` / `splitn`; `Match` (spans) and indexable `Captures` (`caps[0]`, `caps["name"]`);
`RegexBuilder` (case-insensitive, multi-line, `unicode(false)`, …); and a `bytes` module over `&[u8]`. Every
method is verified against the `regex` crate by a differential test suite.

## Divergences from the `regex` crate

A drop-in mirrors semantics, not just signatures. The known differences:

- **Empty-match iteration — resolved.** REAL's engine follows Python `re`'s rule (3.7+: an empty match
  adjacent to a previous non-empty match is kept), but the crate adapts iteration to rust's rule at the
  wrapper — an empty match whose end equals the previous match's end is skipped, exactly as
  `regex-automata`'s `util::iter::Searcher::try_advance` does. So `find_iter` / `split` / `replace_all` match
  the `regex` crate.
- **`shortest_match` — residual.** REAL is leftmost-**first** (like `regex`), but this returns the leftmost
  match's *greedy* end, whereas `regex` returns the earliest position at which a match completes (`a+` on
  `"aaa"`: REAL `3`, `regex` `1`). A true earliest-completion mode (a `first-accept` stop in the forward
  pass) is a planned engine follow-up; until then, use `shortest_match` as an `is_match` that also reports
  where the leftmost match ends.
- **Unicode property classes `\p{…}` — not yet.** REAL rejects them with `Error::Unsupported`. Enable the
  `fallback` feature and `RegexBuilder::new(pat).fallback(true)` to delegate such a pattern to the `regex`
  crate (per pattern, forfeiting the linear-time guarantee — `engine()` reports it). Full `\p{}` support in
  the linear engine is planned.
- **`RegexSet` — not offered.** Multi-pattern set matching is not part of this version.
- **Bounded lookarounds — a *positive* divergence.** REAL supports bounded lookahead `(?=…)` / `(?!…)` and
  lookbehind `(?<=…)` / `(?<!…)` in linear time. The `regex` crate and RE2 support neither. This is a
  documented superset, not a gap.

## The fallback feature

```toml
real-regex = { version = "…", features = ["fallback"] }
```
Off by default (the crate stays strict and pulls no extra dependency). On, a pattern REAL cannot run
linearly can be delegated per pattern with `RegexBuilder::new(pat).fallback(true)`; `Regex::engine()` returns
`Engine::Fallback` for it. `Regex::new` is always strict.

## License

MIT.
