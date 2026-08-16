# REAL — tools

What lives here, and how to run it. `make help` (from the repo root or `make -C tools help`)
lists every target with a one-line description; this file is the map, not a second copy of
the methodology.

| | |
|---|---|
| **Lint** (`lint`) | clang-tidy over the test sources, then the 36 shipped headers through a synthetic TU. The header pass prints `N headers analysed, M warnings` and fails if M is not 0. The SBO union is a named `--checks` exclusion (the accepted MISRA deviation), not a NOLINT. |
| **MISRA** (`misra`) | A MISRA C++:2023-oriented pass over `include/real/` through a synthetic translation unit, against the shared base profile SciForge owns (`$(SCIFORGE_LINT)/clang-tidy-misra`) plus REAL's one deviation (the SBO union in `storage.hpp`). See [docs/MISRA.md](../docs/MISRA.md). |
| **Format** (`format` / `format-check`) | Uncrustify over `include/` + `tests/` (generated Unicode tables excluded), against the shared config SciForge owns (`$(SCIFORGE_LINT)/uncrustify.cfg`). `format` rewrites in place; `format-check` is the dry-run CI uses. |
| **Layering** (`check-layers`) | Enforces the `include/real/` header dependency-tier contract (`check_layers.py`) — a header may only include from its own tier or a lower one. Prints `N headers` and fails if N is 0. |
| **C ABI golden** (`check-capi-abi`) | Regenerates and diffs the frozen C ABI surface pin (`gen_capi_abi_golden.py`) against `bindings/c/real_capi.h` — enum ordinals, documented flag bits, normalized prototypes. Golden lives at `tests/bindings/capi_abi_golden.txt`; enum/flag value pins live in `tests/bindings/test_capi_abi.cpp`. |
| **Doc voice** (`check-doc-voice`) | Fails if a Doxygen comment published by `Doxyfile.site` (public, non-`\internal`) still talks like a bench log. Implementation notes stay in `//`. Needs `build/doc/xml-site` (`make doc-site-xml`). Compares `Doxyfile.site` INPUT (exp) to the headers the XML names (got). `check-doc-style` reads the other tree (`build/doc/xml`) and compares the 36 on-disk headers to that XML. `make doc-xml` refreshes both. The same make target also runs the source twin (`check_doc_voice_source.py`) over `docs/*.dox` and `docs/*.md` and prints how many files it scanned. `docs/BENCHMARKS.md` and `docs/MEASUREMENT.md` are named journals (counted, not a fail); everything else is fail-closed. |
| **Curated members** (`check-curated-members`) | Fails if a `:members:` allowlist silently drops a published symbol, or if a page uses bare `:members:` without `publish_all` in `unpublished.yaml`. Omissions need a reason. |
| **Bench stamp** (`check-bench-stamp`) | Warns if engine *code* moved since the Version cell's `REAL \`X.Y.Z\`` was last written. Pickaxe on that string, not last touch of the path. Warns, never fails. |
| **Site anchors** (`check-site-anchors`) | Every site `:start-after:` / `:end-before:` resolves uniquely in its include target. Reads `docs/site/` only. Does not read `docs/BENCHMARKS.md`. |

## What reads `docs/BENCHMARKS.md`

The Version cell is a stamp (`REAL \`X.Y.Z\`` + whether the tables moved). The journal of
trains lives in [`CHANGELOG.md`](../CHANGELOG.md). There is no third file
(`BENCHMARKS-history.md` would be a second copy of CHANGELOG and is not in the
`make release` dirty allowlist). Scripts parse headings and cells, never the journal.
The YAML line stays: `ns/B` is the ledger's job, and the YAML names a right, not a todo.

| Attachment | Reads | What |
|---|---|---|
| `version-check` | content | the first `REAL \`X.Y.Z\`` (Version cell). A later `REAL \` in §B is not the stamp. |
| `check-bench-stamp` | content + git | the same cell, pickaxed: last commit that *wrote* that string. A host-name rewrite is not a re-measure. |
| `check-bench-ratios` | content | `verify_bench_ratios.py`: `## A.` → `## Multi-pattern` and `## E.` → `### E.1` (tables **and** §A reading bullets). `verify_unicode_ratios.py`: `## Unicode` → `## Methodology`. |
| `gate-doc` | path | if `docs/BENCHMARKS.md` is dirty, run the ratios. Does not parse the file. |
| `gate-bump` | path | this file may be dirty on a version-bump commit (with `CHANGELOG.md`). |
| `make release` | path | dirty allowlist = `docs/release-notes/` + `docs/BENCHMARKS.md`. `CHANGELOG.md` is not a release exception; it rides the bump commit. |
| `docs-site-gate` | forbid | no `{include}` of this file on the site. |

`check_site_anchors.py` is not a consumer. After P1 no site page includes the ledger, and
the gate forbids it; a docstring that still named the file was a ghost.

## Running

    make help                  # every target here, one line each
    make lint                  # clang-tidy over the test sources
    make misra                 # MISRA C++:2023-oriented analysis
    make format                # uncrustify, in place
    make format-check          # uncrustify, dry-run (CI uses this)
    make check-layers          # header-layering contract
    make check-capi-abi        # C ABI golden vs real_capi.h

Each also runs directly from this directory: `make -C tools <target>` from the repo root, or
`make <target>` from inside `tools/`.

## Regenerated tables / goldens

The scripts in this directory that generate committed artifacts (the Unicode fold/property/
script/binary-property tables, the C ABI golden) are not gate-only tools — see
[REGEN.md](REGEN.md) for the regen process, source data, and what to re-run after touching
their inputs.
