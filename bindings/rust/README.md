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
/ `captures_iter` / `capture_locations` + `captures_read` / `captures_read_iter` (reusable group slots) /
`is_match` / `replace` / `replace_all` / `replacen` (with `$`-templates, `NoExpand`, and closures) /
`split` / `splitn`; `Display` / `FromStr` (`format!("{re}")`, `"\\d+".parse()`); `Match` (spans) and
indexable `Captures` (`caps[0]`, `caps["name"]`); `RegexBuilder` (case-insensitive, multi-line,
`unicode(false)`, …); and a `bytes` module over `&[u8]`. Every method is verified against the `regex`
crate by a differential test suite.

## Divergences from the `regex` crate

A drop-in mirrors semantics, not just signatures. The known differences:

- **Empty-match iteration — resolved.** REAL's engine follows Python `re`'s rule (3.7+, which keeps empty
  matches and, after one, re-tries a non-empty match at the same spot); the crate instead **drives the search
  by position**, exactly as `regex-automata`'s `util::iter::Searcher` does — find the leftmost match from a
  position, advance to its end, and step one codepoint past an empty match adjacent to the previous end before
  re-searching. Driving (not filtering re's stream) is necessary because rust visits positions re never does
  (`(?:|ab)*` on "abab": rust yields empties at 1 and 3). To keep this free for the common case, the wrapper
  stays on the cheap engine iterator until the first empty match, then switches to driving — so patterns that
  never match empty pay nothing. `find_iter` / `split` / `replace_all` match the `regex` crate.
- **`$` anchor — resolved.** Python `re`'s `$` (no multiline) matches at the end **or** just before a final
  `\n`; rust's is end-only. The crate compiles every pattern with the engine's `dollar_endonly` flag, so `$`
  is end-only — `a$` on `"a\n"` finds nothing, like `regex`. `(?m)$` (line-relative) is identical in both.
- **`\w` and `\s` semantics — CPython, not UTS#18.** REAL defines `\w` (and `\b`, which inherits it) and `\s`
  the way Python `re` does — its contract — which is not the UTS#18 definition the `regex` crate uses. Each
  difference is **bidirectional** (measured over all 1,112,064 scalars):

  | | code points `regex` matches but REAL does not (UTS#18 ⊃) | code points REAL matches but `regex` does not (CPython ⊃) |
  | --- | --- | --- |
  | `\w` | marks `\p{M}` — Mn 2020, Mc 468, Me 13; Join_Control ZWNJ/ZWJ (2); connectors `\p{Pc}` beyond `_` (9); Other_Alphabetic symbols `\p{So}` (130) | numeric-other `\p{No}` — 915 (superscripts, subscripts, fractions: CPython's `str.isalnum`) |
  | `\s` | — | `U+001C`–`U+001F` (4, the file/group/record/unit separators — CPython's `str.isspace`) |

  `\d` (both `\p{Nd}`) is **identical**. The `\w` delta also flows through every word-boundary assertion —
  `\b`, `\B`, and the `\<` / `\>` word-start/end extensions all use the same word-set — so `\bété` or `\<foo`
  on text with a combining mark or a `\p{No}` will differ from the `regex` crate the same way. These `\w`/`\s`
  differences are intentional (REAL matches `re`, asserted by a REAL/`re` differential and the 3.2M-case
  exhaustive); for byte-for-byte `regex` parity, use the `fallback` feature. The counts are reproducible with
  the committed probe (`fuzz/unicode_probe/`), which re-dumps them on any Unicode bump; the differential fuzzer
  skips a `\w`/`\b`/`\<`/`\s`-family pattern whose text carries a delta code point (the delta set is computed
  from both engines, so it tracks the probe automatically), so the divergence does not read as a REAL bug.
- **Case-insensitive folding — CPython, not simple CaseFolding.** Under `(?i)`, REAL follows Python `re`'s
  equivalences (via `str.upper`/`lower`), so the Turkish **dotless/dotted I** fold with I/i: `(?i)I` matches
  ı (U+0131) and İ (U+0130), and `(?i)\p{Lu}` therefore matches ı — exactly as stdlib `re` does. The `regex`
  crate uses Unicode **simple CaseFolding**, which keeps ı apart. Two code points, one contract each — both
  correct. The differential fuzzer masks exactly this set (`ICASE_FOLD_DELTAS`, computed by asking both
  engines), the twin of the `\w`/`\s` mask; `fallback` gives byte-for-byte crate folding.
- **A malformed `{…}` — literal, not a quantifier.** A `{` that does not open a strict `{n}` / `{n,}` / `{n,m}`
  (ASCII digits only) is a **literal brace** in REAL, matching Python `re`. The `regex` crate is
  whitespace-tolerant, so `{ 2 }` / `{\n4\n}` are quantifiers to it — `${\n…}` becomes `$` repeated (an empty
  match) where re/REAL find nothing. A legal parser-interpretation difference, both correct for their contract;
  the differential fuzzer skips a non-strict-brace pattern by form.
- **Empty-alternation-branch loops — a three-way corner.** For an empty-first-branch repetition (`(|a)*`)
  under `find_iter`'s forced-non-empty step, REAL, the `regex` crate, and Python `re` each produce a
  *different* span sequence on `"aa"`: REAL consumes maximally (`(0,0)(0,2)(2,2)`), the crate goes all-empty
  (`(0,0)(1,1)(2,2)`) or drops the trailing empty (`(a|)*` → loses the final `(2,2)`), and `re` steps out
  through the empty branch (`(0,0)(0,1)(1,1)(1,2)(2,2)`). REAL's exact behaviour and why it is not "fixed"
  (a fix would rework the star-loop termination that underlies every quantifier) are pinned in the C++
  divergences page (`div_empty_first_branch_loop`); the differential fuzzer skips the class by form
  (`has_empty_alternation_branch`), since the crate is not a reliable oracle for it — Python `re` is. A
  single `find` / `captures` agrees. That is a statement about the ENGINE: through this crate's
  `find_iter` the sequences match, because the iterator switches to driving the search by position at
  the first empty match precisely to reproduce the crate's advancement rather than re's. A fixed set
  of these forms is therefore swept against the crate directly (`surface_differential.rs`), where it
  is the only thing that exercises that switch; the fuzzer stays conservative because it generates
  arbitrary patterns and a skip there costs only coverage.
- **`shortest_match` — residual.** REAL is leftmost-**first** (like `regex`), but this returns the leftmost
  match's *greedy* end, whereas `regex` returns the earliest position at which a match completes (`a+` on
  `"aaa"`: REAL `3`, `regex` `1`). A true earliest-completion mode (a `first-accept` stop in the forward
  pass) is a planned engine follow-up; until then, use `shortest_match` as an `is_match` that also reports
  where the leftmost match ends.
- **Unicode property classes `\p{…}` — General_Category, Script, Script_Extensions and the standard
  binary properties, natively.** `\p{L}`, `\p{Lu}`, `\p{Nd}`, the groups `\p{L}`..`\p{C}`, `\p{sc=Greek}`
  / `\p{Script=Latin}` / `\p{scx=Grek}` (short UAX24/ISO 15924 codes, long names, `gc=`/`sc=`/`scx=`
  prefixes, loose matching, negation `\P{…}` — `scx=` has no bare-name form, same as PCRE2), and the 63
  standard binary properties (`\p{Alphabetic}`, `\p{White_Space}`, `\p{Emoji}`, no namespace of their own,
  same as PCRE2) run on REAL's linear engine — `engine()` reports `Real`. Other UAX44 properties
  (`Word_Break`, `Age`, …) raise `Error::Unsupported`; enable the `fallback` feature and
  `RegexBuilder::new(pat).fallback(true)` to delegate *those* to the `regex` crate (per pattern, forfeiting
  the linear-time guarantee — `engine()` reports `Fallback`). Not every UAX44 property is in the `regex`
  crate's tables either — `\p{Bidi_Class=L}` is refused on both sides — so the error tells you which case
  you are in rather than selling a feature that cannot help.
- **`\u{…}` is `\x{…}`.** The regex crate treats `\u{…}` / `\U{…}` / `\x{…}` as synonyms. REAL now
  accepts `\u{…}` as the ECMAScript braced form (the same code-point path as `\x{e9}` / `\u00e9`).
  `\U{…}` is *not* that form — REAL's `\U` is Python's eight hex digits (`\U0001F600`); `\U{e9}`
  stays a syntax error. `re` rejects `\u{…}`; accepting it is a documented superset.
- **Class set notation — declined (rust-only syntax).** Nested character classes (`[a[b]]` = union) and the
  set operators `&&` / `--` / `~~` are `regex`-crate syntax; Python `re` — REAL's model — reads `[` as a
  literal inside a class, so the two would parse the same pattern differently. The crate declines these up
  front with `Error::Unsupported` (never a silent mis-match); escaped forms (`[\[]`) and ordinary ranges stay
  accepted. `fallback` delegates them to `regex`. Planned alongside `\p{}` as drop-in-completeness features.
- **`Captures::expand` is not offered.** The regex crate writes a match into a `String` via a
  `$name` template. Use `replace` / `replace_all` (same `$` spelling) instead.
- **Possessive quantifiers — a deliberate superset (per Python 3.11+/PCRE2), read silently
  differently.** REAL reads a `+` right after a quantifier
  (`x?+`, `x*+`, `x++`, `x{n,m}+`) as **possessive** — match maximally, never give back — the Python `re`
  3.11+/PCRE2 grammar (REAL and `re` agree on the whole family). The `regex` crate has no possessives and
  reads the same text as **nested repetition** (`x?+` ≡ `(?:x?)+`): both engines compile the pattern and
  the spans legitimately differ (`a?+` on `"aaaa"`: REAL `(0,1)(1,2)(2,3)(3,4)`, the crate `(0,4)`;
  `a++a` on `"aaaa"`: REAL finds nothing, the crate `(0,4)`). `tests/possessive.rs` pins both readings;
  the differential fuzzer masks the class by form (`has_possessive_quantifier`).
- **`RegexSet` — offered (which-matched).** Multi-pattern set matching: `RegexSet::new` /
  `is_match` / `matches` (bitset, construction order). Stage-1 is N independent walks with
  per-pattern early-exit — not a fused single-pass (that is a follow-up). Not the same as
  C++ `real::dfa` (maximal-munch lexer).
- **Bounded lookarounds — a *positive* divergence.** REAL supports bounded lookahead `(?=…)` / `(?!…)` and
  lookbehind `(?<=…)` / `(?<!…)` in linear time. The `regex` crate and RE2 support neither. This is a
  documented superset, not a gap.
- **A known upstream `regex` leftmost-first violation.** REAL's differential fuzzer found a case where the
  `regex` crate (1.12.x) is wrong and REAL agrees with Python `re`: on `A|.AA` over `"\n#AA"`, the leftmost
  match is `[1,4)` (it begins with `.`), but the crate's reverse-suffix optimization skips it. Fixed upstream in
  [rust-lang/regex#1373](https://github.com/rust-lang/regex/pull/1373) (found by REAL's fuzzer); repro + analysis
  in `fuzz/known_rust_bugs/`, and the differential fuzzer skips the class so it does not read as a REAL bug.

## The fallback feature

```toml
real-regex = { version = "…", features = ["fallback"] }
```
Off by default (the crate stays strict and pulls no extra dependency). On, a pattern REAL cannot run
linearly can be delegated per pattern with `RegexBuilder::new(pat).fallback(true)`; `Regex::engine()` returns
`Engine::Fallback` for it. `Regex::new` is always strict.

## License

MIT.
