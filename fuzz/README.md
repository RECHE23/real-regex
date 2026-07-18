# REAL — fuzz

What lives here, and how to run it. `make help` (from the repo root or `make -C fuzz help`)
lists every target with a one-line description; this file is the map, not a second copy of
the methodology — that lives in [docs/TESTS.md](../docs/TESTS.md).

| | |
|---|---|
| **Robustness** (`fuzz`) | libFuzzer + ASan/UBSan over the engine's public entry point (`fuzz_target.cpp`) — malformed patterns/inputs must never crash, leak, or UB. Clang-only (libFuzzer). |
| **Compat differential** (`fuzz-compat`) | `real::compat` vs libstdc++'s `std::regex` (search/replace/iterate/token/match-flags) — the net that has caught every silent divergence in the compat layer; runs in CI. |
| **RE2 differential** (`fuzz-re2`) | `real::compat::re2` (drop-in) vs true libre2, the oracle. Curated harness with an internal can-fail proof (`REAL_RE2_DIFF_CANFAIL=1`). Needs `pkg-config re2`; skips cleanly when absent. |
| **Exhaustive compat routing** (`exhaustive-compat`) | Every pattern up to *k* constructs over a tiny alphabet, crossed with every short input (`sciforge.corpus.exhaustive`) — `real::compat` vs the local `std::regex`, tuned by `EC_K`/`EC_N`. |
| **Fowler/AT&T conformance** (`fowler-compat`) | The three vendored `testregex` corpora (`tests/corpora/fowler`) through `real::compat` vs the local `std`, three-way-arbitrated against the corpus's own POSIX expectation. |

`fuzz`/`fuzz-compat`/`fuzz-re2` are the ongoing, time-bounded fuzzers (`FUZZ_TIME=secs`, default
30s locally, CI runs 60s); `exhaustive-compat`/`fowler-compat` are fast, deterministic nets — the
ones that turned up every silent divergence the campaign found, so both run in `full-local-gate`
and CI. `tsan`/`tsan-core` (ThreadSanitizer smokes) are thematically robustness nets too, but stay
at the repo root — their sources live in `tests/`.

## Running

    make help                  # every target here, one line each
    make fuzz FUZZ_TIME=60     # libFuzzer robustness, 60s
    make fuzz-compat           # compat vs std::regex differential
    make fuzz-re2              # compat::re2 vs libre2 differential (needs pkg-config re2)
    make exhaustive-compat     # exhaustive small-space routing check
    make fowler-compat         # Fowler/AT&T POSIX conformance

Each also runs directly from this directory: `make -C fuzz <target>` from the repo root, or
`make <target>` from inside `fuzz/`.

See [docs/TESTS.md](../docs/TESTS.md) for the full testing picture (unit tests, the three
differential fuzzers, the exhaustive enumeration, and the public conformance suites) and what
each one has caught.
