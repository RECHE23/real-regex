# `real::compat` — `std::regex` compatibility (S1: char, search/match)

`real::compat` (header `<real/std_compat.hpp>`) is a drop-in for the `<regex>` surface on the
`char` path. It runs your pattern on **`real`** — linear-time and ReDoS-safe — wherever that is
provably equivalent to `std::regex` (ECMAScript), and falls back to `std::regex` everywhere else.

**The contract:** behave identically to the ECMAScript spec where `real` can prove it, and fall
back to `std::regex` otherwise — *never a silent divergence*. The ECMAScript spec is the primary
oracle; `std::regex` (libstdc++/libc++) is a secondary oracle whose known deviations from the spec
are catalogued below (where `real`, following the spec, is the correct one).

## How a pattern is routed

A `real::compat::regex` is built with `flags::bytes | flags::ecma` so `real`'s byte-oriented,
ECMAScript-`$` (end-only), ECMAScript-`.` (excludes `\n` and `\r`) semantics line up with
`std::basic_regex<char>`. Routing:

1. **A POSIX grammar** (`basic`/`extended`/`awk`/`grep`/`egrep`) or `collate` → `std::regex` up front.
2. Otherwise `real` is tried. If it **rejects** the pattern (a feature it cannot represent), the
   layer falls back to `std::regex`, which may accept it. A pattern invalid for *both* throws
   `real::compat::regex_error` (a `std::regex_error`) carrying std's exact `.code()`.

`regex.uses_real()` reports which backend won.

## What runs on `real` (linear, ReDoS-safe)

Literals, concatenation, alternation, `.` (ECMAScript), character classes & ranges, `\d \w \s`
(+negations), `^ $ \b \B`, greedy/lazy quantifiers `* + ? {m,n}`, groups (capturing,
non-capturing, named), **lookahead and lookbehind** (bounded — `real`'s ReDoS-safe lookaround),
ASCII `icase`, `multiline`. Non-ASCII **literals** match byte-for-byte like `std::regex<char>`.

## What falls back to `std::regex` (loses `real`'s ReDoS-safety)

| Construct | Why | Treatment |
|---|---|---|
| Backreferences `\1`, `(?P=n)` | `real` does not implement them | `real` rejects → std fallback (std supports them) |
| Non-ASCII **inside a class** `[é]`, `[\x80-\xff]` | `real` rejects raw high bytes in `[...]` | clean rejection → std fallback |
| Unbounded / oversized lookaround | exceeds `real`'s bounded-lookaround cap | `real` rejects → std fallback |
| POSIX grammars, `collate` | `real` is ECMAScript-only | screened to std up front |

These patterns run on `std::regex` and therefore lose the linear-time guarantee — a documented,
non-silent trade. Prefer ReDoS-safe equivalents for untrusted input.

## Intentional divergences from libstdc++ `std::regex` (spec-correct)

`real::compat` follows the **ECMAScript spec**; the following are libstdc++ deviations that the
differential harness allowlists (the compat behavior is the spec behavior):

- **POSIX bracket expressions** `[[:digit:]]`: ECMAScript has *no* POSIX classes. `[[:digit:]]` is
  the literal character class `{[ : d i g t}` followed by `]+`. `real::compat` follows the spec.
  libstdc++ applies a **non-standard, non-portable** POSIX extension here — **libc++ does not**, so
  relying on it is already non-portable across `std::regex` implementations. *For POSIX classes, use
  the POSIX grammar (`regex_constants::extended`) explicitly, which routes to `std::regex`.*
- **Lookbehind** `(?<=…)` / `(?<!…)`: ES2018 has it and `real` implements it (bounded, ReDoS-safe);
  libstdc++'s ECMAScript engine rejects it. `real::compat` accepts and matches it.

## Syntax notes (for migrants)

The compat layer builds `real` with `flags::ecma`, which makes the engine follow ECMAScript grammar
rather than `real`'s default (Python-flavoured) one. The differences it aligns — each surfaced by the
differential fuzzer (517 k iterations, zero remaining both-accept divergence):

- `$` (no `multiline`) matches only the very end, not before a trailing `\n` (Python's `re` default).
- `.` (no dotall) excludes `\n` *and* `\r` (ECMAScript line terminators), not just `\n`.
- The escapes `\A \Z \< \>` (REAL anchors) and `\a` (Python bell) become **identity-escape literals**
  (`A Z < >`, `a`) — ECMAScript has no such escapes. `\n \r \t \f \v \0 \xHH` are unchanged.
- A `]` in the head of a class **closes** it: `[]` is the empty class, `[^]` matches any character
  (the ECMAScript "any incl. newline" idiom). Python treats a leading `]` as a literal member.
- Inline global flags `(?ims)` at the start of the pattern are supported; **scoped** groups
  `(?i:…)` are rejected (ECMAScript has no scoped inline flags either).

## Boundaries / current scope

- **S1**: `basic_regex<char>`, `sub_match`, `match_results` (+ `smatch`/`cmatch`), `regex_error`,
  `regex_search`, `regex_match`. Empty-match traversal is *not* a fallback trigger for single
  search/match; `regex_replace`/iterators (where empty-match traversal matters), the full
  `match_flag_type`, `wregex`, and the POSIX grammar engines are later slices.
- `match_results` requires a **contiguous** iterator (a `std::deque` sequence is rejected at
  compile time): sub-matches are built from byte offsets.
- Matching against an rvalue `std::string` is deleted (the result would dangle), as in `real`/`std`.

## Performance (measured, `real` backend vs `std::regex`)

`regex_search`, compat/std time ratio (`<1` = compat faster): email-validate **0.22**, date
**0.13**, alternation **0.49**, long class scan **0.005**. ReDoS `(a+)+b` over `"a"*30` (no match):
**~1000× faster** (std backtracks catastrophically; compat stays linear).
