# Profile opportunities (post O1)

**Stamp:** after O1 density-gate · see also P0 baseline in git history.

## Closed by O1
- **O1 IL density (dispatch):** dense ncap `(?:\w+)_(?:\w+)` was 51 ns/B via IL; after density-gate → **~7.5 ns/B** dominant `lazy_dfa_anchored` (il_abandoned after probe). Sparse IL / date / email / captures unchanged.

## Still open
- **O2 SWAR/`klass_cp`:** `[a-z]+` 2.6 vs `\w+` 6.2; callgrind: membership lambda not inlined. Arc separate.
- **O3/O4:** superset general floor; Teddy multi-lit — lower priority.

Reproduce: `make profile-sample` → `build/profile/run.jsonl` + `grid.md`.
