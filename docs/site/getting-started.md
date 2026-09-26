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

### Compile the engine once

Every translation unit that includes `<real/real.hpp>` compiles the engine
and its Unicode tables: several seconds per file at `-O2`. A project with
many such files can build the engine once instead, as a library, and include
a header that reaches none of it:

```cmake
# configure REAL with -DREAL_BUILD_CAPI=ON, then:
find_package(real CONFIG REQUIRED)
target_link_libraries(app PRIVATE real::capi)
```

```cpp
#include <real_compiled.hpp>

const real::compiled::regex re {R"((\w+)@(\w+))"};
if (const auto m {re.search("mail bob@host now")}) {
  std::string_view user {m.str(1)};   // "bob"
}
```

A file that includes it compiles in under a second. `real::compiled` has
`search`, `match` and `fullmatch` over a region, `find_all`, `count`, `sub`
and group names; results own their spans, so they outlive the regex. What it
gives up is the header-only engine's: `static_regex`, inlining into the
caller, the compatibility layers. The library is static or shared
(`BUILD_SHARED_LIBS`), and `real_capi.h` beside it is the same interface in C.

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
