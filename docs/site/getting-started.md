<!--
First-hour page. Nested under Features so the header stays four items
(contents.md). Snippets are the same CI-tested regions the landing injects --
not a second copy.
-->

# Getting started

Install, then a first match. The {doc}`drop-in pages <drop-in/index>` cover
swapping an API you already use.

## Install

| Channel | Command |
| --- | --- |
| Homebrew (macOS / Linux) | `brew install RECHE23/sci/real-regex` |
| PyPI | `pip install real-regex` |
| crates.io | `cargo add real-regex` |
| Go | `go get github.com/RECHE23/real-regex/bindings/go` |
| vcpkg | [`vcpkg-sci`](https://github.com/RECHE23/vcpkg-sci) → `"dependencies": ["real-regex"]` |
| CMake FetchContent | `GIT_REPOSITORY https://github.com/RECHE23/real-regex` · `GIT_TAG` a release |
| Vendored | copy `include/` and compile `-std=c++20 -I include` |

The C++ library is header-only: installing places the headers. **C++20** is
required; every header asserts it. Consume with CMake
(`find_package(real CONFIG)` + `real::real`), `pkg-config --cflags real`, or
a plain `-I`. The [SciForge](https://github.com/RECHE23/sciforge) harness is
for the test suite, never for the library.

## First match

The same snippets the landing shows — compiled and run by CI, not illustrations.

::::{tab-set}

:::{tab-item} C++
```{literalinclude} ../../examples/cpp/quickstart.cpp
:language: cpp
:start-after: "// [quickstart]"
:end-before: "// [/quickstart]"
```
:::

:::{tab-item} Python
```{literalinclude} ../../bindings/python/examples/quickstart.py
:language: python
:start-after: "# [quickstart]"
:end-before: "# [/quickstart]"
```
:::

:::{tab-item} Rust
```{literalinclude} ../../bindings/rust/examples/quickstart.rs
:language: rust
:start-after: "// [quickstart]"
:end-before: "// [/quickstart]"
```
:::

:::{tab-item} Go
```{literalinclude} ../../bindings/go/quickstart_example_test.go
:language: go
:start-after: "// [quickstart-import]"
:end-before: "// [/quickstart-import]"
```
```{literalinclude} ../../bindings/go/quickstart_example_test.go
:language: go
:start-after: "// [quickstart-body]"
:end-before: "// [/quickstart-body]"
```
:::

::::

## Next

- {doc}`Drop-in <drop-in/index>` — swap `std::regex`, RE2, Python `re`, the
  Rust `regex` crate, or Go `regexp`.
- {doc}`Features <features>` — every construct the engine accepts or refuses.
- {doc}`How it compares <performance/index>` — the capability picture; the
  measured ledger stays on GitHub.
- {doc}`API reference <reference/index>` — curated per-object pages.
