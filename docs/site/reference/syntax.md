<!--
The pattern-language reference: hand-authored (no Doxygen symbol exists for "the
syntax"). Partition: Features = per-construct STATUS, differences-from-re =
divergences and rationale, COMPATIBILITY.md = compat status -- this page is the
MEANINGS, cross-linked, never copied.
-->

# Pattern syntax

The pattern language REAL accepts — every construct matches in **guaranteed
linear time**, never backtracking (ReDoS-safe by construction). Per-construct
*status* (accepted / rejected / extended vs Python `re`) lives in
{doc}`Features <../features>`; the intentional *divergences* and their
rationale in {doc}`Differences from re <../differences-from-re>`.

## Literals and escapes

| Syntax | Meaning |
|---|---|
| `abc` | literal bytes (UTF-8 patterns match their UTF-8 bytes) |
| `\.` `\*` `\\` … | escaped metacharacter, matched literally |
| `.` | any codepoint except `\n` |
| `\n \t \r \f \v \a \0` | control escapes |
| `\xHH` `\x{…}` | a code point by hex — two digits, or any scalar in braces (`\x{1F600}`) |
| `\uXXXX` `\UXXXXXXXX` | a code point by 4- or 8-digit hex |
| `\N{U+XXXX}` | a code point by scalar notation |

`\N{NAME}` (a character by its Unicode *name*) is a Python-surface addition:
the wrapper resolves the name via `unicodedata` before the engine sees the
pattern — see {doc}`the re drop-in <../drop-in/re>`. Surrogate code points are
rejected in every escape form.

## Character classes

| Syntax | Meaning |
|---|---|
| `[abc]` `[a-z]` `[^abc]` `[é]` `[à-ÿ]` | character class, ASCII **and** non-ASCII code-point members / ranges (str mode); `[^…]` matches any code point outside the set |
| `\w \W \d \D \s \S` | word / digit / space classes — Unicode in text mode (like `re`), ASCII in bytes mode or under `a` |

## Quantifiers

| Syntax | Meaning |
|---|---|
| `x*` `x+` `x?` | greedy; append `?` for lazy |
| `x{n}` `x{n,}` `x{,m}` `x{n,m}` | counted repetition (greedy or lazy; counts capped at 1000) |
| `x*+` `x++` `x?+` `x{n,m}+` `(?>x)` | **possessive quantifiers and atomic groups** (no give-back) — over a single atom or one wrapped in one capturing group; linear time, beyond RE2/rust-regex |

## Groups

| Syntax | Meaning |
|---|---|
| `(…)` `(?:…)` | capturing / non-capturing group |
| `(?P<name>…)` `(?<name>…)` | named capturing group (Python and .NET styles) |
| `a\|b` | alternation, leftmost branch preferred |

## Anchors and boundaries

| Syntax | Meaning |
|---|---|
| `^` `$` | line/text anchors (Python semantics: `$` also matches before a final `\n`) |
| `\A` `\Z` | strict text start / end |
| `\b` `\B` | word boundary / non-boundary (Unicode word characters in text mode, ASCII in bytes mode or under `a`) |
| `\<` `\>` | start / end of word (REAL extension, not in Python `re`) |

## Lookarounds

**Bounded lookarounds match in linear time — REAL's differentiator.** Lookahead
`(?=…)` / `(?!…)` and lookbehind `(?<=…)` / `(?<!…)`, each length-bounded and
capture-free. Variable-width lookbehind such as `(?<=a|bb)` is accepted —
beyond `re`/PCRE's fixed-width limit.

## Unicode properties

**`\p{…}` / `\P{…}` match natively and linearly** — a superset of `re`, which
has none of this: General_Category (`\p{L}`, `\p{Nd}`, …), Script (`sc=`,
short ISO codes), Script_Extensions (`scx=`), and the 63 standard binary
properties (`\p{Alphabetic}`, `\p{Emoji}`, …).

Matching is UTF-8 code-point-aware throughout: classes and `.` accept
non-ASCII, `\w \d \s \b` and `IGNORECASE` are Unicode in text mode (ASCII
under `flags::ascii` / `re.A`), and no match boundary ever splits a character.

## Flags

| Syntax | Meaning |
|---|---|
| `(?imsxa)` prefix | global flags: `i` case-insensitive (Unicode fold in text mode) · `m` multiline · `s` dotall · `x` verbose (ignore unescaped whitespace and `#` comments outside classes) · `a` ASCII (`re.A`: keep `\w \W \d \D \s \S \b \B \< \>` and icase folding ASCII, even in text mode) — also `real::flags` on the constructor |

## What is rejected

Backreferences, conditional groups, and a possessive/atomic construct over a
compound body (`(?:ab)*+`, `(?>ab|a)`) are rejected with `real::regex_error` —
never a silent divergence. **Excluded by design is a closed door**: the
per-construct status and the reasons live in {doc}`Features <../features>`.

## Example

Compiled and run by the `example-check` gate on every push:

```{literalinclude} ../../../examples/cpp/reference_syntax.cpp
:language: cpp
:start-after: "// [reference]"
:end-before: "// [/reference]"
```

## See also

- Per-construct status: {doc}`Features <../features>`.
- Intentional divergences and rationale:
  {doc}`Differences from re <../differences-from-re>`.
- The APIs that consume these patterns: {doc}`basic_regex <basic_regex>`,
  {doc}`the Python API <python>`.
- The compat status tables:
  [`docs/COMPATIBILITY.md`](https://github.com/RECHE23/real-regex/blob/main/docs/COMPATIBILITY.md).
