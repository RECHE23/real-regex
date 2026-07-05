# Contributing to REAL

Thanks for your interest. REAL is a header-only C++20 regex engine with an abi3 Python binding; a few
conventions keep it consistent and safe to change.

## Building and testing

The library itself needs only a C++20 compiler. The test suite, the binding and the lint/doc tooling need
[SciForge](https://github.com/RECHE23/sciforge) checked out **as a sibling** (the shared test harness, lint
config and benchmark substrate):

```
Projects/
  real-regex/      <- here
  sciforge/        <- git clone https://github.com/RECHE23/sciforge
```

Then, from `real-regex/`:

```sh
make full-local-gate    # the gate of record: format, layering, tests (clang + g++), sanitizers, MISRA,
                        # lint, doc + strict-Doxygen doc-check, the Python suites, coverage floor
```

Run it before every push. Individual steps (`make test`, `make lint`, `make coverage`, `make doc`) are in
`make help`.

## Conventions that the gate enforces

- **Header layering.** `include/real/` is partitioned into dependency tiers (`core` < `unicode` < the
  `engine`/`automata` runtime < `frontend` < the public root); a header may include only from its own tier
  or a lower one. `tools/check_layers.py` (the `check-layers` gate) fails the build on any upward include.
  See [docs/design.dox §8](docs/design.dox).
- **No libc++ `std::hash` in shipped headers.** The engine headers use no `std::hash` and no
  `std::unordered_map` / `set`: their out-of-line `__hash_memory` symbol (LLVM 19+) fails to resolve against
  an older runtime libc++, a toolchain drift invisible on the machine that built it. Use an in-house FNV over
  bucket-vectors, or a sorted vector, instead. Enforced by the `symbol-hygiene` check where applicable.
- **32-bit safety.** The x64 MSVC CI cannot see a `size_t` narrowing (its `size_t` is 64-bit); the
  `win32-narrowing` CI job compiles the engine headers `-m32` so it does. Accumulate hashes in a fixed 64-bit
  width and truncate at the boundary.
- **Public API is stable.** `<real/real.hpp>` and the opt-in `<real/dfa.hpp>` are the only supported include
  paths; internal headers say so at the top and may move.

## Releases

Calendar-versioned (`YEAR.MONTH.patch`). The GitHub releases page is the changelog — there is no separate
`CHANGELOG` file. Releases push the branch first, let CI go green, then tag (option-B): the tag's annotation
becomes the release notes, and the tagged commit is what CI already validated.

The version is single-sourced from `pyproject.toml` and `make version-check` asserts every surface agrees:
`python/real/__init__.py`, the CMake-derived C++ `version.hpp`, and the Rust crate's `bindings/rust/Cargo.toml`.

**The Rust crate (`real-regex` on crates.io).** It carries the same CalVer — the CalVer is valid SemVer, so
the crate's first publish is the current calendar version, not a `0.0.1` placeholder. Two consequences to keep
in mind:

- **crates.io is immutable.** A published version can only be *yanked* (hidden from new resolves), never
  deleted or overwritten. So a crate publish happens only *after* the matching release is live and verified,
  by hand, once the tag has shipped. Never publish a version you have not already released. The flow, with the
  dry-run as its gate:

  ```
  make crate-publish-check          # vendors include/ + bindings/c into the crate, then cargo publish
                                     # --dry-run COMPILES the packaged tarball — the real gate
  cd bindings/rust && cargo publish  # token in ~/.cargo/credentials.toml
  ```

  The dry-run is load-bearing: `cargo package --list` shows the vendored files but does not build them, so
  only `--dry-run` proves the crate compiles standalone (nothing off crates.io can reach `../../include`). The
  engine sources are vendored into `bindings/rust/vendor/` (git-ignored; carried into the package by the
  `include = [… "vendor/**"]` whitelist in `Cargo.toml`) — regenerate them with `make crate-vendor`, never
  edit them by hand.
- **A breaking change mid-year bumps the year (the major).** SemVer consumers pin with a caret (`^2026.7`),
  which treats a minor/patch bump as non-breaking. REAL grows without breaking by principle; if a genuinely
  breaking change ever ships mid-year, bump the *year* so the caret protects downstream, rather than slipping
  a break through a minor bump.
