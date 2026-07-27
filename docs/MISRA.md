# MISRA C++:2023 deviations

`make misra` runs clang-tidy with the profile in `.clang-tidy-misra`, which covers
a material subset of MISRA C++:2023 through the cppcoreguidelines, cert, bugprone,
hicpp and misc modules. The scope is REAL's own headers (`--header-filter` selects
`include/real/`; the synthetic translation unit instantiates the engine so template
code is checked). Every check left disabled in the profile is a deliberate,
justified deviation, documented here.

## Scope: the C ABI shim (`bindings/c`) is excluded

The analysis covers `include/real/` — the engine. `bindings/c/real_capi.cpp` (the C ABI shim) is deliberately
outside it. A C ABI is structurally at odds with MISRA C++: it deals in opaque handles (`real_regex*` /
`real_iter*` over incomplete types), raw pointer parameters, caller-owned C-string buffers, and
`reinterpret_cast` across the language boundary — precisely the constructs MISRA restricts, and precisely what
a C ABI *must* expose to be callable from C, Rust and beyond. Holding the shim to the engine's profile would
mean suppressing those rules one by one — noise, not safety. The shim earns its safety a different way: it is
compiled into the instrumented test binary (run under ASan + UBSan, coverage-visible), fuzzed
(`make c-fuzz`), and every entry point catches so no C++ exception crosses into C. An assumed, documented
deviation — not a silent gap.

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

## Deferred-initialisation deviation

### `cppcoreguidelines-pro-type-member-init` / `hicpp-member-init` on `basic_pike_state`

Suppressed at that one record (a scoped `NOLINTNEXTLINE`, not a profile-wide exclusion). Two of its members —
`table`, a 256-byte byte-class lookup, and `cp_page`, a 240-byte code-point bitmap — carry no initializer.
Each is filled **in full** on a miss against its own sentinel: `class_table()` writes all 256 entries when
`table_class` does not match the requested class, `cp_page_table()` clears and rebuilds the bitmap when
`cp_page_class` does not match, and both sentinels start at `-1`, which matches no class. So the zeros were
never read.

They were not free. `static_regex` is stateless (`sizeof` 1) and `search()` is `const` and thread-safe, so
both storages build a fresh state per single search; value-initialising these two members put a 496-byte
clear on every one. Measured on an 81-byte subject, arm64/clang, single `search()`:

| pattern | static before | static after | dynamic before | dynamic after |
|---------|---------------|--------------|----------------|---------------|
| `dog`   | 39.6 ns | **18.5** | 55.7 ns | **48.7** |
| `[0-9]+`| 189.0 ns | **163.7** | 206.1 ns | 209.0 |
| `[^,]+` | 280.0 ns | **172.2** | 211.0 ns | **203.0** |

Verified rather than argued: valgrind memcheck over the full suite on x86-64/g++ reports **zero**
uninitialised-value errors and a byte-identical error profile to the pre-change build (78 errors / 23
contexts either way, all `Mismatched free/delete` from `test_static.cpp`'s own instrumented `operator new`);
and the suite rebuilt with clang's `-ftrivial-auto-var-init=pattern`, which fills those members with a poison
pattern instead of zeros, passes 764 tests / 344 492 checks with none failing — so nothing depended on the
value.

Residual cost, stated because a record-scoped suppression is broader than a member-scoped one: a member
added to `basic_pike_state` later *without* an initializer would not be flagged. Every other member there
carries one.

`static_vec::data_` is left uninitialized for the same reason (nothing reads above `size_`, which starts at
0) and is not flagged, because the synthetic MISRA translation unit instantiates `real::regex`, which uses
`small_vec`. The justification is the same one.

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
| `cert-dcl21-cpp` | Obsolete "postfix `operator++` should return a const object" rule — returning a const value disables move semantics and contradicts standard-iterator conventions; the check was dropped in newer clang-tidy. |

> The profile is run with different clang-tidy versions locally (Homebrew) and in
> CI (distro `apt`); check sets differ slightly between versions. The disables and
> fixes above keep the gate green on both. A future step pins one clang-tidy
> version (owned by SciForge) so the gate is fully reproducible.
