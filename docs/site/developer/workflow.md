<!--
Explains the dev workflow: the SciForge boundary and the make-target taxonomy.
Anti-dup: `make help` carries the target list and TESTS.md the test taxonomy --
this page explains the groups and links out, it never recopies.
-->

# Development workflow

The engine itself needs nothing but a C++20 compiler — the library is
header-only and zero-dependency. The workflow below is for building the *test
suite*, the bindings and the docs.

## SciForge, the shared harness

[SciForge](https://github.com/RECHE23/sciforge) is the test/build harness the
stack shares: the test framework compiled into REAL's test translation units,
the reusable CI spine the Python jobs run on, the packaging pins the gates
keep consistent. Its boundary is strict — **it is never a dependency of the
library**. A consumer or packager building the library alone configures with
`-DBUILD_TESTING=OFF` and needs no SciForge at all; the harness exists only
for the test suite, the Python binding's build, and CI.

## The targets, by workflow

`make help` is the canon — every target, grouped. The groups *are* the
workflow:

- **daily** — the edit loop: configure-and-build, run, clean.
- **gates** — the pass/fail checks a push must survive: `version-check`, the
  calibrated `gate-*` family for scoped changes, and `full-local-gate` — the
  complete pre-push record.
- **nets** — the checks that catch what unit tests cannot: every
  `examples/cpp/*.cpp` compiled *and run*, vendored-tree drift, the
  differential harnesses.
- **release** — the install smoke tests (`find_package`, `pkg-config`,
  direct-copy) and `make release`, the codified cut.

A typical session: `make test` while editing, `make full-local-gate` before
any push, `make help` when in doubt — and each binding carries its own
section (`make python-help`, `make rust-help`, …).

## See also

- The exhaustive, grouped target list: `make help` — the canon.
- The test taxonomy and its gates:
  [TESTS.md](https://github.com/RECHE23/real-regex/blob/main/docs/TESTS.md).
- Performance reading: {doc}`Performance <../performance/index>`. Ledger:
  [BENCHMARKS.md](https://github.com/RECHE23/real-regex/blob/main/docs/BENCHMARKS.md).
- Back to the {doc}`Developer hub <index>`.
