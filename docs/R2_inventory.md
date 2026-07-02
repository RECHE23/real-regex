# R2 inventory — verified dead-code / load-bearing / duplication snapshot (2026-07-02)

A point-in-time audit feeding the R (refinement) arc. Zero code change; each item carries its proof
(grep of consumers / a test / reasoning). Statuses: **MORT-prouvé** · **LOAD-BEARING-à-documenter** ·
**DUPLIQUÉ-factorisable** · **SAIN**. Verified by re-running a sample of the greps.

## 1. `pattern_hints` (program.hpp) — field by field

All 18 fields have a real read-consumer **except one**. Write site for every field is `analyze_program`
(prefilter.hpp); consumers below are READ sites.

| Field | Status | Consumers (read) |
|---|---|---|
| prefix / prefix_size | SAIN | pike.hpp:878/1000-1001, prefilter.hpp:451-452 |
| anchored_start | SAIN | pike.hpp:997, 1038 |
| line_anchored | SAIN | pike.hpp:1006 |
| first_bytes_valid | SAIN | pike.hpp:1010/1049, real.hpp:731/761 (has_first_byte_set/may_start_with) |
| empty_match_possible | SAIN | std_compat.hpp:681 (nullable gate). *NB:* prefilter's local of the same name is unrelated |
| single_first | SAIN | pike.hpp:1003, real.hpp:742 (unique_first_byte) |
| first_bytes | SAIN | pike.hpp:1012/1052, real.hpp:761 |
| greedy_class_loop / cp_class / _plus | SAIN | pike.hpp:230/440, 233/488, 530 |
| greedy_group_start / _end | SAIN | pike.hpp:414-416 (D4a) |
| fixed_shape | SAIN | pike.hpp:239 (D4b) |
| codepoint_class_ascii / _plus | SAIN | pike.hpp:242/753, 792 |
| fixed_alternation | SAIN | pike.hpp:245 |
| exact_literal_len | SAIN | pike.hpp:236/952 |
| **has_lookaround** | **LOAD-BEARING-à-documenter** | **prefilter.hpp:443 ONLY** — written 426, read 443 within the *same* `analyze_program` to clear the five fast-path hints. Drives behavior (not dead) but never persists past analysis: no engine/DFA/binding reader. **Candidate: demote from a persisted struct field to a local in `analyze_program`.** |

`dfa.hpp` and `compiler.hpp` read **zero** hint fields (verified). No DUPLIQUÉ fields.

## 2. `std_compat.hpp` — S6b (eager-nullable removal) cleanliness

- **The eager-nullable removal (S6b, `62fac28`) is CLEAN** — SAIN. No orphan branch/flag survives; the
  `nullable_`/`lazy_std_` members and `uses_real_traversal()` are on a live path (regex_replace:1072,
  regex_iterator ctor:1169).
- **`basic_regex::nullable()` (std_compat.hpp:614) — MORT-de-callers.** Zero callers
  (`grep -rn '\.nullable()' include/ tests/ python/` → none; verified). Internal routing reads the
  `nullable_` member, never the accessor. **Action: add a test pin (embedder-introspection, like the
  first-byte API) OR remove.**
- **Stale comment — `tests/test_compat.cpp:1121`** ("the eager std build at ctor is swallowed"):
  misdescribes current behavior (S6b made it lazy). **Action: fix the comment.**
- `sub_match::compare(const sub_match&)` (:207) — LOAD-BEARING (std::sub_match parity API) but
  **untested** (only the `compare(std::string)` overload is pinned, test_compat.cpp:757). Add a test if
  the coverage gate should be honest here.
- All single-caller helpers checked (grammar_forces_std, to_real, replace_stays_real, expand_format,
  rebase_prefix, …) — each names a routing predicate that clarifies its one call site. **None warrant
  inlining.**

## 3. `ast.hpp` — escape/flag duplication

