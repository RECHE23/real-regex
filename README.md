# REAL

[![CI](https://github.com/RECHE23/real-regex/actions/workflows/ci.yml/badge.svg)](https://github.com/RECHE23/real-regex/actions/workflows/ci.yml)
[![PyPI](https://img.shields.io/pypi/v/real-regex)](https://pypi.org/project/real-regex/)
[![release](https://img.shields.io/github/v/release/RECHE23/real-regex)](https://github.com/RECHE23/real-regex/releases)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue)](https://en.cppreference.com/w/cpp/20)
[![header-only](https://img.shields.io/badge/header--only-yes-green)](https://github.com/RECHE23/real-regex/tree/main/include/real)
[![coverage](https://img.shields.io/badge/coverage-%E2%89%A595%25-brightgreen)](https://reche23.github.io/real-regex/coverage/index.html)
[![license](https://img.shields.io/badge/license-MIT-blue)](https://github.com/RECHE23/real-regex/blob/main/LICENSE)

Linear-time, ReDoS-safe C++20 regex with bounded lookarounds — RE2's safety plus the
lookarounds RE2 can't do — and a drop-in `re`-compatible Python binding.

**Regular Expression Algorithmic Library** — a header-only C++20 regex engine, constexpr
from end to end, with an `re`-compatible Python binding.

- **Linear time, always.** The engine is a Pike VM (Thompson NFA simulation):
  no backtracking, ReDoS-safe by construction.
- **Constexpr-friendly.** Patterns known at compile time are parsed, compiled
  and matched at compile time.
- **Minimal memory.** Static (sizes fixed at compile time, zero allocation),
  dynamic (storage sized exactly once at pattern compilation), or hybrid
  (compile-time pattern, runtime text, zero heap allocation).
- **Zero dependencies.** One include.

## The problem

Backtracking engines — PCRE, `std::regex`, Python `re` — are vulnerable to **ReDoS**: a
pattern like `(a+)+b` takes exponential time on a hostile input. The linear-time engines
that fix this — **RE2**, Rust's `regex` — buy safety by **dropping lookarounds** entirely.

REAL gives you **both**: linear-time, ReDoS-safe matching *with* bounded lookarounds.

## How it compares

| | **REAL** | std::regex | RE2 | Rust `regex` | PCRE2-JIT | Python re |
|---|:---:|:---:|:---:|:---:|:---:|:---:|
| Linear-time, ReDoS-safe       | ✅ | ❌ | ✅ | ✅ | ❌ | ❌ |
| Lookarounds                   | ✅ | ✅ | ❌ | ❌ | ✅ | ✅ |
| Possessive quantifiers / atomic groups | ✅³ | ❌ | ❌ | ❌ | ✅ | ✅ |
| Header-only, zero-dependency  | ✅ | ✅¹ | ❌ | ❌ | ❌ | — |
| Constexpr (compile-time match)| ✅ | ❌ | ❌ | ❌ | ❌ | ❌ |
| Drop-in Python `re`           | ✅² | ❌ | ❌ | ❌ | ❌ | ✅ |

¹ part of the C++ standard library. ² for the supported subset (no backreferences, etc.).
³ Tier 1 (linear time): a single atom, or one wrapped in one capturing group — `[^x]*+`,
`\d++`, `(?>\w+)` — covers the dominant real-world shape; a compound body is rejected,
never silently mis-matched.

**Every production engine that ships lookarounds backtracks** (PCRE2,
`std::regex`, Python `re` — ReDoS-unsafe), and every linear-time engine drops
them (RE2, Go `regexp`, the Rust `regex` crate) — **REAL is the only one with
both**. Throughput, machines and the per-engine duels:
[Performance](https://reche23.github.io/real-regex/performance/).

## ReDoS, in numbers

On the classic catastrophic pattern `(a+)+b`, REAL's required-literal prefilter
answers in **~2 µs over 100 000 characters** (no `b` → reject without running the
VM). Strip the trailing literal: the bare engine on `(a+)+` still finishes in
**~5 ms on 100K and stays linear** (~50 ms at 1M) — while `std::regex` takes
**4.1 s on 26 characters** and Python `re` **~1.4 s on 24, climbing exponentially**.
Linear time is the measured property, not an adjective. Method and both legs:
[Performance](https://reche23.github.io/real-regex/performance/) (§C); prefilter
leg also in `make bench-engines` (`redos`).

## Quickstart

**Python** — `pip install real-regex`, drop-in for the supported `re` subset:

```python
import real as re                  # drop-in for the supported re subset
re.search(r"\d+", "x42")           # -> a Match; findall / finditer / sub / split too
```

**C++** — header-only, C++20:

```cmake
find_package(real CONFIG REQUIRED)
target_link_libraries(app PRIVATE real::real)
```

```cpp
#include <real/real.hpp>
real::regex re("[0-9]+");
re.search("x42").matched();        // true
```

More runnable programs — including the ReDoS demo — are in [`examples/`](https://github.com/RECHE23/real-regex/tree/main/examples).

## Installation

| Channel | Command |
| --- | --- |
| PyPI (Python + headers) | `pip install real-regex` |
| crates.io (Rust) | `cargo add real-regex` |
| Go | `go get github.com/RECHE23/real-regex/bindings/go` |
| Homebrew (macOS / Linux) | `brew install RECHE23/sci/real-regex` — the [`homebrew-sci`](https://github.com/RECHE23/homebrew-sci) tap |
| vcpkg | via the [`vcpkg-sci`](https://github.com/RECHE23/vcpkg-sci) registry → `"dependencies": ["real-regex"]` |
| CMake FetchContent | `FetchContent_Declare(real GIT_REPOSITORY https://github.com/RECHE23/real-regex GIT_TAG v2026.8.5)` |
| Vendored | copy `include/` and compile with `-std=c++20 -I include` |

REAL is header-only: installing just places the headers and package metadata.
**C++20 or newer is required** and the consumer passes it (`-std=c++20`) — every
header asserts it with a clear message. Consume via CMake
(`find_package(real CONFIG)` + `real::real`), `pkg-config --cflags real`, or a
plain vendored `-I include`; `add_subdirectory`/`FetchContent` work without
installing. The [SciForge](https://github.com/RECHE23/sciforge) harness is needed
only for the test suite and CI, never for the library — packagers configure with
`-DBUILD_TESTING=OFF`.

## Documentation

The documentation site: <https://reche23.github.io/real-regex/>

- **[Features](https://reche23.github.io/real-regex/features.html)** — the
  capability scorecard, statuses probed in CI.
- **[Drop-in](https://reche23.github.io/real-regex/drop-in/)** — migration
  guides: `std::regex`, RE2, Python `re`, Rust `regex`, Go `regexp`.
- **[API reference](https://reche23.github.io/real-regex/reference/)** — curated
  per-object pages, C++ and Python, rendered from the code's own comments.
- **[Performance](https://reche23.github.io/real-regex/performance/)** — the
  measured baseline: machines, versions and methodology disclosed.
- **[Developer](https://reche23.github.io/real-regex/developer/)** — the
  architecture tour, coverage, testing — and the exhaustive
  [Doxygen tree](https://reche23.github.io/real-regex/api/).

## Supported syntax

REAL accepts the `re` subset you know, plus its differentiators: **bounded
lookarounds** (lookahead and variable-width lookbehind, in linear time),
**possessive quantifiers and atomic groups**, and the **`\p{…}`
Unicode-property superset**. Backreferences and conditional groups are
rejected up front with `real::regex_error` — never a silent divergence.

Full pattern-syntax reference:
<https://reche23.github.io/real-regex/reference/syntax.html>

## Development

```bash
make help             # list all targets
make test             # build and run the test suite
make coverage         # line coverage report (LLVM)
make full-local-gate  # every gate, pre-push record
```

Every behaviour is tested at runtime and in constexpr (`static_assert`) under
Clang and GCC; a parity suite and a randomized differential fuzzer compare the
Python binding against `re`; CI covers Linux (x86-64, AArch64), macOS (arm64)
and Windows (MSVC). REAL holds a mid-90s line-coverage bar on `include/` — a
deliberate, documented exception to the 100%-four-dimension gate of the stack
built on top of it (dual runtime/constexpr execution leaves the last branches
impractical to drive). The full tour, gates and taxonomy:
[Developer](https://reche23.github.io/real-regex/developer/).

**Releasing.** `make release` computes the next calendar version
(`YYYY.M.PATCH`), bumps it, commits, tags and pushes; the pushed tag is the
single trigger for `release.yml`, which builds the abi3 wheels and the sdist and
publishes to PyPI via Trusted Publishing (OIDC, no stored secret).

## License

MIT — Copyright (c) 2026 René Chenard

## Author

René Chenard
