# real (Go)

Go bindings to [REAL](https://github.com/RECHE23/real-regex), a linear-time (ReDoS-safe) regex
engine with bounded lookarounds, over its C ABI (`bindings/c/real_capi.h`) via cgo.

**v0.3 — cgo required, macOS-arm64 and linux-x86-64 only.** Cross-compilation and Windows/MSVC
are explicitly out of scope for this version.

## The one thing to know before migrating from `regexp`

**`\w`, `\d`, and `\s` are Unicode-aware by default here — `regexp` (RE2)'s are ASCII-only by
default.** `\w+` on `"café"` matches all of it (`café`, 5 bytes: c, a, f, é=2 bytes) under this
package; under `regexp`, `\w+` matches only `"caf"` (`é` is not `\w` under RE2's default ASCII
scope, so it is simply skipped, not part of any match). This is REAL following Python `re`'s own
default, not a bug on either side — see `TestFlavorDivergence_WordShorthandIsUnicodeByDefault` and
`TestDocumentedUnicodeClassDivergence`.

A second, reached only through the `[]byte` methods: on **malformed UTF-8**, `regexp` substitutes
U+FFFD for an invalid byte and matches it as one rune, while a malformed byte is never a codepoint
here and no consuming class accepts it — `.` matches nothing on a lone `0xFF`. Zero-width matches do
NOT differ: `^`, `$` and empty matches answer identically on such input. Pinned in both directions by
`TestMalformedUTF8ConsumingDivergence`.

**And three where the two flavors read the same text differently.** These are *silent*: both engines
compile the pattern and the match differs, which is the shape you cannot see coming. Pinned in both
directions by `TestFlavorDivergence_ThreeSilentSyntaxReadings`.

| pattern | `regexp` reads | this package reads |
| --- | --- | --- |
| `a{,2}`, `a{,}` | literal text (RE2 has no such form) | `{0,2}` / `{0,}` — Python's shorthand |
| `[[:alpha:]]`, `[[:digit:]]` | a POSIX class | the class `[[:alph]` — members `[ : a l p h` — followed by a literal `]`, which is `re`'s reading, so it matches `a]` and not `a` |
| `\<w\>` | an escaped literal `<w>` | word-start / word-end anchors, a REAL extension |

All five have one root, and it is worth stating plainly: **the API here is `regexp`'s, the engine is
Python `re`'s.** Where the two flavors disagree about text they both accept, this package follows
`re`. That is the design, not an oversight — it is why these differences exist at all, and why the
list is a list rather than a bug report.

The rest of the `Regexp` surface is asked of `regexp` directly — 57 916 comparisons across every
`Find*`, `Match*`, `Split` and `ReplaceAll` form, crossed with `n` caps, `[]byte` and `string`
halves, and empty-matchable patterns (`TestFindFamilyMatchesRegexp`,
`TestByteAndStringHalvesAgree`, `TestReplaceAllMatchesRegexp`). Read what that sweep is: it varies
the **methods** over a fixed pattern corpus. It is strong evidence about the API and no evidence at
all about syntax a corpus does not contain — which is exactly how the three above went unlisted.

## Why this exists (beyond another `regexp`)

REAL **compiles** every pattern `regexp` compiles (differential-tested against the stdlib, see
`Test_Differential_*`), and matches identically **except for the five flavor differences listed
above**. That qualifier is load-bearing, and this sentence used to omit it.

On top of that, REAL accepts constructs `regexp` rejects outright at compile time. These are a
different thing from the five above, which are about text both engines accept — here `regexp.Compile`
returns an error and this package does not. **This list is open and deliberately carries no count**;
among them:

- bounded lookahead / lookbehind (`(?=...)`, `(?<=...)`, etc.) and possessive quantifiers (`a++`),
  both in linear time — no backtracking and no ReDoS exposure, which is REAL's whole design point;
- a `\` before a non-ASCII character is that character (`\é` matches `é`, `[\à-\é]` is a range),
  Python `re`'s rule — `regexp` answers `invalid escape sequence`, as it does for `\q`;
- `\Z` (end-of-text anchor here, `invalid escape sequence` there);
- `(?#...)` comments (`invalid or unsupported Perl syntax`);
- `{2}a` — a brace with nothing to repeat is literal text here, `missing argument to repetition
  operator` there.

The count is left open on purpose: a closed one invites the same correction the flavor list needed,
and nothing here depends on the total. The **five** flavor differences above are a closed list,
because each is a case where both engines compile and only one can be right about the match.

A `regexp` user migrates without rewriting existing patterns, then gains access to constructs they
could not express before.

## API surface (v0.3)

| This package | `regexp` equivalent | Notes |
|---|---|---|
| `Compile` / `MustCompile` | same | byte-oriented pattern/subject, no separate rune handling needed |
| `QuoteMeta` | same | delegates to `regexp.QuoteMeta`, not C++ `compat::re2::QuoteMeta` (that one escapes a larger set) |
| `Match` / `MatchString` (package) | same | compile + search; the handle is closed before return |
| `(*Regexp) String` | same | the source text, kept on the Go value (the C ABI has no getter); Close does not clear it |
| `(*Regexp) Close` | *(none — GC only)* | releases the C++ object explicitly; a finalizer is a safety net, not a substitute |
| `(*Regexp) NumSubexp` / `SubexpNames` | same | |
| `(*Regexp) Match` / `MatchString` | same | a *search*, not a full-string match — see FullMatch |
| `(*Regexp) Find` / `FindString` / `FindIndex` / `FindStringIndex` | same | leftmost match; `Find` is nil on no match |
| `(*Regexp) FindAll` / `FindAllString` / `FindAllStringIndex` | same | `n` as in `regexp` (0 → nil, <0 → all) |
| `(*Regexp) FindAllIndex` | same | byte offsets, `[start,end)`, group 0 only; `n` as in `regexp` (0 → nil, <0 → all) |
| `(*Regexp) FindSubmatch` / `FindStringSubmatch` | same | groups as bytes/strings; unset group is nil / `""` |
| `(*Regexp) FindAllSubmatch` / `FindAllStringSubmatch` | same | `n` as in `regexp` (0 → nil, <0 → all) |
| `(*Regexp) FindSubmatchIndex` | same | every group's span; unset group is `-1,-1` |
| `(*Regexp) FindAllSubmatchIndex` | same | every match's group spans; unset group is `-1,-1`; `n` as in `regexp` |
| `(*Regexp) FindStringSubmatchIndex` | same | the leftmost match's group spans as byte offsets into `s`; unset group is `-1,-1` |
| `(*Regexp) FindAllStringSubmatchIndex` | same | every match's group spans as byte offsets into `s`; `n` as in `regexp` |
| `(*Regexp) Split` | same | slices on matches; `n` as in `regexp` |
| `(*Regexp) FullMatch` | **no equivalent** | the whole ABI's `real_match(REAL_MODE_FULLMATCH)` — `regexp.MatchString` is really a *search* |
| `(*Regexp) ReplaceAll` | `ReplaceAll` | **template sigil differs**: this package uses REAL/Python-style `\1`/`\g<name>`; regexp `$1`/`$name`/`${name}` is an error, not a silent literal; `$$` is left as two dollars (regexp collapses it to one) — not translated |
| `RegexSet` (`CompileSet`, `IsMatch`, `Matches`, `Size`) | **no equivalent** | multi-pattern which-matched set — wraps `real::regex_set` (Stage-1 N-walks, or a fused single-pass DFA once enough members are DFA-eligible) directly, mirrors the Python binding's own native `RegexSet` |
| bounded lookaround, possessive quantifiers | **`regexp.Compile` rejects these patterns outright** | REAL-only; confirmed empirically in `Test_BeyondRE2_*` |

## Flags

REAL's native flag bitmask (`bindings/c/real_capi.h`'s own documented numbering table) has no
`regexp`-equivalent constants — `regexp` has no flags parameter at all (inline `(?i)`-style
modifiers instead). Not exposed in this package's Go API; a future version would need its own named
Go constants, not borrowed from either engine's convention.

One flag is set unconditionally, and it is not a choice this package leaves open: `dollar_endonly`,
so that `$` means what it means in `regexp`. Without it the engine's `re` default applies and `$`
also matches just before a trailing newline, so `foo$` matched `"foo\n"` here and did not under
`regexp` — a silent difference on one of the most ordinary patterns there is. The Rust crate sets the
same flag for the same reason.

## Vendoring

`vendor_include/` and `real_capi.{h,cpp}` in this directory are a **generated, committed**
snapshot of `../../include/real` and `../../bindings/c/real_capi.{h,cpp}` — required (not just a
convenience) because a module fetched via `go get` has no access to the rest of the monorepo.
Never edit them directly:

```
make go-vendor         # regenerate from the source of truth
make go-check-vendor   # CI gate: fails if the committed snapshot has drifted
```

## Versioning

This module is tagged independently of the engine's own CalVer releases (`v2026.7.x`), using
Go's monorepo tag-prefix convention: `bindings/go/vX.Y.Z`. `go get
github.com/RECHE23/real-regex/bindings/go@vX.Y.Z` resolves against that tag, not the engine's
own tags — the two version sequences are unrelated by design.
