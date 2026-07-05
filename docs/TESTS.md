# Testing

REAL is tested four ways, each catching what the others cannot:

- **Unit + parity tests** (`tests/`, `bindings/python/tests/`) — behaviour pinned directly, and a differential
  corpus run against Python `re` across every API.
- **Three differential fuzzers**, one per external engine REAL is measured against:
  - vs Python **`re`** — `bindings/python/tests/test_differential_fuzz.py`, thousands of random patterns
    (nullable loops, scoped flags) compared to `re`.
  - vs **`std::regex`** — `fuzz/fuzz_compat.cpp` (libFuzzer + ASan/UBSan), the `real::compat` drop-in against
    libstdc++'s `std::regex`; the net that has caught every silent compat divergence.
  - vs the Rust **`regex`** crate — `bindings/rust/fuzz/` (cargo-fuzz, nightly), the `real-regex` crate against
    `regex` on spans / captures / replace / split. The C ABI shim has its own robustness fuzzer
    (`fuzz/fuzz_capi.cpp`).
- **An exhaustive small-space enumeration** (`sciforge.corpus.exhaustive`) — every pattern up to *k*
  constructs over a tiny alphabet crossed with every short input, differential against `re`.
- **Public conformance suites** — the vendored rust-regex and Fowler/AT&T corpora, below.

## Public conformance suites

The corpora under `tests/corpora/` are vendored verbatim from public test suites (SHA-pinned; licences
in `tests/corpora/PROVENANCE.md`) and run through the SciForge corpus contract (`../sciforge/docs/corpus.md`)
by `tests/corpora/run_conformance.py` — REAL as the engine, Python `re` as the oracle. Every case
resolves to exactly one status:

- **pass** — REAL matches `re`.
- **intentional_divergence** — a documented, on-purpose difference (carries its `divergences.dox` link).
- **out_of_contract** — the corpus's origin uses different match-selection *semantics* than our oracle
  (a leftmost-longest / POSIX row against a leftmost-first oracle); set aside, not scored.
- **filtered** — the test uses an API REAL does not offer (anchored / sub-region / overlapping search,
  a raw-byte or non-UTF-8 haystack); filtered at import and counted.
- **bug** — an unexplained disagreement with `re`.

**Result — 762 / 762 in-contract cases pass (100%), 0 bugs.**

| Corpus | In-contract pass | intentional_divergence | out_of_contract | filtered |
| --- | --- | --- | --- | --- |
| rust/flags | 9 / 9 | — | — | 2 |
| rust/multiline | 137 / 137 | — | — | 3 |
| rust/unicode | 84 / 84 | — | — | — |
| rust/word-boundary | 56 / 56 | — | — | 47 |
| rust/word-boundary-special | 80 / 80 | — | — | 12 |
| rust/iter | 19 / 19 | — | — | 3 |
| rust/misc | 13 / 13 | — | — | 3 |
| rust/regression | 55 / 55 | — | — | 31 |
| rust/no-unicode | 12 / 12 | — | — | 11 |
| rust/crlf | 14 / 14 | — | — | 1 |
| rust/empty | 19 / 19 | — | — | — |
| rust/utf8 | 1 / 1 | — | — | 27 |
| rust/anchored · substring · bytes | — | — | — | 42 (all API-out) |
| fowler/basic | 180 / 180 | — | 30 | — |
| fowler/nullsubexpr | 49 / 49 | 4 | 5 | — |
| fowler/repetition | 53 / 53 | — | 38 | — |

The `out_of_contract` count on the Fowler corpora is expected: those are AT&T **POSIX** (leftmost-longest)
tests, and REAL, like `re`, is leftmost-first — the disagreeing rows are quarantined by the manifest's
`semantics` field, not scored as failures. The four `intentional_divergence` rows in `nullsubexpr` are
the nullable-loop final-iteration capture (the `div_empty_iteration_capture` divergences section) (RE2 / Rust / Go lineage).

**Found and fixed by this net:** the nullable-loop leftmost-first bug — a greedy `*` over an empty-first
alternation branch (`(?:|a)*`) matched greedily where leftmost-first requires preferring the empty
branch. The rust `empty.toml` corpus surfaced it; it is fixed, and this suite is the regression guard.

Run it: `python3 tests/corpora/run_conformance.py` (needs `../sciforge/python` on the path).
