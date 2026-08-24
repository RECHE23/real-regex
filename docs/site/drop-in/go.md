<!--
The Drop-in target page for Go's regexp, on the shared per-target template.
Canon = bindings/go/README.md (the binding's README, always up-to-date).
-->

# Drop-in for Go `regexp`

**v0.1 subset, strict (cgo; macOS-arm64 & linux-x86-64 only).**
The methods below, not the whole `regexp.Regexp` surface. Every accepted pattern is guaranteed linear.
An unsupported construct is rejected at compile time instead of silently backtracking.

## Adopt / swap

```go
import real "github.com/RECHE23/real-regex/bindings/go"

re := real.MustCompile(`\d+`)
re.MatchString("x42")   // true — a search, like regexp.MatchString
```

## API offered

`Compile` / `MustCompile`, `(*Regexp) Close`, `String`, `Match` / `MatchString`,
`Find` / `FindString` / `FindIndex`, `FindAll` / `FindAllString` / `Split`,
`FindAllIndex`, `FindSubmatchIndex` / `FindAllSubmatchIndex`, and `ReplaceAll`.

Beyond `regexp` — flagged extensions, never silent divergences:

- `(*Regexp) FullMatch` — whole-string match; `regexp.MatchString` is really a
  *search*.
- `RegexSet` (`CompileSet`, `IsMatch`, `Matches`, `Size`) — multi-pattern
  which-matched set; `regexp` has no equivalent.
- Bounded lookahead / lookbehind (`(?=…)`, `(?<=…)`, etc.) and possessive
  quantifiers (`a++`); `regexp.Compile` rejects these patterns outright.
- **ReplaceAll template sigil differs** — this package uses REAL/Python-style
  `\1` / `\g<name>`; `regexp` uses `$1` / `${name}` — not translated, document
  your own convention if you need both.

Object-level reference:
[pkg.go.dev](https://pkg.go.dev/github.com/RECHE23/real-regex/bindings/go).

## Differences & limitations

The one thing to know — **`\w`, `\d`, and `\s` are Unicode-aware by default
here.** `regexp` (RE2) uses ASCII-only by default. `\w+` on `"café"` matches all
of it under this package; under `regexp`, `\w+` matches only `"caf"` (because
RE2's `\w` does not include the accented é). This follows REAL's alignment with
Python `re`, not a divergence — both are intentional designs.

v0.1-specific: cgo required; supported platforms are macOS-arm64 and
linux-x86-64 only; no flags parameter exposed in the Go API (always compiles
with default flags). Not in this subset: `FindAllStringSubmatch`, `Expand`,
package-level `MatchString`.

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
