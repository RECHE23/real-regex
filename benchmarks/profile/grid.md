# P0 profile grid — 2026-07-10T02:39:29Z · 6139b22 · arm64

Timing = clean build; routes = `-DREAL_PROFILE` build. ns/B never from instrumented.

| cell | ns/B p50 | route dom | predicted | gap? | dominated? | force |
| --- | ---: | --- | --- | --- | --- | --- |
| w_plus/prose_ascii/count_matches/none | 6.229 | `cp_class_loop` | `cp_class_loop` |  | no | none |
| w_plus/prose_ascii/count_matches/class_off | 8.623 | `lazy_dfa_anchored` | `cp_class_loop` |  | no | class_off |
| az_plus/prose_ascii/count_matches/none | 2.591 | `class_loop` | `class_loop` |  | no | none |
| az_plus/prose_ascii/count_matches/class_off | 8.108 | `lazy_dfa_anchored` | `class_loop` |  | no | class_off |
| digits/log_dense/count_matches/none | 1.734 | `class_loop` | `class_loop` |  | no | none |
| bw_word/prose_ascii/count_matches/none | 6.144 | `cp_class_loop` | `cp_class_loop` |  | no | none |
| baz/prose_ascii/count_matches/none | 5.723 | `class_loop` | `class_loop` |  | no | none |
| ident_cap/ident_dense/count_matches/none | 68.555 | `inner_literal` | `lazy_dfa_or_general` |  | no | none |
| ident_cap/ident_dense/count_matches/ldfa_off | 62.540 | `inner_literal` | `lazy_dfa_or_general` |  | no | ldfa_off |
| ident_cap/ident_dense/count_matches/il_off | 81.294 | `lazy_dfa_anchored` | `lazy_dfa_or_general` |  | no | il_off |
| ident_cap/ident_dense/find_iter_span/none | 68.648 | `inner_literal` | `lazy_dfa_or_general` |  | no | none |
| ident_cap/ident_dense/find_iter_span/ldfa_off | 62.688 | `inner_literal` | `lazy_dfa_or_general` |  | no | ldfa_off |
| ident_cap/ident_dense/find_iter_span/il_off | 81.054 | `lazy_dfa_anchored` | `lazy_dfa_or_general` |  | no | il_off |
| ident_ncap/ident_dense/count_matches/none | 50.921 | `inner_literal` | `lazy_dfa_or_general` |  | YES by il_off (8.88×) | none |
| ident_ncap/ident_dense/count_matches/ldfa_off | 45.443 | `inner_literal` | `lazy_dfa_or_general` |  | no | ldfa_off |
| ident_ncap/ident_dense/count_matches/il_off | 5.733 | `lazy_dfa_anchored` | `lazy_dfa_or_general` |  | no | il_off |
| ident_cap_log/log_dense/count_matches/none | 0.031 | `inner_literal` | `lazy_dfa_or_general` |  | no | none |
| email/prose_ascii/count_matches/none | 0.031 | `inner_literal` | `lazy_dfa_or_general` |  | no | none |
| email/prose_ascii/count_matches/ldfa_off | 0.031 | `inner_literal` | `lazy_dfa_or_general` |  | no | ldfa_off |
| email/prose_ascii/find_iter_span/none | 0.031 | `inner_literal` | `lazy_dfa_or_general` |  | no | none |
| email/prose_ascii/find_iter_span/ldfa_off | 0.031 | `inner_literal` | `lazy_dfa_or_general` |  | no | ldfa_off |
| alt/sparse_generic/count_matches/none | 0.470 | `alternation` | `alternation` |  | no | none |
| lit_dog/sparse_generic/count_matches/none | 0.251 | `exact_literal` | `exact_literal` |  | no | none |
| date_fixed/log_dense/count_matches/none | 2.518 | `inner_literal` | `lazy_dfa_or_general` |  | no | none |
| superset_emoji/utf8_mixed/count_matches/none | 56.081 | `general_full` | `lazy_dfa_or_general` |  | no | none |

## Recognition gaps
_none on default cells_

## Dispatch-dominated (forced ≥20% faster)
- `ident_ncap` default 50.921 → il_off wins 8.88×