- Escape **decoding** is already well-factored on the `decode_digit_escape` model (`parse_byte_escape`,
  `parse_unicode_codepoint`, `decode_digit_escape`), shared by atom and class contexts. SAIN.
- **Finding 1d — DUPLIQUÉ-factorisable (the one substantive item):** the `\d \D \w \W \s \S` letter →
  (set-fn, ranges-table, negated) classification is written **twice** — atom at ast.hpp:1208-1225
  (`add_class_node(... shorthand_ranges(TABLE) ...)`), class at ast.hpp:1325-1348
  (`merge_property(klass, ranges, SET(), TABLE, NEG ...)`). A shared classifier
  (`shorthand_lookup(letter, set&, table&, negated&)`) would put the letter→set/table fact in one place;
  the two call sites legitimately diverge in what they DO (emit node vs merge into class) — exactly the
  decode_digit_escape split. **Proposed, René to decide.**
- Naming candidate: `static bool is_ascii(char)` for the 3× `static_cast<uint8_t>(ch) …/>= 0x80` idiom
  (ast.hpp:546/1005/1303) — sits beside the existing `is_ascii_alnum`; also erases a `0x80U` vs `0x80`
  inconsistency. Count 3 → propose, don't force.
- `text_shorthand()` (7 uses) is the exemplary already-factored predicate. `bytes_`/`ecma_` bare checks
  (4×/6×) are SAIN — already atomic names; an accessor would be pure ceremony. **Do not force.**
- Findings 1a-1c (dangling-backslash consume, `\N{...}` reject, byte-escape reject tail) are exact but
  cosmetic dups — not worth acting on.

## 4. pike / compiler / prefilter — bypassed paths & defensive branches

- **fold-byte (removed in CF2) — SAIN, zero crumbs.** `flags::icase`/`fold` appear zero times in
  pike.hpp; all folding is compile-time (`compiler.hpp:545 effective_class` → fold_ascii_case /
  unicode_casefold). The `byte` emitter documents the routing (compiler.hpp:588-591). No dead byte-fold
  branch. prefilter.hpp:361/374 document a *completed* replacement (content check), not orphaned code.
- **Defensive-unreachable branches — SAIN, all annotated.** Every structurally-unreachable branch found
  carries the coverage-honesty comment (storage.hpp:99/123, compiler.hpp:386 interner same=false,
  prefilter.hpp:370, pike.hpp:1222/1507/1330). All `opcode` switches are exhaustive (no `default:`).
  **Zero offenders lacking the honest comment.**

## 5. real.hpp / binding — unexposed surfaces

- SAIN. Every `Match_*`/`Pattern_*`/`real_*` C function in _real.cpp is wired (def + table entry). The
  one non-`re` C++ surface, the first-byte API, is documented, consumed by SciLex, and tested
  (tests/test_first_bytes.cpp). Nothing genuinely unreferenced (documented public API is out of scope).

## 6. Tests — coverage duplication

- SAIN, no clear exact-duplicates (conservative bar — over-coverage < a gap). test_classes (re-oracle)
  vs test_klass_cp (VM-internal), test_iterate (1-arg) vs test_region (pos/endpos), test_anchors vs
  test_region, and the unicode table tests (isolated second-net) are all distinct layers.

---

## Actionable shortlist (for future R fiches, René to prioritize)

1. `has_lookaround` → demote to a local in `analyze_program` (removes a persisted field with no reader).
2. `std_compat.hpp basic_regex::nullable()` → test-pin or remove (zero callers).
3. `tests/test_compat.cpp:1121` stale eager-build comment → fix.
4. ast.hpp Finding 1d → shared `\d\D\w\W\s\S` classifier (the one real factoring); + optional `is_ascii(char)` helper.
5. (optional) test-pin `sub_match::compare(const sub_match&)`.

Everything else audited is SAIN — the arcs left the code clean (fold-byte removal, S6b, defensive-branch honesty all verified).
