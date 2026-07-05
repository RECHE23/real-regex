# Language bindings

REAL's engine is header-only C++; these are the surfaces that make it callable from other languages.

| | |
| --- | --- |
| **`c/`** | The C ABI shim (`real_capi.h` / `real_capi.cpp`) — opaque handles over `real::regex`, the substrate every non-C++ binding sits on. |
| **`python/`** | The abi3 CPython binding (the `real` package; its README is the PyPI page). |
| **`rust/`** | The `real-regex` crate — a `regex`-crate-shaped API over the C shim. |

## CMake position: `bindings/c` is source-only, by design

REAL's CMake install exports the **header-only C++ library** (`find_package(real)`), and nothing else. The C
ABI shim is **not** installed as a library and has no CMake target: it exists to be *compiled into* a
consumer, not linked against.

- The Rust crate's `build.rs` `cc`-compiles `real_capi.cpp` directly (against the live headers in-tree, a
  vendored copy off crates.io). A future Node/Ruby binding would do the same.
- A header-only C++ engine plus a compile-into-consumer C boundary means there is no shared/static
  `libreal_capi` for a package manager to ship, and so nothing for `install()` to export.

Revisit only if a C (not C++) consumer needs REAL as a system-installed library — then `bindings/c` would
gain an `install(FILES real_capi.h)` and a static-lib target with its own find_package smoke. Until such a
consumer exists, adding one would be install surface with no user.
