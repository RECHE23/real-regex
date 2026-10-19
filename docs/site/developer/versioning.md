<!--
The versioning and stability policy: what each published surface promises, how a
change is classified, how a break ships. CONTRIBUTING.md's release section links
here instead of restating it.
-->

# Versioning and stability

REAL is calendar-versioned: `YEAR.MONTH.PATCH`, the patch counting releases
within the month. Every surface — the C++ headers, the Python package, the Rust
crate, the C ABI — carries the same version, single-sourced from
`pyproject.toml` and checked by `make version-check`; the Go module carries its
own `v0` line. A calendar version is valid Semantic Versioning, and this policy
makes it mean what SemVer tools read: **the year is the major**. A caret pin
(`^2026.7` in Cargo, `>=2026.7,<2027` in pip) accepts every release of the year
and no break.

## What is covered

A surface is **stable** when this site documents it: the classes, functions
and members in the {doc}`C++ reference <../reference/index>` and
{doc}`Python reference <../reference/python>`, the Rust crate's public items,
the functions of `bindings/c/real_capi.h`, and the Go package's exported names.
Everything else is outside the promise:

- **Internal** — `real::detail`, `real::prof`, and every name the reference
  does not show. They change in any release.
- **Experimental** — public names listed with a reason in
  `docs/site/reference/unpublished.yaml` as experimental (for example
  `basic_regex::find_iter_longest`). A class marked `publish_all` there, such as
  `real::dfa_munch_memo`, is published whole, and stable.
  They may change in any release until they are published; the release notes
  say when one does.

Behaviour is part of the promise where it is documented: which strings a
pattern matches, the spans it reports, the errors it raises and their codes.
A fix that makes REAL agree with its documentation, or with the reference
engine a surface claims to follow (`re` for the Python drop-in, RE2 for
`compat-re2`), is a bug fix, not a break, and the release notes name it.

## Within a year: additions and fixes only

A release inside a year may add names, overloads, flags, error codes and
enum values at the end, and may fix bugs. It does not remove or rename a
stable name, change a signature, narrow what a stable function accepts, or
change documented behaviour.

A name that is to go is **deprecated** first — `[[deprecated("...")]]` in C++,
a `DeprecationWarning` in Python, `#[deprecated]` in Rust, a `// Deprecated:`
paragraph in Go — with the replacement named in the message, and stays for at
least three months. It is removed only in a new year.

## Across a year: how a break ships

Breaking changes are developed on the `next` branch and ship together in the
first release of the following year (`YEAR+1.1.0`), with a migration section in
its notes. `main` keeps releasing the current year from the same engine, so a
break never forces an out-of-season version number, and never reaches a caret
pin. The one exception is a security fix that cannot be made compatibly; it
ships when it is ready, and its notes say what it breaks and why.

## The C ABI

`bindings/c/real_capi.h` is frozen-additive: functions, enum values and flags
are added, never changed or removed. `REAL_ABI_VERSION` (and
`real_abi_version()`, which reports the value the library was built with)
counts incompatible changes, and moves only with a new year; a binding that
links the library rather than compiling it compares the two at load time.
`tests/bindings/capi_abi_golden.txt` pins the surface, and `make
check-abi-bump` refuses a golden that removes or changes a line against the
last release unless `REAL_ABI_VERSION` moved with it.

## The Go module

The module lives at `github.com/RECHE23/real-regex/bindings/go` and is tagged
`bindings/go/v0.MINOR.PATCH`. The patch follows every engine release; the
minor moves when the Go API breaks, which follows the same rule as above. The
module moves to `v1` once its API has held through a year.

## Exceptions so far

v2026.9.7 changed the Rust `RegexSet::matches` return type in a release that
Cargo reads as compatible. That is the break this policy exists to prevent;
the next change of its kind waits for `next`. Two checks now hold it: CI
compares the Rust crate's API on `main` with the last version on crates.io
(`cargo-semver-checks`, the `rust-semver` job), and `make release` judges the
Go module's next version against its API's change since the last tag
(`gorelease`, in `go-release-tag`), refusing a patch that breaks it.

## Release cadence

One planned release a month, its first (`YEAR.MONTH.0`), carrying what the
month brought. Between two, a release goes out for a correctness fix — a wrong
match, a read past an operand, a crash, a broken guarantee — the day it is
ready, carrying `main` as it is; additions and speed-ups wait for the next
planned one. Dependents then follow one version a month, and a fix never waits.

## Correctness record

The day of the last correctness finding — a wrong match, a read or write past
an operand, a crash or a hang — in a released version, and the release that
fixed it. REAL is called mature once six weeks have passed without one, six
weekly long fuzzing campaigns green (`.github/workflows/fuzz-long.yml`) beside
the differential oracles (PCRE2, RE2, Python's `re`, Go's
`regexp`).

| Last finding | What | Fixed in |
| --- | --- | --- |
| 2026-10-15 | a read one byte past the subject, a lost match in bytes mode, a signed overflow in `\U`, found by the long fuzzing campaign | 2026.10.16 |
