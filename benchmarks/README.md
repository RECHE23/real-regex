# REAL — benchmarks

What lives here, and how to run it. `make help` (from the repo root or `make -C benchmarks help`)
lists every target with a one-line description; this file is the map, not a second copy of the
numbers or the methodology — both live in [docs/BENCHMARKS.md](../docs/BENCHMARKS.md).

| | |
|---|---|
| **A/B engine throughput** (`bench-engines`) | REAL vs `std::regex` / PCRE2 / RE2, C++ side by side (`bench_engines.cpp` measures, `bench_engines.py` reports: median ns/byte, ratio-vs-REAL with a bootstrap CI, ASCII box-plots). PCRE2/RE2 compile in only when `pkg-config` finds them. |
| **Multi-pattern** (`bench-multipattern`) | Which-matched + extraction-count throughput (`mp_bench.cpp`); RE2/Hyperscan optional. |
| **The rust duel** (`bench-duel`) | REAL vs the `regex` crate, ns/byte with match counts cross-checked, non-cherry-picked (`duel/`; needs a Rust toolchain to build `duel/rust_bench`). |
| **4-D veto matrix** (`bench-matrix`, `matrix-gate`) | The one *gated* target here — `matrix4d/` sweeps pattern × size × match/no-match × density for the inner-literal route and exits non-zero on a red cell. `bench-matrix` is the full sweep; `matrix-gate` is the fast 64 KB subset `full-local-gate` runs on every push. |
| **Profiling** (`profile-sample`, `profile-callgrind`) | The P0 substrate (`profile/`): a clean-vs-instrumented 2-pass grid (JSONL + markdown) and, where `valgrind` is available, callgrind runs over the same cells. |

None of these except `matrix-gate` are CI gates — they measure wall time, which is host-noise;
see docs/BENCHMARKS.md's own "Not gated" note for why that is deliberate, not an oversight.

## Running

    make help                # every target here, one line each
    make bench-engines        # A/B engine throughput
    make bench-duel            # REAL vs rust `regex`
    make bench-matrix          # full 4-D veto sweep (matrix-gate is the fast subset, run by full-local-gate)
    make profile-sample        # P0 profile grid

Each also runs directly from this directory: `make -C benchmarks <target>` from the repo root, or
`make <target>` from inside `benchmarks/`.

## Methodology

The numbers, the honesty rules (equality-checked before timing, same-machine regression framing,
never a marketing claim), and how to reproduce each table live in
[docs/BENCHMARKS.md](../docs/BENCHMARKS.md) — this README does not duplicate them.
