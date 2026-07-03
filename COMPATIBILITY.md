# real::compat — std::regex compatibility {#compat}

`real::compat` (header `<real/std/regex.hpp>`) is a drop-in for the `<regex>` surface on the
`char` path. It runs your pattern on **`real`** — linear-time and ReDoS-safe — wherever that is
provably equivalent to `std::regex` (ECMAScript), and falls back to `std::regex` everywhere else.

> New here? Start with the migration tour: [Drop-in for std::regex](@ref std_regex_dropin). This page
> is the exhaustive per-feature reference; REAL's own differences from Python `re` are in
> [the divergences page](@ref divergences).

**The contract:** behave identically to the ECMAScript spec where `real` can prove it, and fall
back to `std::regex` otherwise — *never a silent divergence*. The ECMAScript spec is the primary
oracle; `std::regex` (libstdc++/libc++) is a secondary oracle whose known deviations from the spec
are catalogued below (where `real`, following the spec, is the correct one).

## Feature scorecard

The public **status view** of REAL's regex features — the lines where REAL says something notable
against a full feature matrix. Statuses: **supported** (parity with Python `re`), **extension**
(REAL accepts it, `re` does not), **planned**, **excluded by design**. The *rationale* for each lives
in the [divergences page](@ref divergences); this table is the single status source and links there —
there is no competing table.

**Excluded by design is a closed door, not a missing feature.** Backreferences, recursion, and callouts
each make matching super-linear and would reopen the ReDoS door this engine exists to close — so they
raise a clear error rather than sitting on a roadmap.

