# Go module v0.2.0 — `FindAllIndex` and `FindAllSubmatchIndex` take `n`

**BROUILLON — le tag `bindings/go/v0.2.0` est posé à la main, pas par `make release`.**

This is a **breaking change to the Go binding only**. Nothing in the engine, the Python binding, the
Rust crate or the C ABI moves with it. It is cut as its own module tag so that the break carries its
own version number and its own note, rather than arriving inside an engine release about something
else.

## What changed

```go
// before
func (r *Regexp) FindAllIndex(text []byte) [][]int
func (r *Regexp) FindAllSubmatchIndex(text []byte) [][]int

// after — the signatures `regexp` has
func (r *Regexp) FindAllIndex(b []byte, n int) [][]int
func (r *Regexp) FindAllSubmatchIndex(b []byte, n int) [][]int
```

`n` means what it means everywhere else in `regexp`: **0 → nil, <0 → all, >0 → at most n**.

Six of the eight `FindAll*` methods already took it — `FindAll`, `FindAllString`,
`FindAllStringIndex`, `FindAllSubmatch`, `FindAllStringSubmatch`, `FindAllStringSubmatchIndex`.
These two did not, so a caller porting from `regexp` hit a compile error on two methods out of eight
and had to remember which. The asymmetry was the whole defect.

**Fixing your code is mechanical**: add `, -1` to every call. That is exactly the old behaviour —
every match, no cap.

## The cap counts filtered matches, and stops the scan

Two properties worth stating, because both are observable:

`n` counts matches **after** the empty-match rule, not before. The enumeration drops an empty match
that abuts the preceding one (this is `regexp`'s rule, adopted in the engine release before this
one), and the cap applies to what survives. `FindAllIndex([]byte("axbxc"), 2)` on `x*` returns the
first two of `[0,0] [1,2] [3,4] [5,5]`, not the first two of the unfiltered six.

And the scan **stops** at `n` rather than enumerating everything and slicing. On a large subject with
a small `n` that is the difference between reading the whole input and reading a prefix of it.

## Two private helpers are gone

`findAllIndexN` and `findAllSubmatchIndexN` existed only to carry the `n` the public methods lacked.
Every other `FindAll*` went through them. With the parameter where it belongs, the six other methods
call the public ones directly and the two helpers are deleted — the change removes code rather than
adding it.

## Why `v0.2.0` and not `/v2`

Go's module rules allow a breaking change inside `v0.x` without an import-path change: the
compatibility promise starts at `v1`. The import path stays
`github.com/RECHE23/real-regex/bindings/go`, and `go get -u` moves a caller onto it — which is why
the *minor* bump matters. Shipping this as `v0.1.31` would have delivered a compile break as a patch
upgrade.

That was a real risk, not a hypothetical one: `make release` hardcoded `bindings/go/v0.1.<n+1>`, so
the first engine release after this change would have tagged the broken API as a patch. It now reads
the minor from the newest Go tag instead of assuming it, and refuses rather than guessing if it
cannot parse one. A minor bump stays a human decision — tag `bindings/go/v0.<m>.0` by hand once, and
every later release follows from there.

## Verified

Both methods are compared against `regexp` itself, over six pattern/subject pairs and six values of
`n` (`-1, 0, 1, 2, 3, 99`), including the shapes where the cap and the empty-match rule interact:
`x*` on `"axbxc"`, the empty pattern, a pattern with groups, and a pattern with no match at all.
