<!--
The Drop-in target page for RE2, on the shared per-target template. Canon = the
file comment of include/real/compat/re2/re2.hpp -- distilled here, not copied.
-->

# Drop-in for RE2

**Full drop-in, strict — `real::compat::re2`, header-only, zero-dep.** The `RE2`
surface you already type, on REAL's linear engine; every accepted pattern is
guaranteed linear, and a construct this layer cannot honor is a clean
`ok() == false` — RE2's own no-exception contract, never a silent backtrack.
RE2 itself is a test-time oracle only, never linked.

## Adopt / swap

```cpp
#include <real/compat/re2/re2.hpp>
using real::compat::re2::RE2;

std::string user, host;
RE2::PartialMatch("info@example.com", R"((\w+)@(\w+))", &user, &host);
// host == "example" — group 2, via RE2's typed Arg extraction
```

## API offered

- **Statics** — `FullMatch` / `PartialMatch` / `Consume` / `FindAndConsume`
  (typed `RE2::Arg` extraction: one `&int`, `&std::string`, … per group),
  `Replace` / `GlobalReplace`, `QuoteMeta`.
- **Instance** — `ok()` / `error()` / `error_code()` / `pattern()` /
  `NumberOfCapturingGroups()`.
- **`RE2::Options`** — `set_longest_match` is honored by every unanchored
  search; options with no REAL-side equivalent are rejected at construction,
  never silently ignored (details below).
- **`RE2::Set`** — Add / Compile / Match, multi-pattern which-matched on
  `real::regex_set`.

One include: `<real/compat/re2/re2.hpp>`. Object-level reference:
{doc}`RE2 compatibility <../reference/compat-re2>`.

## Differences & limitations

Distilled from the layer's own contract — the exhaustive version is the file
comment of
[`re2.hpp`](https://github.com/RECHE23/real-regex/blob/main/include/real/compat/re2/re2.hpp):

- **REAL is a near-total syntax superset** — everything RE2 compiles, plus
  bounded lookarounds and possessive quantifiers (RE2 rejects both outright)
  and a wider `\p{…}` set (`sc=`/`scx=` and the UCD binary properties RE2's
  grammar lacks).
- **Two places REAL is deliberately stricter** — duplicate capture names
  (`(?P<n>…)(?P<n>…)`, ambiguous match-by-name) and surrogate code points in
  `\x{…}`/`\u`/`\U`/`\N`. Both a clean `ok() == false`; both principled
  divergences, and the only two entries in this layer's differential-fuzz
  known-gap ledger.
- **No fallback** — RE2 is not a runtime dependency, so there is nothing to
  delegate to: an unsupported pattern or option rejects immediately with
  `error()` explaining why (unlike the `std::regex` layer's opt-in fallback).
- **Scope is RE2's default mode** — UTF-8, non-POSIX. `posix_syntax`, Latin-1,
  `never_nl`, `never_capture` reject at construction; `perl_classes` /
  `word_boundary` / `one_line` are inert exactly as in real RE2 outside POSIX
  mode.
- **`\C` is accepted** — one byte, possibly mid-codepoint, safe on this
  byte-offset API.

## Comparison

Single-pattern numbers against RE2, PCRE2-JIT and `std::regex` live in the
[performance ledger](https://github.com/RECHE23/real-regex/blob/main/docs/BENCHMARKS.md);
the reading is {doc}`Performance <../performance/index>`. RE2 cannot compile
the lookaround row at all.

Multi-pattern is covered too: `RE2::Set` compiles into `real::regex_set`, a
shipped hybrid — per-pattern walks below a calibrated set size (competitive
there, sometimes the fastest route), and above it the DFA-eligible members
switch automatically onto a fused single-pass scan that stays flat as the set
grows. Measured against Google's `RE2::Set` on the same host it is near
parity — ahead on sparse corpora, a hair behind on dense ones. The residual
cuts both ways: lookaround and Unicode-`\w` patterns stay on per-pattern walks
and degrade at large N — but `RE2::Set` cannot compile a lookaround at all,
and REAL leads on multi-pattern extraction. Numbers in
{doc}`Performance <../performance/index>`.
