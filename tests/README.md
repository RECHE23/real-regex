# REAL — tests

What lives here, and how to run it. `make help` (from the repo root or `make -C tests help`)
lists every target with a one-line description; this file is the map, not a second copy of the
methodology — that lives in [docs/TESTS.md](../docs/TESTS.md).

| | |
|---|---|
| **Suite** (`test`) | The C++ ctest suite, mirroring the engine tiers (`core/`, `unicode/`, `engine/`, `automata/`, `frontend/`, `compat/`, `bindings/`) plus the vendored conformance corpora (`corpora/`). `build` (the CMake configure+compile) stays a repo-root target — `test` builds first, then runs ctest. |
| **Sanitize** (`sanitize`) | The same suite, rebuilt and rerun under ASan + UBSan (a separate `build/sanitize` config). LeakSanitizer is CI-Linux-only — see the target's own comment for why. |
| **Coverage** (`coverage`, `coverage-check`, `coverage-html`) | LLVM source-based coverage (Clang-pinned end to end). `coverage` prints the text summary and an HTML report; `coverage-check` enforces the line-coverage floor (the CI gate — `COV_FLOOR`/`COV_FLOOR_IGNORE` live in `../mk/common.mk`, since the root's own `full-local-gate` echoes the floor directly); `coverage-html` is the silent variant `make doc`/`docs-site-gate` build on. |
| **ThreadSanitizer smokes** (`tsan`, `tsan-core`) | Standalone Clang/TSan programs, not part of the ctest suite: `tsan` hammers the concurrent lazy `std_engine()` build on a shared regex (`compat/tsan_compat.cpp`); `tsan-core` barrier-syncs first searches on fresh regexes to race the core shared-confirm/immutables caches (`engine/tsan_core.cpp`). `tsan-core` has a can-fail proof — see the target's own comment. |

`test`/`sanitize`/`coverage-check` are gated in CI (`ci.yml`) and in `full-local-gate`;
`tsan`/`tsan-core` run in `ci.yml`'s fuzz job. The sciforge ecosystem CI also runs
`make -C real-regex test` directly (cross-repo) — the reason `test` stays a root-invocable name.

## Running

    make help                  # every target here, one line each
    make test                  # build (root) + run the ctest suite
    make sanitize               # ASan + UBSan
    make coverage                # text summary + HTML report (advisory)
    make coverage-check           # enforce the coverage floor (CI gate)
    make tsan                     # ThreadSanitizer: concurrent std_engine
    make tsan-core                 # ThreadSanitizer: core shared-confirm/immutables

Each also runs directly from this directory: `make -C tests <target>` from the repo root, or
`make <target>` from inside `tests/`. `build` itself is a repo-root-only target (`make build` from
the root, or `make -C tests test` after building separately) — it is not duplicated here.

See [docs/TESTS.md](../docs/TESTS.md) for the full testing picture (unit tests, the differential
fuzzers, the exhaustive enumeration, the public conformance suites, and the coverage exclusions'
own rationale) and what each one has caught.
