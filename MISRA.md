# MISRA C++:2023 deviations

`make misra` runs clang-tidy with the profile in `.clang-tidy-misra`, which covers
a material subset of MISRA C++:2023 through the cppcoreguidelines, cert, bugprone,
hicpp and misc modules. The scope is REAL's own headers (`--header-filter` selects
`include/real/`; the synthetic translation unit instantiates the engine so template
code is checked). Every check left disabled in the profile is a deliberate,
justified deviation, documented here.

## Active-member deviation

### `cppcoreguidelines-pro-type-union-access`

`small_vec` (small-buffer optimisation) stores either an inline `T[InlineCapacity]`
buffer or a heap pointer in a `union Storage`, and tracks the active member with the
`is_heap_` flag. Every access of `storage_.inline_buffer` / `storage_.heap_ptr` is
guarded by `is_heap_` (verified site by site: inline accesses occur only on the
`is_heap_ == false` branch, heap accesses only on the `true` branch), so the active
member is never misread. This is the type-safe-by-active-member SBO idiom; a
`std::variant` would add a discriminator the container already maintains and would
not be usable in the constexpr engine. The union's copy/move special members are
explicitly `= delete`d (see `storage.hpp`) so a value copy of `Storage` — which
would inherit the wrong active member and double-free — cannot compile.

## Style / tooling deviations (pre-existing)

These checks are disabled because they conflict with deliberate, idiomatic choices
in a header-only constexpr regex engine; none hides a defect.

| Check | Reason |
|-------|--------|
| `cppcoreguidelines-avoid-magic-numbers` | Numeric literals are intrinsic to the VM opcodes, character-class bitsets and bit masks. |
| `cppcoreguidelines-pro-bounds-pointer-arithmetic` | The Pike VM and `small_vec` deliberately walk buffers by pointer. |
| `cppcoreguidelines-pro-bounds-constant-array-index` | Fixed compile-time tables and the inline buffer are indexed directly. |
| `cppcoreguidelines-pro-bounds-avoid-unchecked-container-access` | Hot paths index without `.at()`; bounds are established by surrounding invariants. |
| `hicpp-avoid-c-arrays`, `cppcoreguidelines-avoid-c-arrays`, `modernize-avoid-c-arrays` | The SBO inline buffer and constexpr tables must be C arrays (union / constant-evaluation constraints). |
| `misc-include-cleaner` | The umbrella header aggregates the module headers by design. |
| `misc-non-private-member-variables-in-classes`, `cppcoreguidelines-non-private-member-variables-in-classes` | Small POD aggregates (opcodes, hints) expose public data members intentionally. |
| `hicpp-named-parameter`, `readability-named-parameter` | Unnamed parameters appear in overload sets and tag dispatch. |
| `misc-no-recursion` | The descent parser is intentionally recursive (depth is bounded and guarded). |
| `bugprone-easily-swappable-parameters` | Some public signatures take adjacent same-type parameters (e.g. `pos`, `endpos`) to match the `re` API. |
| `cppcoreguidelines-avoid-const-or-ref-data-members` | A few helper types hold a reference/const member by design and are non-assignable. |
