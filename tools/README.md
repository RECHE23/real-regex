# REAL — tools

What lives here, and how to run it. `make help` (from the repo root or `make -C tools help`)
lists every target with a one-line description; this file is the map, not a second copy of
the methodology.

| | |
|---|---|
| **Lint** (`lint`) | clang-tidy over the test sources (`.clang-tidy` at the repo root). |
| **MISRA** (`misra`) | A MISRA C++:2023-oriented pass over `include/real/` through a synthetic translation unit, against the shared base profile SciForge owns (`$(SCIFORGE_LINT)/clang-tidy-misra`) plus REAL's one deviation (the SBO union in `storage.hpp`). See [docs/MISRA.md](../docs/MISRA.md). |
| **Format** (`format` / `format-check`) | Uncrustify over `include/` + `tests/` (generated Unicode tables excluded), against the shared config SciForge owns (`$(SCIFORGE_LINT)/uncrustify.cfg`). `format` rewrites in place; `format-check` is the dry-run CI uses. |
| **Layering** (`check-layers`) | Enforces the `include/real/` header dependency-tier contract (`check_layers.py`) — a header may only include from its own tier or a lower one. |
| **C ABI golden** (`check-capi-abi`) | Regenerates and diffs the frozen C ABI surface pin (`gen_capi_abi_golden.py`) against `bindings/c/real_capi.h` — enum ordinals, documented flag bits, normalized prototypes. Golden lives at `tests/bindings/capi_abi_golden.txt`; enum/flag value pins live in `tests/bindings/test_capi_abi.cpp`. |
| **Doc voice** (`check-doc-voice`) | Fails if a Doxygen comment published by `Doxyfile.site` (public, non-`\internal`) still talks like a bench log. Implementation notes stay in `//`. Needs `build/doc/xml-site` (`make doc-site-xml`). `check-doc-style` reads the other tree (`build/doc/xml`); `make doc-xml` refreshes both. |
| **Curated members** (`check-curated-members`) | Fails if a `:members:` allowlist silently drops a published symbol, or if a page uses bare `:members:` without `publish_all` in `unpublished.yaml`. Omissions need a reason. |

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
