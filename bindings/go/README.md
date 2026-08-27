# real (Go)

Go bindings to [REAL](https://github.com/RECHE23/real-regex), a linear-time (ReDoS-safe) regex
engine with bounded lookarounds, over its C ABI (`bindings/c/real_capi.h`) via cgo.

**v0.1 — cgo required, macOS-arm64 and linux-x86-64 only.** Cross-compilation and Windows/MSVC
are explicitly out of scope for this version.

## The one thing to know before migrating from `regexp`

**`\w`, `\d`, and `\s` are Unicode-aware by default here — `regexp` (RE2)'s are ASCII-only by
default.** `\w+` on `"café"` matches all of it (`café`, 5 bytes: c, a, f, é=2 bytes) under this
package; under `regexp`, `\w+` matches only `"caf"` (`é` is not `\w` under RE2's default ASCII
scope, so it is simply skipped, not part of any match). This is REAL following Python `re`'s own
default, not a bug on either side — see `TestFlavorDivergence_WordShorthandIsUnicodeByDefault`.

## Why this exists (beyond another `regexp`)

REAL is a strict *syntax superset* of RE2/`regexp` on the shared core: every pattern `regexp`
accepts, this package accepts identically (differential-tested against the stdlib, see
`Test_Differential_*`). On top of that, REAL supports constructs RE2 rejects outright at compile
time: bounded lookahead/lookbehind (`(?=...)`, `(?<=...)`, ...) and possessive quantifiers
(`a++`), both in linear time (no backtracking, no ReDoS exposure — REAL's whole design point). A
`regexp` user migrates without rewriting existing patterns, then gains access to constructs they
could not express before.

## API surface (v0.1)

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
| `(*Regexp) FindAllIndex` | `FindAllIndex(text, -1)` | byte offsets, `[start,end)`, group 0 only |
| `(*Regexp) FindSubmatch` / `FindStringSubmatch` | same | groups as bytes/strings; unset group is nil / `""` |
| `(*Regexp) FindAllSubmatch` / `FindAllStringSubmatch` | same | `n` as in `regexp` (0 → nil, <0 → all) |
| `(*Regexp) FindSubmatchIndex` / `FindAllSubmatchIndex` | same | every group's span; unset group is `-1,-1` |
| `(*Regexp) Split` | same | slices on matches; `n` as in `regexp` |
| `(*Regexp) FullMatch` | **no equivalent** | the whole ABI's `real_match(REAL_MODE_FULLMATCH)` — `regexp.MatchString` is really a *search* |
| `(*Regexp) ReplaceAll` | `ReplaceAll` | **template sigil differs**: this package uses REAL/Python-style `\1`/`\g<name>`; regexp `$1`/`$name`/`${name}` is an error, not a silent literal; `$$` is left as two dollars (regexp collapses it to one) — not translated |
| `RegexSet` (`CompileSet`, `IsMatch`, `Matches`, `Size`) | **no equivalent** | multi-pattern which-matched set — wraps `real::regex_set` (Stage-1 N-walks, or a fused single-pass DFA once enough members are DFA-eligible) directly, mirrors the Python binding's own native `RegexSet` |
| bounded lookaround, possessive quantifiers | **`regexp.Compile` rejects these patterns outright** | REAL-only; confirmed empirically in `Test_BeyondRE2_*` |

## Flags

REAL's native flag bitmask (`bindings/c/real_capi.h`'s own documented numbering table) has no
`regexp`-equivalent constants — `regexp` has no flags parameter at all (inline `(?i)`-style
modifiers instead). Not yet exposed in this package's Go API (v0.1 always compiles with no
flags); a future version would need its own named Go constants, not borrowed from either engine's
convention.

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
