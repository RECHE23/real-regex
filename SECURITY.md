# Security Policy

## Supported versions

REAL ships under CalVer (`YYYY.M.PATCH`). Only the **most recent release** receives
fixes; older releases do not.

| Version | Supported |
| --- | --- |
| latest release | ✅ |
| older releases | ❌ |

## Reporting a vulnerability

Please report security issues privately through GitHub's
**[Report a vulnerability](https://github.com/RECHE23/real-regex/security/advisories/new)**
(private vulnerability reporting). Do **not** open a public issue for a security report.

### A ReDoS bypass *is* a vulnerability

REAL's core guarantee is **linear-time matching**: no pattern and input should drive it
into super-linear — let alone exponential — time. If you find a pattern and an input for
which REAL is **not** linear (a bypass of the ReDoS-safety guarantee), treat it as a
security vulnerability and report it through the channel above, with the pattern, the
input (or a generator for it), and the observed scaling. That is exactly the kind of
report this policy exists to receive.

**One nuance: the fallback paths.** `real::compat` (C++) and the Rust crate's `fallback` feature can, by
explicit opt-in, delegate a pattern REAL cannot run linearly to `std::regex` / the `regex` crate. On those
delegated patterns the linear-time guarantee is *deliberately forfeited* and documented — so a super-linear
time there is **not** a REAL bypass, it is the opt-in you asked for (and observable via `uses_fallback()` /
`engine()`). A ReDoS report is about REAL's *own* engine running non-linearly, or about a pattern being
delegated when you did **not** opt in.

## The three published surfaces

REAL ships as three artifacts, all built from one engine. Reports are welcome on any of them:

- **The header-only C++ library** (`include/real/`) — the engine itself; a memory-safety issue or a
  linear-time bypass here is the core case above.
- **The abi3 Python wheel** (`real-regex` on PyPI) — the CPython binding. A crash, a memory error, or a
  divergence that breaks the ReDoS guarantee through the Python surface.
- **The `real-regex` Rust crate** (crates.io) — a safe wrapper over the C ABI shim (`bindings/c`). The shim
  is the raw-pointer boundary; it is fuzzed under ASan/UBSan (`make c-fuzz`) and run under the sanitizers,
  but a soundness hole in the safe API (a use-after-free, a boundary the wrapper does not uphold) is in scope.

The engine, the shim and the bindings share a version; a fix ships to all affected surfaces in the next
CalVer release.
