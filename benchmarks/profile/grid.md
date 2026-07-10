# P0 profile grid — 2026-07-10T03:00:45Z · 16d0543 · arm64

Timing = clean build; routes = `-DREAL_PROFILE` build. ns/B never from instrumented.

| cell | ns/B p50 | route dom | predicted | gap? | dominated? | force |
| --- | ---: | --- | --- | --- | --- | --- |
| w_plus/prose_ascii/count_matches/none | 6.246 | `cp_class_loop` | `cp_class_loop` |  | no | none |
| w_plus/prose_ascii/count_matches/class_off | 8.755 | `lazy_dfa_anchored` | `cp_class_loop` |  | no | class_off |
| az_plus/prose_ascii/count_matches/none | 2.592 | `class_loop` | `class_loop` |  | no | none |
| az_plus/prose_ascii/count_matches/class_off | 8.088 | `lazy_dfa_anchored` | `class_loop` |  | no | class_off |
| digits/log_dense/count_matches/none | 1.792 | `class_loop` | `class_loop` |  | no | none |
| bw_word/prose_ascii/count_matches/none | 6.146 | `cp_class_loop` | `cp_class_loop` |  | no | none |
| baz/prose_ascii/count_matches/none | 5.876 | `class_loop` | `class_loop` |  | no | none |
| ident_cap/ident_dense/count_matches/none | 68.717 | `inner_literal` | `lazy_dfa_or_general` |  | no | none |
| ident_cap/ident_dense/count_matches/ldfa_off | 62.912 | `inner_literal` | `lazy_dfa_or_general` |  | no | ldfa_off |
| ident_cap/ident_dense/count_matches/il_off | 81.790 | `lazy_dfa_anchored` | `lazy_dfa_or_general` |  | no | il_off |
| ident_cap/ident_dense/find_iter_span/none | 68.582 | `inner_literal` | `lazy_dfa_or_general` |  | no | none |
| ident_cap/ident_dense/find_iter_span/ldfa_off | 63.257 | `inner_literal` | `lazy_dfa_or_general` |  | no | ldfa_off |
| ident_cap/ident_dense/find_iter_span/il_off | 81.637 | `lazy_dfa_anchored` | `lazy_dfa_or_general` |  | no | il_off |
| ident_ncap/ident_dense/count_matches/none | 7.515 | `lazy_dfa_anchored` | `lazy_dfa_or_general` |  | YES by il_off (1.45×) | none |
| ident_ncap/ident_dense/count_matches/ldfa_off | 53.443 | `general_full` | `lazy_dfa_or_general` |  | no | ldfa_off |
| ident_ncap/ident_dense/count_matches/il_off | 5.179 | `lazy_dfa_anchored` | `lazy_dfa_or_general` |  | no | il_off |
| ident_cap_log/log_dense/count_matches/none | 0.031 | `inner_literal` | `lazy_dfa_or_general` |  | no | none |
| email/prose_ascii/count_matches/none | 0.031 | `inner_literal` | `lazy_dfa_or_general` |  | no | none |
| email/prose_ascii/count_matches/ldfa_off | 0.031 | `inner_literal` | `lazy_dfa_or_general` |  | no | ldfa_off |
| email/prose_ascii/find_iter_span/none | 0.031 | `inner_literal` | `lazy_dfa_or_general` |  | no | none |
| email/prose_ascii/find_iter_span/ldfa_off | 0.031 | `inner_literal` | `lazy_dfa_or_general` |  | no | ldfa_off |
| alt/sparse_generic/count_matches/none | 0.468 | `alternation` | `alternation` |  | no | none |
| lit_dog/sparse_generic/count_matches/none | 0.259 | `exact_literal` | `exact_literal` |  | no | none |
| date_fixed/log_dense/count_matches/none | 2.539 | `inner_literal` | `lazy_dfa_or_general` |  | no | none |
| superset_emoji/utf8_mixed/count_matches/none | 56.541 | `general_full` | `lazy_dfa_or_general` |  | no | none |

## Recognition gaps
_none on default cells_

## Dispatch-dominated (forced ≥20% faster)
- `ident_ncap` default 7.515 → il_off wins 1.45×
