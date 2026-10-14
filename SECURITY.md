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

### Linear-time ≠ constant-bounded cost

The linear-time guarantee is **asymptotic**: no pattern REAL accepts drives it into super-linear time on
its input. It does **not** bound the *constant* factor. A pattern that is legally O(n) can still carry a
large constant — and in a multi-tenant setting where an attacker supplies the *pattern* (not just the
input), a legally-linear-but-expensive-per-byte pattern can still saturate a core. That is not a bypass;
it is a cost you must budget for if you accept untrusted patterns.

The legal, non-bypass worst cases, and where each constant comes from (`include/real/core/config.hpp`
unless noted):

- **Lookaround**: the constant grows with `k`, the number of lookarounds in the pattern, and with each one's
  size. A bounded lookahead (length `L`, capped at `max_lookaround_length`, 255 bytes) runs a sub-match at
  every position it is tested, about `O(n · k · L)`. An unbounded lookahead runs one backward pass over the
  whole subject the first time a search tests it, about `O(n · m)` for its `m` instructions, and then reads
  a bit per position — paid in full even when the match is found early, and one bit per byte of memory. A lookbehind runs as one forward walk per search
  that carries every start together, about `O(n · k · m)` with `m` its sub-program's size, which grows with
  `L` but is not squared by it. Measured 2026-09-25 (x86-64, g++ 13.3, `-O2`, one run each):
  `(?<=a{1,L}b)a` over 10 KB of `a` costs 0.83, 1.73 and 3.14 µs per byte for `L` = 50, 100, 200 — 32 ms
  at `L` = 200. The same session read 31.5, 118.5 and 462 µs per byte (4.7 s) before, when the body was
  re-tried from every start within `L` bytes behind the position.
- **Lazy-DFA thrash falling back to the general Pike VM**: the DFA state cache holds up to `state_budget`
  (4096, `include/real/automata/lazy_dfa.hpp`) states before a flush; `thrash_flushes` (2) flushes within
  one scan trip a fallback to the slower general engine for the rest of that scan — still linear, at a
  higher per-byte constant.
- **A compiled program at its size cap**: `max_program_size` (262144 instructions) — reachable via nested
  bounded repeats (`{1000}`-class quantifiers); more instructions per byte scanned.
- **Capture groups at their cap**: `max_group_count` (32766) — `slot_count = 2 * (groups + 1)` capture
  slots copied on every branch (COW-slots), so a pattern with many groups raises the constant behind every
  step of the scan.

**If you accept untrusted patterns** (not just untrusted input — e.g. a multi-tenant service where a
tenant supplies the regex), the recommended posture: stay on `policy::strict` (`real::compat`'s default,
which rejects a pattern REAL cannot run linearly rather than silently degrading); lower the limits of each
pattern you compile to whatever your workload actually needs, well below the library defaults; and bound the
haystack size upstream of the match call. The limits are set per pattern, where it is compiled:

| surface | how | refused as |
| --- | --- | --- |
| C++ | `real::regex(pattern, flags, real::limits {...})`, `real::regex_set(patterns, flags, limits)` | `regex_error`, `kind() == error_kind::limit` |
| C | `real_compile_opts(..., &real_limits {...}, ...)` | `REAL_ERR_LIMIT` |
| Python | `real.compile(pattern, limits=real.Limits(...))` | `real.error`, `kind == "limit"` |
| Rust | `RegexBuilder::new(p).size_limit(bytes).dfa_size_limit(bytes)` | `Error::CompiledTooBig` |
| Go | `real.CompileWith(pattern, real.Options{...})` | an error wrapping `real.ErrLimit` |

The limits are the program's instructions, a bounded repeat's count, the capture groups, the nesting, a
lookaround's length, and the bytes the search's lazy DFA may cache (past them the search finishes on the VM, still
linear). A per-pattern limit may lower the build's, never raise it; the build's ceilings are the macros
`REAL_MAX_PROGRAM_SIZE`, `REAL_MAX_REPEAT_COUNT`, `REAL_MAX_GROUP_COUNT`, `REAL_MAX_NESTING_DEPTH`,
`REAL_MAX_LOOKAROUND_LENGTH` and `REAL_MAX_DFA_STATES` (`-DREAL_MAX_REPEAT_COUNT=100`; a value above the default is
refused at compile time, and every translation unit of a program must see the same values —
`include/real/core/config.hpp`). No fallback policy delegates a pattern past a limit to another engine. There is no
runtime kill-switch for match-time cost (`prefilter_work_units` exists only as a test instrument): match time is
linear in the haystack, which is the size to bound.

**The distinction that matters for a report**: a pattern and input that drive REAL *super-linear* is a
bypass — report it through the channel above. A pattern that is linear but expensive on a hostile input
it was never rejected for is not a bypass; it is a case to bound through the configuration above, in a
deployment that accepts untrusted patterns. That is a deployment responsibility, not an engine defect —
said here for the same reason the benchmarks document where PCRE2-JIT beats REAL on raw throughput: this
policy states what the guarantee covers, not more.

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
