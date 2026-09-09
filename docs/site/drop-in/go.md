<!--
The Drop-in target page for Go's regexp, on the shared per-target template.
Canon = bindings/go/README.md (the binding's README, always up-to-date) --
DISTILLED here, not copied: this page shares no prose with it (measured: 0 of the
canon's 9 blocks appear verbatim, though the page is 92% of its length). Edit the
canon first when a FACT changes, then restate it here in this page's own words;
`make check-doc-mirror` verifies the canon still exists and deliberately compares
no prose, because a distillation is allowed to say less, and differently. The
header said only "Canon =" until the v0.2.0 arity break, and the ambiguity cost a
manual comparison nobody could have automated.
-->

# Drop-in for Go `regexp`

**v0.3 subset, strict (cgo; macOS-arm64 & linux-x86-64 only).**
The methods below, not the whole `regexp.Regexp` surface. Every accepted pattern is guaranteed linear.
An unsupported construct is rejected at compile time instead of silently backtracking.

## Adopt / swap

```go
import real "github.com/RECHE23/real-regex/bindings/go"

re := real.MustCompile(`\d+`)
re.MatchString("x42")   // true — a search, like regexp.MatchString
```

## API offered

`Compile` / `MustCompile`, `QuoteMeta`, package `Match` / `MatchString`,
`(*Regexp) Close`, `String`, `Match` / `MatchString`,
`Find` / `FindString` / `FindIndex`, `FindAll` / `FindAllString` / `Split`,
`FindSubmatch` / `FindStringSubmatch` / `FindAllSubmatch` / `FindAllStringSubmatch`,
`FindAllIndex`, `FindSubmatchIndex` / `FindAllSubmatchIndex` / `FindAllStringSubmatchIndex`,
and `ReplaceAll`.

Beyond `regexp` — additions and refusals, each visible at compile time (the *silent*
differences, where both engines compile and the match differs, are listed under
Differences & limitations below):

- `(*Regexp) FullMatch` — whole-string match; `regexp.MatchString` is really a
  *search*.
- `RegexSet` (`CompileSet`, `IsMatch`, `Matches`, `Size`) — multi-pattern
  which-matched set; `regexp` has no equivalent.
- Bounded lookahead / lookbehind (`(?=…)`, `(?<=…)`, etc.) and possessive
  quantifiers (`a++`); `regexp.Compile` rejects these patterns outright.
- A `\` before a non-ASCII character is that character (`\é` matches `é`,
  `[\à-\é]` is a range) — Python `re`'s rule; `regexp` answers `invalid escape
  sequence` there, as it does for `\q`.
- **ReplaceAll template sigil differs** — this package uses REAL/Python-style
  `\1` / `\g<name>`. A `$1` / `$name` / `${name}` template is an error, not a
  silent literal; it is not translated to `\1`. `$$` is regexp's escape for a
  literal dollar; this package leaves both dollars.

Object-level reference:
[pkg.go.dev](https://pkg.go.dev/github.com/RECHE23/real-regex/bindings/go).

## Differences & limitations

The one thing to know — **`\w`, `\d`, and `\s` are Unicode-aware by default
here.** `regexp` (RE2) uses ASCII-only by default. `\w+` on `"café"` matches all
of it under this package; under `regexp`, `\w+` matches only `"caf"` (because
RE2's `\w` does not include the accented é).

That is the first of **five flavor differences**, and they share one root: the
API here is `regexp`'s, the engine is Python `re`'s, so where the two flavors
read the same text differently this package follows `re`. Three of the five are
**silent** — both engines compile the pattern and the match differs:

| pattern | `regexp` reads | here |
| --- | --- | --- |
| `a{,2}`, `a{,}` | literal text | `{0,2}` / `{0,}`, Python's shorthand |
| `[[:alpha:]]`, `[[:digit:]]` | a POSIX class | a literal class, as `re` reads it |
| `\<w\>` | an escaped literal `<w>` | word-start / word-end anchors |

The fourth is `\w`/`\d`/`\s` above; the fifth is malformed UTF-8 through the
`[]byte` methods, where `regexp` substitutes U+FFFD and this package matches
nothing. All five are pinned in both directions by the binding's tests.

v0.3-specific: cgo required; supported platforms are macOS-arm64 and
linux-x86-64 only; no flags parameter exposed in the Go API (always compiles
with default flags). Not in this subset: `Expand`.

Full reference — the binding's own
[README](https://github.com/RECHE23/real-regex/blob/main/bindings/go/README.md).

## Comparison

REAL on Go has a **structural advantage** over `regexp` on constructs `regexp`
rejects outright — bounded lookarounds and possessives run in linear time under
REAL where RE2 offers no route at all. A drop-in replacement trades this for
interoperability with the stdlib; migrating existing patterns gains access to
both features.

Go is not part of the shared binding benchmark suite. The engine itself (REAL vs
RE2) is measured in {doc}`Performance <../performance/index>`; the Go binding
adds the cgo call overhead — not the same numbers as the pure-C library.
