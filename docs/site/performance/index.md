---
orphan: true
---

<!--
Footer-only: a direct root-toctree entry would become a fifth header item.
The full ledger is docs/BENCHMARKS.md on GitHub -- this page is the reading,
not a second copy of the tables. Do not {include} that file here.
-->

# Performance

REAL is the linear-time, ReDoS-safe engine that is also fast: ahead of
`std::regex` everywhere and of RE2 on most general shapes, trading blows with
PCRE2-JIT and Rust's `regex` crate, and orders of magnitude ahead of every
backtracker on adversarial (ReDoS) inputs — safety it never trades for the
speed.

## How it compares

| | **REAL** | std::regex | RE2 | Rust `regex` | PCRE2-JIT | Python re |
|---|:---:|:---:|:---:|:---:|:---:|:---:|
| Linear-time, ReDoS-safe | yes | no | yes | yes | no | no |
| Lookarounds | yes | yes | no | no | yes | yes |
| Possessive / atomic | yes¹ | no | no | no | yes | yes |
| Header-only, zero-dep | yes | yes² | no | no | no | — |
| Constexpr match | yes | no | no | no | no | no |
| Drop-in Python `re` | yes³ | no | no | no | no | yes |

¹ Tier 1 (linear time): a single atom, or one wrapped in one capturing group —
`[^x]*+`, `\d++`, `(?>\w+)`. A compound body is rejected, never silently
mis-matched. ² Part of the C++ standard library. ³ For the supported subset
(no backreferences).

Every production engine that ships lookarounds backtracks. Every linear-time
engine drops them. REAL is the only one with both.

## ReDoS

On the classic catastrophic pattern, backtrackers explode or refuse at a few
dozen characters. REAL and RE2 stay linear at a hundred thousand. Two honest
legs: the required-literal prefilter on `(a+)+b`, and the bare VM on `(a+)+`
with nothing to short-circuit. The measured table is §C of the ledger.

## The ledger

Tables, machines and the version stamp live in
[BENCHMARKS.md](https://github.com/RECHE23/real-regex/blob/main/docs/BENCHMARKS.md)
on GitHub — not on this site. Per-train notes live in
[CHANGELOG.md](https://github.com/RECHE23/real-regex/blob/main/CHANGELOG.md).
A re-stamp must not silently rewrite a published page.

What a timing claim is allowed to say:
[MEASUREMENT.md](https://github.com/RECHE23/real-regex/blob/main/docs/MEASUREMENT.md).

Reproduce with `make bench-engines` (C++) and `make python-bench` (binding vs
`re`). Both check match-count equality before timing — a fast wrong answer is
not a win.
