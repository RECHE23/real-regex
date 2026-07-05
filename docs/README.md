# REAL — documentation

The reference docs for [REAL](../README.md), a linear-time, ReDoS-safe C++20 regex engine with bounded
lookarounds and an `re`-compatible Python binding.

## Where things are

| | |
|---|---|
| **[Performance](BENCHMARKS.md)** | The measured baseline vs `std::regex` / PCRE2 / RE2, the ReDoS numbers, and the one-pass-arc duel against the rust `regex` crate. |
| **[Compatibility](COMPATIBILITY.md)** | The supported `re` / `std::regex` subset and every intentional divergence. The `real::compat` drop-in tour is [std_regex_dropin.dox](std_regex_dropin.dox); the `re` divergences are [divergences.dox](divergences.dox). |
| **[Tests & conformance](TESTS.md)** | The differential harnesses, the coverage floor, and the gate policy. |
| **[MISRA](MISRA.md)** | The MISRA C++ posture and its documented deviations. |
| **Architecture & API** | The Doxygen site — [How REAL Works](design.dox) plus the full API reference: <https://reche23.github.io/real-regex/> |

## The engine, in one glance

The headers under `include/real/` are partitioned into dependency tiers; `tools/check_layers.py` (the
`check-layers` gate) forbids any upward include, so the layering is enforced, not aspirational.

```
core/       program.hpp, charclass.hpp, config.hpp          the IR and primitives
unicode/    utf8.hpp, unicode_props.hpp, unicode_fold.hpp   decode + the generated tables (use the IR)
engine/  +  pike.hpp, prefilter.hpp, assert_eval.hpp        the runtime — one tier (pike -> onepass,
automata/   lazy_dfa.hpp, onepass.hpp, utf8_ranges.hpp        onepass -> assert_eval within it)
frontend/   ast.hpp, compiler.hpp                           the parser/compiler (they use the runtime)
real/       real.hpp, dfa.hpp, version.hpp, storage.hpp     the public API + the assembly it drives
std/        regex.hpp (+ its parts)                        the std::regex-compatibility drop-in (real::compat)
```

The full tier contract is [design.dox §8](design.dox). The public include paths — `<real/real.hpp>` and the
opt-in `<real/dfa.hpp>` — are stable; the rest are internal.

## Repository map

```
include/real/   the header-only engine, partitioned into dependency tiers (see above)
bindings/       language bindings — c/ (the C ABI shim), python/ (the abi3 binding + PyPI page), rust/ (the real-regex crate)
tests/          the C++ suite, mirroring the engine tiers ({core,unicode,engine,automata,frontend,compat}/)
fuzz/           the libFuzzer harnesses (robustness, and the real::compat-vs-std differential)
tools/          dev tooling: the codegen (Unicode tables), check_layers.py (the layering gate)
benchmarks/     the throughput/ReDoS benches and the REAL-vs-rust duel (duel/)
cmake/          the installed CMake package config (find_package(real) support)
docs/           this documentation (design.dox, BENCHMARKS, COMPATIBILITY, TESTS, MISRA, release-notes/)
examples/       small runnable programs, including the ReDoS demo
.github/        CI and release workflows
```

## Contributing & releases

See [CONTRIBUTING.md](../CONTRIBUTING.md) for the build, the sibling checkouts and the gate. The GitHub
releases page is the changelog.