| Feature | Status | Rationale | Since / target |
| --- | --- | --- | --- |
| Backreferences (`(a)\1`, `(?P=n)`) | **excluded by design** | non-regular → super-linear → ReDoS ([why](@ref div_rejected)) | — |
| Conditional groups `(?(id)…)`, recursion, callouts | **excluded by design** | non-regular control flow → ReDoS ([why](@ref div_rejected)) | — |
| Unicode property classes `\p{…}` | **planned** | opt-in header (needs the Unicode tables; not in `re`) | opt-in |
| `\N{NAME}` named characters | **supported** | binding resolves the name via `unicodedata` (no C++ table) | 2026.7 |
| `\N{U+XXXX}` scalar form | **extension** (PCRE2-style) | uniform C++/Python surfaces; `re` knows only names ([more](@ref div_named_scalar)) | 2026.7 |
| `\z` end-of-text anchor | **supported** | exact alias of `\Z` (Python 3.14's meaning) | 2026.7 |
| Octal escapes in a class `[\1]` `[\12]` | **supported** | every `\digit` is octal in a class (no back-refs there) | 2026.7 |
| Variable-width lookbehind `(?<=a|bb)` | **extension** | bounded → still linear; `re`/PCRE reject it ([more](@ref div_lookbehind)) | — |
| Word-edge anchors `\<` `\>` | **extension** | word-start / word-end; `re` has no such escape | — |
| POSIX `[[:alpha:]]` classes | **supported** | exact `re`-parity: `re` reads them as a *literal* class (currently with a `FutureWarning`); REAL matches the same characters | — |
| Global flags `i m s x a` (and `(?imsxa)` prefix) | **supported** | `re` semantics; `re.U` is a no-op, `re.L` excluded | — |
| Scoped inline flags `(?imsxa:…)` / `(?-…:…)` / `(?…-…:…)` | **supported** | per-scope `i m s x a` (Python 3.11 semantics), exact `re`-parity in str and bytes | 2026.7 |

## How a pattern is routed

A `real::compat::regex` is built with `flags::bytes | flags::ecma` so `real`'s byte-oriented,
ECMAScript-`$` (end-only), ECMAScript-`.` (excludes `\n` and `\r`) semantics line up with
`std::basic_regex<char>`. Routing:

1. **A POSIX grammar** (`basic`/`extended`/`awk`/`grep`/`egrep`) or `collate` → `std::regex` up front.
2. Otherwise `real` is tried. If it **rejects** the pattern (a feature it cannot represent), the
   layer falls back to `std::regex`, which may accept it. A pattern invalid for *both* throws
   `real::compat::regex_error` (a `std::regex_error`) carrying std's exact `.code()`.

`regex.uses_real()` reports which backend won; `regex.nullable()` reports whether the pattern can
match empty (the state that routes `regex_replace`/iterators to `std` — see the traversal rows below).

## What runs on real (linear, ReDoS-safe)

Literals, concatenation, alternation, `.` (ECMAScript), character classes & ranges, `\d \w \s`
(+negations), `^ $ \b \B`, greedy/lazy quantifiers `* + ? {m,n}`, groups (capturing,
non-capturing, named), **lookahead and lookbehind** (bounded — `real`'s ReDoS-safe lookaround),
ASCII `icase`, `multiline`. Non-ASCII **literals** match byte-for-byte like `std::regex<char>`.

## What falls back to std::regex (loses real's ReDoS-safety)

| Construct | Why | Treatment |
|---|---|---|
| Backreferences `\1`, `(?P=n)` | `real` does not implement them | `real` rejects → std fallback (std supports them) |
| **Raw** non-ASCII bytes inside a class `[é]` | `real`'s bytes path rejects raw high bytes in `[...]` (a `\xHH` escape does *not* — `[\x80-\xff]` stays on `real` as a byte class, matching `std::regex<char>`) | clean rejection → std fallback |
| Unbounded / oversized lookaround | exceeds `real`'s bounded-lookaround cap | `real` rejects → std fallback |
| POSIX grammars, `collate` | `real` is ECMAScript-only | screened to std up front |
| `\0` followed by a digit (`\00`, `\012`) | `real` reads a legacy octal escape (Annex B); libstdc++ reads `\0`=NUL then a literal digit — strict ECMAScript makes it a syntax error, so neither is the spec answer | screened to std up front (a both-accept divergence otherwise; the fuzzer found it) |
| **Nullable patterns in `regex_replace`/iterators** | empty-match *traversal* (advance-after-empty-match) differs between `real` (Python) and ECMAScript | a real-backed pattern that can match empty (`a*`, `(x)?`) routes those operations to a lazily-built `std::regex` — **per operation**, so `search`/`match` keep `real`'s ReDoS-safety even for nullable-ReDoS like `(a*)*` |

These patterns run on `std::regex` and therefore lose the linear-time guarantee — a documented,
non-silent trade. Prefer ReDoS-safe equivalents for untrusted input.

## regex_replace

The replacement format is ECMAScript: `$$` → `$`, `$&` → the whole match, `` $` `` → the text since
the previous match, `$'` → the text to the end, `$N` / `$NN` → group N (matching
`std::regex_replace`, which the differential harness pins). `format_first_only` and `format_no_copy`
are honoured. A non-nullable real-backed pattern runs the substitution on `real`'s linear traversal —
measured **6–17× faster** than `std::regex_replace`; a nullable one falls back to `std`.

The real expander honours only `format_first_only`, `format_no_copy` (and the `match_any` hint);
**any other flag routes the whole substitution to `std::regex_replace`** (so compat == std) — a
constraining match flag (`match_not_bol`, `match_continuous`, …, which the ECMAScript expander cannot
apply) or **`format_sed`** (POSIX replacement syntax it would mis-read). A format containing **`$0`**
also routes to std: `$0` is platform-variant (libstdc++ = the whole match, strict-ECMAScript/MSVC = a
literal `$0`), so `real` cannot pick one without risking a silent divergence.

## Errors and thread-safety

Every compat entry point that can fail throws a **`real::compat::regex_error`** (which *is* a
`std::regex_error`), never a raw `std` one — construction (POSIX/wide/custom-traits screens, the
real→std fallback) and the lazy `std` build alike. A pattern `real` accepts but `std` rejects (a
*real superset*, e.g. `\A` = literal `A` on `real`, rejected by `std`) runs `search`/`match` on
`real`; it reaches `std` only via a constraining match flag or a nullable / `$0` / sed
`regex_replace`, or an iterator routed to `std`. If that `std` build fails, the error is a **late but
homogeneous** `compat::regex_error` — construction succeeded (the pattern is valid for `real`), and
the error appears only when the `std`-only operation is first invoked (an error, never a silent
wrong result).

The `std` engine for a real-backed pattern is built lazily on demand under a **build mutex**, and the
read is taken under the same lock, so concurrent `const` operations on one shared regex object are
race-free for **both** nullable and non-nullable patterns — preserving `std`'s guarantee that
concurrent `const` operations on one object are safe (verified under ThreadSanitizer — `make tsan`,
which also runs in CI). The build is per operation and cold relative to matching. (`std::once_flag`
would be lighter but is non-copyable, and `basic_regex` must stay copyable like `std::regex`; the
static mutex keeps the value semantics defaulted.)

## Behaviour after a failed match

A failed `regex_search` / `regex_match` leaves the `match_results` **ready** (`ready() == true`,
`size() == 0`, `empty()`), exactly like `std::regex`. `operator[]` / `position` / `length` / `str`
for an out-of-range group index return an **end-anchored unmatched sub_match** (`{end, end, false}`,
so `position()` is the full sequence length and `length()` is `0`) — never out of bounds. A token
selector like `{2}`/`{5}` or a field `< -1` relies on this; a field `< -1` is undefined in `std`, and
compat is *safe* there, yielding an unmatched token.

## Platform-variant std::regex (MSVC vs libstdc++/libc++)

`std::regex` is not identical across implementations, and a few of its behaviours are
platform-variant. Where `real::compat` **wraps** `std` (a fallback pattern, a wide `CharT`, a
constraining flag) it is `≡` the *local* `std` by construction; where it is **real-backed** it
chooses the spec-reasonable behaviour, which may differ from a given `std` on those points:

- **Out-of-range / unmatched `sub_match`.** libstdc++/libc++ anchor it at the sequence end
  (`position() == length`); MSVC-`std` leaves it singular (`position() == 0`). `real::compat` is
  real-backed, so its `match_results` come from `real`'s offsets and it is **end-anchored
  universally** — matching libstdc++/libc++, differing from MSVC on `.first`/`.second`/`.position`
  (the participation flag and `str()` are empty/`false` everywhere). This is a deliberate, documented
  choice, not a bug; the tests assert the end-anchored contract directly and only differ against
  `std` on the platform-invariant fields.
- **Escape strictness (`\0`+digit).** `\0` followed by a digit (`\00`, `\012`) is screened to `std`
  (see the fallback table). `std` itself is platform-variant: libstdc++/libc++ accept it (Annex B
  legacy octal, lenient), MSVC-`std` rejects it (`error_escape`, strict). `real::compat` defers to
  the local `std` on both sides — it **throws iff `std` throws**, and where `std` accepts, it runs on
  `std` and matches it. So the *construction* of such a pattern succeeds on Linux and throws a
  `real::compat::regex_error` on MSVC, exactly as the platform's `std::regex` does.

## Iteration (regex_iterator)

`real::compat::regex_iterator` (with `sregex_iterator` / `cregex_iterator`) walks the non-overlapping
matches like `std::regex_iterator`. Same per-operation routing as `regex_replace`: a non-nullable
real-backed pattern drives `real`'s linear traversal (repeated region search — a non-nullable pattern
never matches empty, so the position always advances and the ECMAScript and `real` sequences agree);
the std backend and nullable patterns wrap `std::regex_iterator` (whose empty-match advance *is*
ECMAScript's). The default-constructed iterator is the end sentinel. Constructing from a temporary
regex is `=delete`d (it would dangle), exactly as `std::regex_iterator`. The differential fuzzer
compares the whole **span sequence** (and each match's `prefix()`/`suffix()`), not just the first
match — the empty-match traversal being the risk it pins.

`regex_token_iterator` (with `sregex_token_iterator` / `cregex_token_iterator`) wraps that iterator,
so it inherits the nullable routing unchanged. For each match it yields the requested fields in
order: `N >= 0` is capture group `N` (a non-participating group is an empty `matched == false`
token), and `-1` is the text *before* this match since the previous one (the match's `prefix()`),
which makes `-1` a splitter. After the last match a trailing `-1` field yields the final suffix
**only when it is non-empty** (an empty field *between* adjacent matches is still produced — the
asymmetry `std` pins); with `-1` and no match at all, the whole sequence is the single token. The
fuzzer compares the `(str, matched)` token sequence for the `-1` and `0` fields.

## Match flags (match_flag_type)

`regex_search` / `regex_match` and both iterators take an optional `match_flag_type` (default
`match_default`). The rule is **honor-on-`real` or fall back to `std`, never accept-then-ignore**:

- `match_default` and `match_any` keep the `real` backend. `match_any` is a non-constraining hint
  (return *a* match) that `real` already satisfies by returning the leftmost match, so ignoring it is
  sound.
- **Any constraining flag** — `match_not_bol`, `match_not_eol`, `match_not_bow`, `match_not_eow`,
  `match_not_null`, `match_continuous`, `match_prev_avail` — is not expressible through `real`'s API,
  so that single operation routes to `std::regex` (lazy-built if the pattern is real-backed), which
  honors every flag by construction. The flags are translated by an exhaustive compat→std table.

This is a *per-operation* decision, like the nullable routing: a pattern keeps `real`'s ReDoS-safety
for its flag-free `search`/`match` calls and only the flagged call pays the `std` cost. Affining a
flag onto `real` (e.g. `match_continuous` → `real`'s anchored `match` at a position) is a measured
optimization left for later, not a hand-coded partition — the differential fuzzer would otherwise
have to police a mis-categorization. The fuzzer generates a random flag subset and compares
`compat(mf)` vs `std(mf)` on search + match + iterate, which is what proves the partition.

## Always-std parts of the surface (wregex, POSIX, nosubs)

`real` runs only the **`char` path with default traits, ECMAScript grammar, reporting every group**.
Everything outside that is routed to `std::regex` by a compile-time gate (`real_eligible<CharT, Traits>`)
plus the option screen — `real` is never even tried, so these are std by construction:

- **`wregex` / `wchar_t` (and `char8/16/32_t`, custom `Traits`)**: the gate is `constexpr`, so `real`'s
  char-only code (the byte `string_view`, `fill_from_real`, `next_real`) is **compiled out** for these
  instantiations — the `real::regex` alternative of the backend variant stays dead. `wregex::uses_real()`
  is always `false`. The wide typedefs are provided: `wregex`, `wsmatch`/`wcmatch`,
  `wssub_match`/`wcsub_match`, `wsregex_iterator`/`wcregex_iterator`,
  `wsregex_token_iterator`/`wcregex_token_iterator`; `regex_search`/`regex_match`/`regex_replace` are
  templated on `CharT` and dispatch the wide path to `std`.
- **POSIX grammars** (`basic`/`extended`/`awk`/`grep`/`egrep`) and **`collate`**: screened to `std` up
  front (`real` is ECMAScript-only).
- **`nosubs`**: `std` answers it by exposing only group 0, while `real` always reports every group — a
  structural both-accept divergence — so `nosubs` is screened to `std`. (Honoring it on `real` by
  truncating `match_results` to size 1 is a measured optimization for later, not a correctness need.)

## Intentional divergences from libstdc++ std::regex (spec-correct)

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

- **Surface**: `basic_regex<char>`, `sub_match`, `match_results` (+ `smatch`/`cmatch`), `regex_error`,
  `regex_search`, `regex_match`, `regex_replace`, the two iterators, the full `match_flag_type`,
  `wregex`, and the POSIX grammar engines. Empty-match traversal is *not* a fallback trigger for single
  `search`/`match` -- only for `regex_replace`/iterators, where the advance-after-empty-match rule
  differs from ECMAScript.
- `match_results` requires a **contiguous** iterator (a `std::deque` sequence is rejected at
  compile time): sub-matches are built from byte offsets.
- Matching against an rvalue `std::string` is deleted (the result would dangle), as in `real`/`std`.

## Performance (measured, real backend vs std::regex)

`regex_search`, compat/std time ratio (`<1` = compat faster): email-validate **0.22**, date
**0.13**, alternation **0.49**, long class scan **0.005**. ReDoS `(a+)+b` over `"a"*30` (no match):
**~1000× faster** (std backtracks catastrophically; compat stays linear).
