# P0 opportunities — classified (no optim builds)

**Stamp:** commit base `6139b22` + P0 WIP · host arm64 (M1 Pro) · clean timing / instrumented routes · `schema_version: 1`  
**Source:** `build/profile/run.jsonl`, `grid.md` · callgrind on this host is dyld-dominated (see note) — Ir/byte needs **devbox x86**.

## Hypotheses (a)–(d)

| ID | Claim | Verdict | Evidence |
|---|---|---|---|
| **(a)** SWAR-`\w+` | Port `run_class_loop` SWAR/cascade to Unicode word → close 1.5× vs rust | **SUPPORTED (proxy)** | `[a-z]+` 2.59 ns/B vs `\w+` 6.31 (**2.4×** same corpus). Forcing class_off on `\w+` → lazy-DFA 8.48 (**worse**). Gap is **scalar `klass_cp` vs byte class-loop**, not “missing DFA”. Gain-proxy: if `\w+` approached `[a-z]+` (~2.6), beats rust §E `\w+` 3.15. |
| **(b)** route-enum | Precomputed enum vs 11-if cascade | **OPEN / low priority** | No cell showed dispatch tax as dominant vs path body. Defer until (a)/(d) done. |
| **(c)** unif-wb | Unify `wb_lead`/`wb_trail` across families | **OPEN / simplify** | Not measured as Ir hot; keep as cleanup arc (correctness-neutral refactor). |
| **(d)** ident-DFA | Why lazy-DFA doesn’t serve dense `(\w+)_(\w+)` | **ANSWERED — IL dispatch** | On `ident_dense`, default route = **`inner_literal`** (memmem `_`). **Non-capturing** `(?:\w+)_(?:\w+)`: default **50.97** ns/B; force `il_off` → **lazy_dfa_anchored 5.84** (**8.73×** dispatch-dominated ≥20%). Capturing shape: IL ~69 still slightly beats forced DFA ~81 (extractor cost). Log-sparse `_`: IL **0.031** (correct). **10× §E residual = dense IL thrash, not slots.** |

## Opportunities (ranked)

### O1 — Inner-literal abandon / density gate (dispatch)
- **Mechanism:** IL is preferred over lazy-DFA even when `_`/`@` hit-rate is high → per-hit reverse-confirm dominates.
- **Cells:** `ident_ncap/ident_dense` (dominated 8.73×); contrast `ident_cap_log` 0.031 (IL correct).
- **Gain-proxy:** up to **~9×** on dense ncap; capture shapes need IL-vs-DFA cost model, not blind disable.
- **Arc:** *IL density / false-candidate budget* — abandon to DFA when candidates/byte exceeds threshold (existing `il_abandoned` seam path).

### O2 — SWAR / block scan for `klass_cp` word class (generalize (a))
- **Mechanism:** `run_cp_class_loop` per-CP vs `run_class_loop` SWAR for ASCII classes.
- **Cells:** `w_plus` 6.3 vs `az_plus` 2.6; `bw_word` 6.4 ≈ `w_plus` (B-1 live).
- **Gain-proxy:** ~2× raw `\w+` → likely pass rust 3.15 ns/B on prose.
- **Arc:** *cp-class ASCII-fast / SWAR with ≥0x80 bailout* (hypothesis a).

### O3 — Superset / general Pike cost
- **Mechanism:** `[\w😀]` under `\b` correctly **general_full** (55.9 ns/B) — recognition OK, no greedy_cp.
- **Cells:** `superset_emoji` — not a bug; shows general path floor.
- **Gain-proxy:** secondary; only if general-window hot in production traces.
- **Arc:** none for P0; keep as patho ceiling.

### O4 — Literal / alt prefilter (Teddy)
- **Cells:** `lit_dog` 0.26, `alt` 0.47 — already fast; §E rust still 1.2–2.5× on these shapes.
- **Arc:** Teddy multi-lit (register item) — lower priority than O1/O2 given absolute ns/B.

## Callgrind note
Homebrew valgrind on arm64 attributes **dyld**, not REAL hot loops. **Re-run `make profile-callgrind` on devbox (g++13 x86)** for Ir/byte columns on `\w+`, ident, alt, and rust `\w+`.

## Invisibility-OFF
- Macros expand to `((void)0)` without `-DREAL_PROFILE`.
- Gate tests (`make test`) profile-OFF: **all pass** including route-pinning.
- Smoke ON: `profile_runner_inst attr '\w+' …` → `routes.cp_class_loop ≥ 1`.

## Non-goals (P0 held)
No optim commits. No CI timing gate. No public API. Explorer (Phase 2) consumes `run.jsonl` schema_version 1 as-is.
