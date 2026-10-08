# REAL — performance baseline

A reproducible snapshot of REAL's throughput against other regex engines, and of
its Python binding against the standard library's `re`. It serves two purposes:

1. an **honest competitive picture** — where REAL wins, where it loses, and why;
2. a **regression tripwire** — re-measure on the *same machine* before a grouped
   push and investigate any clear, repeatable slowdown.

It is informational only. Neither benchmark is invoked by `full-local-gate`, and
neither can fail a build — wall-time is noisy and hardware-dependent, so absolute
figures and cross-machine comparison are explicitly **not** the goal.

Reproduce with **`make bench-engines`** (C++, in-process) and **`make python-bench`**
(binding vs `re`). Both check result/match-count equality before timing — a fast wrong
answer is not a benchmark win.

> ### Read this before reading any delta
>
> Every **absolute** number in this document (the `ns/B` columns, the ratios against `std::regex`,
> PCRE2-JIT and RE2) is a straightforward measurement and stands as published.
>
> Every **before/after delta** in `CHANGELOG.md` — "−26.8 %", "+15.3 %", "+5.1 %" — was
> produced by compiling twice and comparing. That method has since been calibrated **on this very
> harness** and found to report **double-digit deltas on rows that provably cannot be affected**:
> deleting 21 lines of compile-time-only code moved `digits [0-9]+` by +16.7 %. The cause is that
> `real` is header-only, so a single build is one sample from a distribution of code layouts that
> belongs to the consumer, not to us.
>
> **`docs/MEASUREMENT.md` is now the authority on what a timing claim here is allowed to say.** It
> gives each row's measured noise floor — they range from **0.1 % to 7.3 %** — and lists, claim by
> claim, which figures in the train journal survive contact with those floors and which do not. Two
> decision rules this project used are retired there, including "same direction on both ISAs means
> real work".
>
> The Version cell is a stamp, not a train. Deltas live in `CHANGELOG.md` and are kept rather than
> deleted: they are the record of what was tried, and the refutations among them are the most useful
> part. Treat each as *one draw*, not as the change's property, and re-judge with `make bench-layout`
> before relying on one.

## Conditions of this baseline

| | |
| --- | --- |
| Version | REAL `2026.10.5` + tree `3c72c9d6` — **tables moved (measured).** Three runs per ISA, minimum per cell (§A, §E, §Unicode), six on x86-64 for §A and §E (that host has episodes), median of three (§B). Same hosts as the last stamp (tree `6a80b657`, two days earlier), so the columns are read against it. Between the two: the trailing-lookaround route takes code-point bodies and is one call per match, the batched code-point filler inlines its width test under GCC `-O2`, and the bindings cross with fewer calls and copies. Every row that moved by more than its band was checked against an instruction count (GCC 13, 14, 15 and clang 19) and, on arm64, against a layout-robust A/B; what those found is stated row by row below. §multi-pattern keeps the `600b0fb` stamp (its host is the only one with Hyperscan). Train and deltas: `CHANGELOG.md`. |
| Machines | §A on **two ISAs**: `x86-64` (g++ 15.3.1 on an idle Skylake-family core, the host of the last stamp) *and* `arm64` (Apple clang 16, **on AC power** — see `docs/MEASUREMENT.md` §3.5 for why the state is declared and why its cost must not be assumed). §B on arm64; §E and §Unicode's duel on both, x86-64 on §A's host. §multi-pattern measured on **x86-64** (g++ 13.3, RE2 + Hyperscan 5.4) on the original host, at tree `600b0fb` |
| Engines | `std::regex`; **PCRE2 10.47, JIT on, both ISAs** (the x86-64 host's system package; `make bench-engines` prints the version it actually LINKED, which the recipe resolves through `pkg-config`, so a second installation wins silently unless `PKG_CONFIG_PATH` and `LD_LIBRARY_PATH` both point at the intended one); RE2 (2025-11-05, soname 11, built from source on x86-64 — the host carries the library but not its headers; 11.0 on arm64). Multi-pattern: RE2::Set, Hyperscan (optional). §E: rust `regex` 1.12.4 |
| Python | CPython 3.14.6, `re` (stdlib) vs the in-place REAL `2026.10.5` extension at tree `3c72c9d6`, median of three runs per cell, `benchmarks/bench.py` run on an already-built extension: `make python-bench` builds it first, and a run right after that build reads its first case slow (the build's heat, not the code). `sub · dates with refs` remains the known-unstable row. Train: `CHANGELOG.md`. |
| Method | §A: median of N = 30 paired batches, bootstrap CI, **three full runs per ISA with the minimum taken per cell** (both ISAs — one run does not survive a host's episodic interference); match counts equal on every case, both ISAs. §E: `run_duel.py` best-of-15 per run, minimum of three runs per cell, REAL `count_matches` vs rust `find_iter`/`captures_iter`, match counts equal; `real_bench` built by hand at `-O3 -flto` as the tables state (the `make bench-duel` recipe builds it at `-O2`). §multi-pattern: best-of-7, `make bench-multipattern`. **Every ratio below is computed from the raw ns/B pair, and `benchmarks/verify_bench_ratios.py` re-derives all of them plus §A's reading bullets — `make check-bench-ratios`, step 7b of the local gate, part of `gate-doc` whenever this file is touched, and a step of the **Docs-site** workflow — which is the one CI net with no `paths-ignore`, so it fires on the doc-only pushes that edit this file.** That wiring is new, and the sentence it replaces was not true when it was written: the script was called from nothing at all, and running it for the first time failed on two cells — a third once its rounding rule was made exact — while every range, per-row pair and count in §A's bullets had been stale for three stamps. A checker nothing runs is a claim, not a check |

## A. C++ engine throughput

Each engine compiles the pattern once, then counts all non-overlapping matches over the same corpus; only
the scan is timed. `ns/B` is nanoseconds per corpus byte (lower is better). `(x)` is *engine_time /
REAL_time* — **> 1 means REAL is faster**. Match counts agreed across all four engines on every case, on
both ISAs, on the same `3c72c9d6` tree for this re-stamp.

**x86-64** — g++ 15.3.1, N = 30 × 6 runs, PCRE2 10.47-JIT, RE2 2025-11-05 (a Skylake-family core, the host of the last stamp):

| case | REAL ns/B | std::regex | PCRE2-JIT | RE2 |
| --- | ---: | ---: | ---: | ---: |
| words `[a-z]+` | 1.22 | 25.25 (**20.65×**) | 5.10 (**4.17×**) | 18.44 (**15.08×**) |
| digits `[0-9]+` | 0.68 | 15.39 (**22.53×**) | 2.90 (**4.25×**) | 10.29 (**15.07×**) |
| fields `[^,]+` | 1.68 | 15.69 (**9.32×**) | 3.98 (**2.36×**) | 14.75 (**8.76×**) |
| single `[a-z]` | 2.49 | 41.33 (**16.59×**) | 19.03 (**7.64×**) | 59.59 (**23.91×**) |
| words `[a-z]{4,}` | 0.70 | 14.85 (**21.12×**) | 3.27 (**4.65×**) | 10.90 (**15.50×**) |
| words `[a-z]++` | 0.87 | 20.25 (**23.25×**) | 5.10 (**5.86×**) | unsupported |
| alternation `the\|fox\|dog` | 1.22 | 19.41 (**15.98×**) | 1.80 (**1.48×**) | 6.96 (**5.73×**) |
| date `{4}-{2}-{2}` | 0.47 | 11.59 (**24.50×**) | 0.49 (**1.04×**) | 2.79 (**5.90×**) |
| hex `[0-9a-f]{8}` | 0.98 | 13.15 (**13.43×**) | 1.50 (**1.53×**) | 2.78 (**2.84×**) |
| literal | 0.21 | 9.39 (**43.88×**) | 0.48 (**2.24×**) | 1.60 (**7.48×**) |
| anchored `^[a-z]+$` | 0.41 | unsupported | 0.42 (**1.03×**) | 1.21 (**2.97×**) |
| lookahead `[a-z]+(?=[a-z])` | 3.74 | 43.94 (**11.75×**) | 5.43 (**1.45×**) | unsupported |

**arm64** — Apple clang 16, N = 30 × 3 runs, PCRE2 10.47-JIT, RE2 11.0:

| case | REAL ns/B | std::regex | PCRE2-JIT | RE2 |
| --- | ---: | ---: | ---: | ---: |
| words `[a-z]+` | 0.78 | 91.46 (**117.71×**) | 2.32 (**2.99×**) | 13.75 (**17.70×**) |
| digits `[0-9]+` | 0.64 | 82.35 (**129.48×**) | 1.44 (**2.26×**) | 8.65 (**13.60×**) |
| fields `[^,]+` | 1.82 | 74.27 (**40.79×**) | 1.87 (**1.03×**) | 10.97 (**6.02×**) |
| single `[a-z]` | 1.92 | 66.38 (**34.52×**) | 8.04 (**4.18×**) | 41.17 (**21.41×**) |
| words `[a-z]{4,}` | 0.66 | 71.67 (**109.09×**) | 2.03 (**3.09×**) | 8.07 (**12.28×**) |
| words `[a-z]++` | 0.78 | unsupported | 2.32 (**2.99×**) | unsupported |
| alternation `the\|fox\|dog` | 1.30 | 110.95 (**85.02×**) | 1.56 (**1.20×**) | 6.11 (**4.68×**) |
| date `{4}-{2}-{2}` | 0.33 | 71.18 (**217.01×**) | 0.39 (**1.19×**) | 3.41 (**10.40×**) |
| hex `[0-9a-f]{8}` | 1.42 | 79.35 (**56.04×**) | 1.24 (0.88×) | 3.41 (**2.41×**) |
| literal | 0.20 | 30.75 (**156.09×**) | 0.48 (**2.44×**) | 1.36 (**6.90×**) |
| anchored `^[a-z]+$` | 0.32 | unsupported | 0.42 (**1.32×**) | 2.18 (**6.86×**) |
| lookahead `[a-z]+(?=[a-z])` | 4.38 | 155.96 (**35.62×**) | 3.66 (0.84×) | unsupported |

**Reading — verdict brut, no dressing up. Every ratio in these bullets is checked against the cells
above by `make check-bench-ratios`** (local gate step 7b), because the bullets below were wrong for
three consecutive stamps while the tables were right — see each bullet's own note.

<!-- [std-regex-reading] — drop-in/std-regex-tour.md slices from here to the RE2 bullet. Placed
     BEFORE the list, not between two items: an HTML comment inside a list splits it in two when
     rendered. Same reason as the duel-reading marker further down — the wording this used to anchor
     on was the heading above, and extending that heading by one sentence broke the site build. A
     marker's own name is never spelled in brackets outside its marker: two occurrences and the
     extractor silently takes the first, which check-site-anchors refuses. -->
- **REAL ≫ `std::regex`**, always: **9.32–43.88×** on x86-64, **34.52–217.01×** on arm64 (libc++'s
  `std::regex` falls even further behind on arm64). Never below 9.32×. These bounds are read off the
  cells by `make check-bench-ratios`; they drifted from the table for three stamps while they were typed.
- **REAL > RE2**, always where RE2 supports the pattern: **2.84–23.91×** on x86-64, **2.41–21.41×** on
  arm64.
- **REAL vs PCRE2-JIT: ten of twelve rows are REAL's on BOTH ISAs** — `words [a-z]+` (**4.17×**
  x86-64 / **2.99×** arm64), `digits` (**4.25×** / **2.26×**), `single` (**7.64×** / **4.18×**),
  `words [a-z]{4,}` (**4.65×** / **3.09×**), `words [a-z]++` (**5.86×** / **2.99×**), `alternation`
  (**1.48×** / **1.20×**), `date` (**1.04×** / **1.19×**), `literal` (**2.24×** / **2.44×**), `anchored`
  (**1.03×** / **1.32×**) and `fields` (**2.36×** / **1.03×**) — the x86-64 `anchored` and `date` cells and the
  arm64 `fields` cell a hair over parity, not wins worth leaning on. The other two are REAL's on **x86-64 only**:
  `hex` (**1.53×** / 0.88×) and `lookahead` (**1.45×** / 0.84×).
- **What moved since the last stamp, two days earlier on the same hosts.** `lookahead` is the train's row: x86-64
  4.42 → 3.74 ns/B (−15 %), at −14.5 % instructions on GCC 15; arm64 4.46 → 4.38 (−1.9 %). x86-64 `fields` 1.93 →
  1.68 (−13 %) is the code-point filler's GCC inlining. Two x86-64 rows moved the other way **at an unchanged
  instruction count** and are this build's placement, not engine work: `words [a-z]+` 0.86 → 1.22, with
  `std::regex`'s own cell on that row +37.5 % and the `[a-z]+` witness of §Unicode (same pattern, other corpus)
  +1.7 %, the same in all six runs; and `single` 2.20 → 2.49 (+13 %), which a REAL-only unit counts at −2.4 %
  instructions and which keeps its gap with the branch-alignment workaround for this core's jump erratum, so
  this four-engine unit's inlining. arm64 reads within 3 % of the last stamp on every row.
- **The gauge.** `std::regex`, PCRE2 and RE2 are third-party constants, so their columns are the drift
  witness. RE2 reads within 0.6 % of the last stamp on x86-64 and within 0.7 % on arm64 but `alternation`
  (−5.4 %) and `digits` (+2.4 %); PCRE2 within 0.3 % on arm64. `std::regex`'s x86-64 column moves with this
  unit's placement (`words [a-z]+` +37.5 %, `single` −5.4 %, `lookahead` −4.1 %): read a REAL move on those
  x86-64 rows against it.
- **The lookahead line is PCRE2's on arm64, and the gap is a trade this project chose.** REAL does a
  **bounded lookaround in linear time**; PCRE2 is faster here by **backtracking** (itself ReDoS-able on a
  crafted lookaround), and **RE2 and the rust crate cannot compile the pattern at all**. On x86-64 the
  row is REAL's at this stamp (1.45×), as at the last one. The row is `count_matches`; the trailing-lookaround rework of v2026.8.12
  paid 18 % here for −91.8 % on `find_iter`, which is where most callers meet this shape.

## Multi-pattern — which-matched + extraction (Stage-1 `regex_set`)

Reproduce with **`make bench-multipattern`** (RE2 and Hyperscan optional via pkg-config). Informational
only — not a CI gate. Absolute MB/s track the host; the durable content is the **shape** and the
equal-set / equal-count asserts.

**Semantics (round-3, equal counts):**

| Table | Question | Engines | Forced full scan? |
| --- | --- | --- | --- |
| **A — filtre / IDS** | which-matched (which patterns hit ≥ once) | REAL N-walks (`regex_set`), RE2::Set, Hyperscan `SINGLEMATCH` | yes — 8 present + (N−8) absent |
| **B — extraction** | all non-overlapping matches | REAL `count_matches` N-walks, RE2 `FindAndConsume` N-walks | inherent (present patterns only) |

**x86-64** (g++ 13.3, RE2, Hyperscan 5.4, 1 MiB log-like corpus, best-of-7 MB/s, best of three runs, higher is better; tree `600b0fb`, 2026-09-29, on the stamp's original x86-64 host — §A's new host has no Hyperscan):

TABLE A — which-matched (sets equal when all engines compile):

| N | HS single | RE2::Set | REAL `regex_set` |
| ---: | ---: | ---: | ---: |
| 16 | ~343 | ~444 | **~677** (fastest) — N-walks |
| 32 | ~322 | ~451 | ~399 — fused after 1 MiB |
| 64 | ~383 | ~452 | ~398 — fused |
| 128 | ~379 | ~452 | ~388 — fused |

TABLE B — extraction non-overlapping (counts equal REAL/RE2):

| N | REAL N-walks | RE2 N-walks |
| ---: | ---: | ---: |
| 4 | ~285 | ~70 |
| 8 | ~157 | ~42 |

**Reading — capacity first, speed second:**

- **This stamp (`600b0fb`, x86-64, best of three runs of best-of-7).** `regex_set` walks each member below
  `fused_deferred_min_eligible` (24), builds its fused which-matched DFA once a set of 24 to 55 members has
  walked 1 MiB, and at construction from `fused_min_eligible` (56). N = 32 therefore reads the fused scan
  now, ~399 MB/s against the N-walks' ~228 at the last stamp. **The N = 64 and 128 rows read lower than the
  last stamp (~398 and ~388 against ~426 and ~466) and are layout, not work:** the same bench source built
  against the `2026.9.9` tree and this one, both with branches aligned to 32 bytes
  (`-Wa,-mbranches-within-32B-boundaries`; this host is a Coffee Lake core, which the JCC erratum makes
  placement-sensitive), reads 465–478 and 464–465 MB/s at N = 64, and 732–751 against 735 at N = 16. A
  bisect over the train's commits moved N = 64 between 368 and 481 MB/s on commits that do not touch the
  set's scan. **Table B's gain is work:** aligned the same way, extraction reads 228 → 298 MB/s at N = 4
  and 143 → 164 at N = 8 (the literal search of this train), with RE2's column unchanged at 69 and 42.

- **Architectural gap:** single-pass engines (RE2::Set, Hyperscan) stay **flat** in N; pure N-walks
  **degrade** hard (e.g. ~421 → 41 MB/s from N=32 → 256 on arm64). Stage-2 fused which-matched
  is **flat** and closes most of that gap.
- **Stage-2 fused + set-level first-byte skip** (same host/harness, arm64,
  `benchmarks/s2a_measure.cpp`, RE2::Set on). Two corpora: **dense** log-like (matches every
  line) and **sparse** realistic (generic text, rare hits — where prefix-accel matters).

  **SPARSE** (first-byte skip's happy path):

  | N | fused+skip | fused no-skip | pure N-walks | RE2::Set | sets |
  | ---: | ---: | ---: | ---: | ---: | ---: |
  | 64 | **~569 MB/s** | ~402 | ~45 | ~460 | equal |
  | 128 | **~560** | ~402 | ~21 | ~457 | equal |
  | 256 | **~553** | ~402 | ~10 | ~460 | equal |

  **DENSE** (skip still helps modestly):

  | N | fused+skip | fused no-skip | pure N-walks | RE2::Set | sets |
  | ---: | ---: | ---: | ---: | ---: | ---: |
  | 64 | ~438 MB/s | ~399 | ~178 | ~452 | equal |
  | 128 | ~434 | ~389 | ~83 | ~454 | equal |
  | 256 | ~432 | ~385 | ~40 | ~455 | equal |

  Fused stays **flat** in N (~4–50× pure N-walks at large N). **Skip vs no-skip:** ~+40% sparse,
  ~+10–12% dense (teeth-verify). **vs RE2::Set same-host:** sparse **~1.2× ahead** (claim
  measured); dense still **~0.95–0.97×** (quasi-parité, not a rout). `regex_set` routes fused when
  `eligible.size() ≥ 56` (calibrated crossover), else N-walks; lookarounds stay N-walk. Skip is
  off when any rule lacks a sound `first_bytes` set (empty-match / can start anywhere).
- **`\b`/`\B` wrap on shape fast-paths** (same host, arm64, post-Stage-2 tree):
  patterns like `\b[0-9a-f]{8}\b` and `(?:foo|bar)\b` re-use `run_fixed_shape` / `run_alternation`
  / exact-literal with an O(1) boundary check. Fair same-hit-count vs the unwrapped proxy:
  **`\bhex8\b` ~1.0× proxy SIMD** (was ~0.1× / ~57 MB/s on dense → ~540 MB/s); alt-trail ~0.84–0.96×
  proxy. Not a general assertion-DFA; `$` / complex assert shapes stay deferred.
- **Incumbent for this product shape is RE2::Set**, not Hyperscan. « Faster than Hyperscan » is not
  a product goal; HS is another corner (thousands of literals / streaming).
- **At small N** pure N-walks remain competitive (sometimes faster than fused).
- **Extraction:** REAL per-pattern `count_matches` beats RE2 N-walks ~2.5× on the present-pattern table.
- **Bounded lookarounds** are in REAL's set (RE2::Set cannot compile them) — a feature differentiator
  beyond throughput.
- `real::dfa` munch is **not** this API (lexer one-winner); Stage-2 uses `dfa_mode::which_matched`.

## B. Python binding vs re

`ratio` is *re_time / REAL_time* — **> 1 means REAL is faster**.

| case | `re` | REAL | ratio |
| --- | ---: | ---: | ---: |
| date · search @100KB | 1.16 ms | 1.5 µs | **754.79×** |
| date · findall groups | 1.24 ms | 2.0 µs | **613.21×** |
| alternation · findall @100KB | 799.9 µs | 9.4 µs | **84.75×** |
| word starts ASCII · findall (multiline) | 447.7 µs | 8.6 µs | **52.05×** |
| sub · dates with refs | 1.24 ms | 25.1 µs | **49.61×** ⚠ |
| literal · miss @1MB | 694.0 µs | 36.3 µs | **19.14×** |
| literal · hit @1MB | 694.6 µs | 36.4 µs | **19.05×** |
| digits · sparse findall @100KB | 1.14 ms | 93.1 µs | **12.34×** |
| sub · spaces @100KB | 2.22 ms | 451.8 µs | **5.00×** |
| words · findall @1KB | 16.1 µs | 5.0 µs | **3.22×** |
| emails · findall groups | 1.48 ms | 583.6 µs | **2.53×** |
| words · findall @10KB | 151.5 µs | 63.8 µs | **2.40×** |
| words · findall @100KB | 1.52 ms | 709.7 µs | **2.13×** |
| words · dense findall @100KB | 1.53 ms | 730.3 µs | **2.10×** |
| split · commas @100KB | 76.9 µs | 39.8 µs | **1.95×** |
| words · findall @1MB | 15.84 ms | 8.26 ms | **1.91×** |
| hex ids · findall | 248.6 µs | 132.9 µs | **1.89×** |
| non-space · Unicode findall | 1.76 ms | 1.09 ms | **1.61×** |
| literal · anchored miss @1MB | 182 ns | 196 ns | 0.92× |
| `(a+)+b` · re n=24 / REAL n=10k (prefilter) | 1087.17 ms | **598 ns** | **~1.8×10⁶×** (ReDoS) |

The reason is worth keeping, because it bounds what §B can ever show: this table's regime is dominated by
the **per-call** cost of crossing into Python, not by the scan. v2026.8.11's engine gain was −5.2 % on one
C++ row of eighteen; at this boundary that is invisible. So an engine train that touches only scan cost
should be expected NOT to move §B, and "§B is stale" is a weaker debt than four consecutive release-note
sets implied. What WOULD move it is per-call work — which is what v2026.8.6 (20–25 % off the fixed per-call
cost) did, and it shows below. The exception proves the rule's premise rather than breaking it: `alternation ·
findall` went from 2.60× to 68.99× at the `2026.9.9` stamp (84.75× at this one), because on that subject the scan WAS the cost — the
alternation's first bytes stopped on nearly every word of prose, and this train's per-branch block filters
removed those stops (`CHANGELOG.md`).

The instrument was null-calibrated before this reading rather than trusted: `collect_pair` alternates
subject and reference within each of 40 samples and takes the median of per-sample ratios, but always in
the same order, so a position bias would not cancel. Pointing both sides at the SAME operation — true ratio
exactly 1.0 — five times gave medians of 0.9958, 1.0006, 0.9994, 0.9981 and 1.0010: no consistent sign, and
under 0.4 % at the median. The paired design also absorbs machine state that makes `bench_layout.py`
unusable on a loaded host, which is why this table is measurable where §A's floors are not.

**⚠ `sub · dates with refs` is not stable at this precision.** The three published runs read 94.27× /
49.61× / 44.97×, a spread as wide as at the last five stamps. The median of the published runs is shown, and
the disagreement is stated rather than hidden behind a bootstrap interval that this row's own re-runs
contradict. Every other cell holds inside 4.2 % across the three runs. Against the last stamp two days
earlier, every ratio is within 4.5 % but `date · findall groups`, 565.32× → 613.21×.

On the fuzzed corpus (`benchmarks/fuzz_bench.py`, 2886 comparable cases; not re-run at this stamp, the figures are the `2026.9.7` stamp's): aggregate wall time **REAL 3.9–4.0 ms
vs `re` 45.6 s (~12 000×)** over three runs, and `re` hit **85 catastrophic blow-ups where REAL stayed
linear**. REAL's aggregate was 241 ms at the last stamp: the tail of cases where it paid the VM once per match of a
pattern that can match empty, or rescanned a run once per candidate, is the part of this train that shows most
here. Honest detail from the same runs: on the *median* fuzz case — a tiny subject where nothing amortises —
the per-op ratio is **0.85–0.88×** (0.44× at the last stamp), so REAL is still slightly slower there;
the aggregate win is the tail `re` cannot survive. Bounded-lookahead throughput is flat at ~20 MB/s from
1 KB to 1 MB (linear fit R² = 1.0000 — O(n), not backtracking).

### finditer memory — lazy iteration

`Pattern.finditer` yields one `Match` at a time (an internal lazy iterator over the
C++ match cursor), so iterating it holds **O(1)** matches live, against **O(n)** for
materialising them. Peak Python allocation (`tracemalloc`, `benchmarks/finditer_memory.py`):

| matches | lazy iteration | `list(finditer)` |
| ------: | -------------: | ---------------: |
|  50 000 |       ~0.5 KiB |         ~2.7 MiB |
| 200 000 |       ~0.5 KiB |          ~11 MiB |

(`tracemalloc` counts only Python-level allocations; each `Match` also owns C++ span
vectors, so the eager footprint is larger still.) `findall` stays eager — returning a
list is its contract.

### Threaded single-shot matching — GIL release

`match` / `fullmatch` / `search` release the GIL around the core VM scan so threads
can match in parallel (with the GIL held, throughput was flat at 1.00× across
threads — the GIL was the bottleneck). The scan uses **per-call local scratch**, so
it is reentrant by construction; the previous shared `pat->scratch` fields were
**removed**. The GIL is released **only when the subject is ≥ 512 B** — below that the
thread-state save/restore costs more than the sub-microsecond scan.

Throughput (searches/sec), pattern `\w+@\w+\.\w+`, `benchmarks/gil_throughput.py`:

| subject       | 1 thread |    2T |    4T |        8T |
| ------------- | -------: | ----: | ----: | --------: |
| tiny (16 B)   |   1.61 M | 0.99× | 0.99× |     1.00× |
| medium (1 KB) |   38.3 K | 1.90× | 3.29× |     2.82× |
| large (64 KB) |      599 | 1.91× | 3.45× | **4.15×** |

Real (≥ 512 B) subjects scale **3–4×** at 8 threads. Honest trade-off: single-thread
tiny-subject throughput dropped (~3.07 M → ~1.61 M searches/sec) — the per-call
scratch allocation, which a 16 B sub-microsecond scan can't amortise. That is a pure
micro-benchmark (a tight loop of single matches on a 16 B string); real workloads use
larger subjects (which scale) or `findall`/`finditer` (unaffected), so it is accepted.
A thread-local *warm* scratch would remove it but is unsafe here — `pike_vm` caches
the class table by per-program class index, so a state reused across patterns returns
wrong results — hence per-call scratch.

### Threaded findall / split — GIL release (two-phase)

`findall` and `split` also release the GIL, but in two phases: a first pass walks the
matches with the GIL **released** and records each match's byte spans into a flat buffer
(reentrant — the match iterator owns its VM scratch and only reads the immutable
program); a second pass builds the Python objects with the GIL **held**. That second
pass is the catch — it allocates **O(matches)** Python objects under the GIL, a *serial*
tail that both caps scaling (~2× for fast-scanning patterns) and, on small match-dense
subjects, lets the frequent per-call GIL toggling *regress* multi-thread throughput. So
the release threshold here is **4 KB**, not the 512 B of single-shot matching. Below it
the interleaved scan runs under the held GIL, byte-identical to before.

Throughput (calls/sec), `benchmarks/gil_throughput.py` — `findall` `\w+`:

| subject           | 1 thread |    2T |    4T |    8T |
| ----------------- | -------: | ----: | ----: | ----: |
| 1 KB  (GIL kept)  |   93.2 K | 1.01× | 1.00× | 1.01× |
| 16 KB (two-phase) |   6.45 K | 1.85× | 1.85× | 1.82× |
| 64 KB (two-phase) |   1.60 K | 1.95× | 1.99× | 2.04× |

`split` `\s+`:

| subject           | 1 thread |    2T |    4T |    8T |
| ----------------- | -------: | ----: | ----: | ----: |
| 1 KB  (GIL kept)  |   94.0 K | 1.01× | 1.00× | 1.00× |
| 16 KB (two-phase) |   5.92 K | 1.85× | 1.94× | 1.88× |
| 64 KB (two-phase) |   1.59 K | 1.97× | 1.98× | 1.95× |

**Reading.** Single-thread is within noise of the pre-change path at every size (the
offset buffer and one GIL toggle cost nothing measurable next to the object building).
Multi-thread scales to a **build-bound ~2× ceiling** for these fast-scanning patterns —
this is **not** linear scaling: the GIL-held build is an irreducible serial fraction
(Amdahl), so more threads cannot push these patterns past ~2×. Patterns that scan more per
match, e.g. `.` (one codepoint per match), scale further (~4× at 8 threads on ≥ 32 KB)
because their parallel scan is a larger share of the call. Sub-4 KB subjects stay flat **by
design**: the threshold keeps them on the serial path rather than paying toggle
contention for no gain.

**Cross-platform.** The threshold is measured, not arbitrary, and the regression it
guards is specific to macOS / Apple Silicon: *forcing* the release at 1 KB there drops a
fast-scanning pattern to **0.85× at 4 threads** (the `PyEval_SaveThread`/`RestoreThread`
toggle is dear — P/E cores + QoS under oversubscription — and dwarfs the sub-millisecond
call). That is the regression 4 KB avoids. On Intel/Linux (i5-4590T Haswell, 2 cores) the
toggle is cheap enough that releasing pays from **0.5 KB** (~1.7×) with **no regression at
any size**, plateauing near **1.9×** (~95 % of 2 cores). So 4 KB is the
cross-platform-*robust* value — the max of the two platforms' requirements — and doubles
as a macOS anti-regression guard rather than a single-machine artifact: it never hurts on
either platform, and the gain it forgoes below 4 KB is sub-millisecond. A per-`#if __APPLE__`
split (4 KB on macOS, 512 B elsewhere) is measured-sound but deliberately not taken — one
honest constant beats a platform fork for a sub-millisecond knob.

Transient memory: the collect phase holds **O(matches × (groups + 1))** byte offsets
(8 B each) for the call's duration — small, but not zero. `findall`/`split` already
return O(matches) Python objects, so the order is unchanged; `finditer` stays lazy
(O(1) memory) on the interleaved path and is deliberately left untouched.

## C. ReDoS safety — the headline property

The classic catastrophic pattern needs **two honest legs**. `(a+)+b` over `"a"×N`
(no `b`) is rejected by REAL's **required-literal prefilter** (memchr of `b`) — that
shows how fast the common ReDoS shape dies. The **guarantee** is the bare VM on
`(a+)+` (no required literal to short-circuit): still **linear** in N.

**Scope of this re-measure:** arm64, Apple clang, `-O3`, matching-only
after compile, median of 31 (3 consistent rounds). §A/§Unicode are now re-stamped at
`2026.7.55`; this section's own two-leg numbers were measured at `2026.7.51` and are
unchanged by that train (neither leg touches the bare-VM or prefilter paths timed here). x86-64 cross-check (same method): prefilter
~1.2 µs / bare VM ~10.6 ms at N=100K. Prefilter leg is also what `make bench-engines`
emits under `redos`.

| engine / path | input | time |
| --- | --- | ---: |
| REAL `(a+)+b` (literal prefilter, no `b`) | N = 100 000 | **~2.1 µs** (reject; ~2 µs best) |
| REAL `(a+)+` (bare VM, no required literal) | N = 100 000 | **~4.9 ms** (linear) |
| REAL `(a+)+` (bare VM, linearity check) | N = 1 000 000 | **~49 ms** (≈10× → linear) |
| RE2 `(a+)+b` | N = 100 000 | ~0.22 ms (linear; this harness) |
| RE2 `^(a+)+$` | N = 100 000 | **~0.22 ms** (linear; the resistant shape) |
| `std::regex` (libstdc++) | N = 26 | 4107 ms (backtracks; libc++ instead *refuses* from N = 13 — see the sweep below) |
| Python `re` | n = 24 | 1397.76 ms (and climbing exponentially) |

REAL and RE2 stay linear; the backtracking engines (`std::regex`, `re`) either refuse
or blow up at trivially small inputs.

**The `^(a+)+$` row for RE2 was missing until now, and adding it cost REAL a claim.** This section's
own text calls that shape the distinguishing one — anchored at both ends with a breaking suffix,
there is no required literal to prefilter and nothing to auto-possessify — yet `make bench-engines`
asked it only of REAL and PCRE2. Asked of all four, at N = 100 000:

| engine | `(a+)+b` | `^(a+)+$` |
| --- | ---: | ---: |
| REAL | 0.003 ms | **4.00 ms** |
| RE2 | 0.220 ms | **0.220 ms** |
| PCRE2-JIT | 0.005 ms | refused (catastrophic backtracking) |
| `std::regex` | see below — refuses from N = 13 | same |

**`std::regex` is now measured straddling its cutoff rather than above it**, because three rows all
reading "refused" show the refusal and none of the curve. Swept N = 8…26 on libc++, the time
**doubles per added character** and then the implementation refuses outright — on a complexity
counter, not a clock:

| N | 8 | 10 | 12 | 13 | 26 |
| --- | ---: | ---: | ---: | ---: | ---: |
| `(a+)+b` | 0.068 ms | 0.275 | 1.115 | refused | refused |
| `^(a+)+$` | 0.041 ms | 0.157 | 0.630 | refused | refused |

Each step of two characters is ×4.0, to within a percent, on both shapes. That is the property this
whole section exists to contrast: REAL crosses 100 000 characters — nearly four orders of magnitude
more input — in 4 ms, on the shape where PCRE2 gives up entirely.

So the safety claim gets STRONGER — REAL and RE2 are the only two engines linear on both shapes, and
that is now measured for all four rather than asserted for two — while the throughput claim on this
row gets weaker: **RE2 answers the resistant shape 18× faster than REAL** (0.22 ms against 4.00), and
the incomplete table was hiding that. Both engines are linear; RE2's constant on a nested-quantifier
NFA is far better, which is its lazy DFA against REAL's thread list. Recorded as a gap, not an
asterisk.

A second defect of the same kind was fixed in the harness while adding those cells: the `std::regex`
rows fed BOTH shapes the same subject, so whichever shape that subject happened to satisfy answered
in microseconds and was reported as if it had survived backtracking. Each shape now gets the subject
that defeats it — `"a"×N` for `(a+)+b`, `"a"×N + "b"` for `^(a+)+$` — which is the pairing the large
subjects already used. The prefilter makes the classic demo *faster*
than older docs claimed (~0.52 ms was a stale figure that measured neither leg); the
bare-VM row is the guarantee without that short-circuit. This is the property REAL
is built to guarantee.

### PCRE2-JIT — the engine this table used to omit

PCRE2-JIT is REAL's main throughput competitor everywhere else in this document (see the Unicode
table below, where it leads the property/script band), and it was absent from exactly the section
that states REAL's headline property. That asymmetry is corrected here, and the answer is not the
one the classic demo implies.

**`(a+)+b` does not discriminate against PCRE2 at all.** It answers in microseconds at N = 100 000,
because two of its optimisations apply: `a` and `b` are disjoint, so `(a+)+` is auto-possessified
and the ambiguity disappears, and `b` is a required literal it scans for first — the same
short-circuit REAL's own prefilter uses. Any comparison built on this pattern flatters both engines
for their optimisers rather than testing their guarantees.

The shape that does discriminate removes both: anchored at each end, no required literal to scan
for, and a subject that fails only at the last byte. `^(a+)+$` over `"a"×N + "b"`:

| N | REAL | PCRE2-JIT (10.47) |
| ---: | ---: | :--- |
| 16 | 0.060 ms | 0.305 ms |
| 20 | 0.002 | 4.428 |
| 22 | 0.001 | 18.131 |
| 24 | 0.001 | **refused** — `PCRE2_ERROR_MATCHLIMIT` at ~21.8 ms |
| 100 000 | **4.047 ms** | **refused** |
| 1 000 000 | **40.110 ms** (10× for 10× input — linear) | **refused** |

`^(a\|aa)+$` on the same subjects traces the curve more finely before the limit bites — 0.014, 0.086,
0.220, 0.575, 1.494, 3.918, 10.240, 27.053 ms at N = 16…32, roughly ×2.6 every two characters, then
refused at N = 34.

**So PCRE2-JIT does backtrack exponentially, and its default `match_limit` converts the blow-up into
a refusal at ~20–30 ms rather than a hang.** That is materially better than `std::regex`, which
spends 4107 ms at N = 26 and keeps climbing. It is not the same thing as an answer: the caller gets
`PCRE2_ERROR_MATCHLIMIT`, a negative return code that is neither "match" nor "no match", and code
that treats any negative rc as "no match" — a common shape — silently accepts a non-answer as a
negative result. REAL returns the correct no-match at every N, in time linear in the input.

Reproduce with `make bench-engines` (the `redos` block now carries both patterns and a `pattern`
field per row; PCRE2 rows appear when `pkg-config --exists libpcre2-8`).

## D. real::dfa — capture-free maximal-munch DFA (opt-in)

`real::dfa` (`<real/dfa.hpp>`, opt-in — not pulled in by `<real/real.hpp>`) fuses a set
of patterns into one DFA that recognizes the winning rule (longest match; ties to the
earliest pattern; empty excluded) in a single pass — the rule dispatch a lexer wants.
It is built once at run time, then immutable. It is not timed here in isolation; as a
lexer's per-mode dispatch it runs ≈20× the per-rule scan on a rule set where many rules
share leading bytes (measured in SciLex, which consumes it through `dfa_modes`).
Patterns with a zero-width assertion no DFA can represent throw `real::dfa_error`.

## E. REAL vs the rust `regex` crate

The rust `regex` crate (a lazy-DFA engine with literal prefilters) is REAL's closest peer on the
linear-time-guarantee axis. Same patterns, same corpora (~1 MB), best of 15, match counts equal.
REAL `count_matches` vs rust `find_iter` (spans) — scan cost, no capture-slot fill on either side.
Ratio is `rust_ns/REAL_ns` (> 1 means REAL is faster), from `run_duel.py --json`, re-checked by
`benchmarks/verify_bench_ratios.py`. rust `regex` 1.12.4, both ISAs, `-O3 -flto`. Reproduce:
`make bench-duel`.

**arm64** — Apple clang 16, `-O3 -flto`:

| case | REAL ns/B | rust ns/B | winner |
| --- | ---: | ---: | :--- |
| literal `dog` | 0.256 | 0.596 | **REAL 2.3×** |
| alternation `fox\|dog\|cat` | 0.776 | 1.369 | **REAL 1.8×** |
| class `[a-z]+` | 1.168 | 12.252 | **REAL 10.5×** |
| digits `[0-9]+` | 1.390 | 17.523 | **REAL 12.6×** |
| fields `[^,]+` | 2.201 | 9.401 | **REAL 4.3×** |
| word-boundary `\b\w+\b` | 1.720 | 11.347 | **REAL 6.6×** |
| email `(\w+)@(\w+)` | 1.695 | 5.273 | **REAL 3.1×** |
| ident `(\w+)_(\w+)` | 6.430 | 30.508 | **REAL 4.7×** |
| date no-match `\d{4}-\d{2}-\d{2}` | 0.012 | 0.012 | tie 1.0× |
| date sparse `\d{4}-\d{2}-\d{2}` | 0.045 | 0.074 | **REAL 1.6×** |
| email sparse `(\w+)@(\w+)` | 0.051 | 0.123 | **REAL 2.4×** |
| key= `key=(\w+)` | 0.961 | 1.435 | **REAL 1.5×** |

**x86-64** — g++ 15.3.1, `-O3 -flto` (§A's host, minimum of six runs):

| case | REAL ns/B | rust ns/B | winner |
| --- | ---: | ---: | :--- |
| literal `dog` | 0.275 | 0.644 | **REAL 2.3×** |
| alternation `fox\|dog\|cat` | 0.857 | 1.643 | **REAL 1.9×** |
| class `[a-z]+` | 1.388 | 15.465 | **REAL 11.1×** |
| digits `[0-9]+` | 1.503 | 18.558 | **REAL 12.3×** |
| fields `[^,]+` | 2.127 | 12.870 | **REAL 6.1×** |
| word-boundary `\b\w+\b` | 1.885 | 13.591 | **REAL 7.2×** |
| email `(\w+)@(\w+)` | 1.653 | 5.073 | **REAL 3.1×** |
| ident `(\w+)_(\w+)` | 6.310 | 35.770 | **REAL 5.7×** |
| date no-match `\d{4}-\d{2}-\d{2}` | 0.015 | 0.015 | tie 1.0× |
| date sparse `\d{4}-\d{2}-\d{2}` | 0.064 | 0.084 | **REAL 1.3×** |
| email sparse `(\w+)@(\w+)` | 0.056 | 0.126 | **REAL 2.2×** |
| key= `key=(\w+)` | 0.896 | 1.710 | **REAL 1.9×** |

<!-- Keep this marker: docs/site/drop-in/regex.md slices from it, and anchoring that include on
     wording instead broke the site build once when a re-stamp rewrote the sentence below. The
     marker must stay the LAST line of this comment -- the extractor resumes at the next newline. -->
<!-- [duel-reading] -->

**Reading.** REAL leads 11 of 12 rows on both ISAs. The twelfth, `date no-match`, is a tie on both (0.012 ns/B
each side on arm64, 0.015 on x86-64): both engines cross 1 MB in under 20 µs without a match, so the ratio is
on work that has already collapsed. These rows are `find_iter`. Against the last stamp: arm64 `email` +7.7 %,
`ident` +5.8 % and `email sparse` +6.2 %, which a layout-robust A/B of the same three shapes through `find_iter`
in a REAL-only unit reads indistinguishable from layout (+0.1 to +0.7 %, Apple clang, arm64); they read within
1 % one commit earlier, and that commit moves them by under 0.4 % in instructions (clang 19, `-O3 -flto`). Every
other arm64 row within 3 %. x86-64 `alternation` −13 %;
`word-bound` +14 %, `digits` +8 %, `literal` +9 % and `class` +6 % moved while g++ 15 at `-O3 -flto` counts
every one of those rows 1.9 to 4.8 % FEWER instructions than at the last stamp's tree: this binary's placement,
on a host whose duel leg is known to be bimodal. `ident` and `email` still carry what right groups cost since
`2026.10.2` (`CHANGELOG.md`). x86-64 cells are minima of six runs.
Capture apples-to-apples is §E.3; the tables above are span/count-only.

### E.1 The lazy-DFA arc: what it bought, and the gap that remains

The `kFirstMatch` forward DFA and its reverse start-finder now route eligible searches — byte and Unicode
`\w \d \s` patterns, no assertions or lookarounds — through a two-pass scheme: the DFAs locate the match
span capture-free, and the Pike VM runs only on that window for the groups. First, against REAL's *own*
pre-arc Pike VM, on `(\w+)@(\w+)` (default flags, 1 MB, best of 12):

| subject | REAL routed | REAL pure Pike | the arc bought |
| --- | ---: | ---: | :--- |
| no-match (no `@`) | 5.6 | 39.4 | **7.0×** |
| sparse (rare `@`) | 6.5 | 39.4 | **6.0×** |
| dense (every token an email) | 34.3 | 44.9 | 1.3× |

No-match and sparse subjects — the common shape in validation and log scanning — get a real 6–7×: the DFA
rejects or skips at ~5–6 ns/B where the VM ground at ~40. The dense subject barely moves, and the
three-column comparison against rust says why (same `(\w+)@(\w+)`, rust `captures_iter` touches every group):

| subject | REAL routed | rust `find_iter` (spans) | rust `captures_iter` (apples-to-apples) |
| --- | ---: | ---: | ---: |
| no-match | 5.6 | 0.013 | 0.013 |
| sparse | 6.5 | 0.11 | 0.29 |
| dense | 33.4 | 2.25 | 6.95 |

rust's `find_iter` answers *spans* without running its capture machinery — the asymmetry §E flagged — but
its `captures_iter`, groups fully extracted, is the honest comparable, and it is still 4.8× ahead on the
dense row (and far more on the sparse and no-match ones). Two mechanisms REAL has not built account for the
survivors, both named follow-ups rather than mysteries:

- **Dense extraction.** rust writes capture slots in a single deterministic pass — a *one-pass* engine, for
  patterns whose state×byte transition is unique, and `(\w+)@(\w+)` qualifies — instead of the Pike thread
  lists REAL's windowed pass still runs. **The dense-extraction floor is the span extractor, not the DFA:**
  a one-pass-style extractor on the already-located window is the identified follow-up.
- **No-match / sparse.** rust `memchr`es the required inner literal `@` — at a *variable* offset, which
  REAL's fixed-offset rare-byte hint cannot cover — and verifies around each hit, never scanning the
  non-matching stretches. A variable-offset inner-literal prefilter is the other identified follow-up.

The arc closed the gap between REAL and its own VM on the no-match/sparse axis; it did not close the gap to
rust's multi-engine architecture, and the two engines that would are named here, not implied.

Reproduce: `benchmarks/duel/` — a rust binary (`regex = "1"`, pinned in its `Cargo.lock`; `find`/`captures`
modes), a REAL binary, and `run_duel.py` (the corpus and the §E table). Dev-tooling; not wired into any gate.

### E.2 The one-pass arc: engine parity, and the machinery that now bounds it

§E.1's first follow-up was a one-pass capture extractor. It is built, and the arc is complete. A pattern is
*one-pass* when at most one thread crosses any byte (RE2's `onepass.cc`); its captures then fill in a single
left-to-right pass with no thread lists. A deterministic UTF-8 trie made the default-flags Unicode `\w \d \s`
one-pass (they were not before — a naive range alternation shared lead bytes), so the flagship `(\w+)@(\w+)`
qualifies, and the router fills its captures with the one-pass pass instead of the windowed Pike VM.

The result on the flagship dense find_iter — the whole `(\w+)@(\w+)` loop, groups fully extracted:

| flagship dense find_iter | ns/B | vs. the §E.1 window-Pike |
| --- | ---: | ---: |
| window-Pike baseline (§E.1) | 33.4 | 1× |
| **one-pass arc, final** | **8.0** | **4.2×** |
| rust `captures_iter` (apples-to-apples) | 7.0 | REAL is **1.14×** |

The no-match scan on the same pattern is **2.9 ns/B**. §E.1 measured this line at 4.8× behind rust's
`captures_iter`; it is now within **1.14× — engine parity.** Four findings, in the order the attribution
surfaced them (each measured before it was fixed — the profiler moved the target every time):

1. **The extractor itself** took the matching *core* to parity: the one-pass pass replaced the windowed Pike
   VM on its own line (~19 → ~2.5 ns/B). The core (locate `[s,e]` + extract) is ~6.7 ns/B, at rust's whole
   `captures_iter`. The engine stopped being the gap here; the rest was machinery REAL redid per iteration.
2. **The immutable machinery was rebuilt per find_iter** — the byte-program, the one-pass table, and the
   byte-class alphabet, each derived afresh for every iterator. They moved into a per-regex cache built once
   under `std::call_once` (thread-safe, ThreadSanitizer-verified on a shared regex; the mutable DFA caches
   stay per-iterator). A **Moore partition refinement** shrinks the one-pass table where the byte-trie's
   sharing was lost in the flood-fill (the flagship: 2508 → 660 nodes, 6.3 → 1.8 MB) with no throughput cost.
3. **The DFA transition tables were nested vectors** (two loads per byte). Flattened to `state*stride + class`
   (one load) they took the scan itself down, and the no-match line with it (3.6 → 2.7).
4. **The find_iter dispatch repeated per-match invariants** — a VM that copied the program view every advance,
   a `call_once` load on the hot path, a result re-binding its unchanged context. Setting each once took the
   last stretch to ~8.0.

**Anchored matching (Tier B).** The direct `match` / `fullmatch` entry points — and, through the std-compat
layer, `real::compat::regex_match` (the `std::regex` drop-in) — now route through the one-pass pass too, for
assertion-bearing patterns as well: `^ $ \b \B \A \Z` become edge conditions the extractor evaluates
(Brüggemann-Klein's one-unambiguous automata carry empty-width conditions on their transitions). A one-pass
`regex_match` with submatches — `(\w+)@(\w+)`, `^(\w+)$`, `\b(\w+)\b` — extracts **4–5× faster** than the
Pike VM it replaces. The one exception, deliberately declined to the VM (never wrong), is two edges on one
byte-class whose assertion masks differ (`\bx|\Bx` — RE2's `kImpossible` refinement), a bounded follow-up.

**Honesty.** One-pass touches neither the sparse nor the no-match-prefilter gap — those remain §E.1's
inner-literal-prefilter follow-up, and the residual find_iter dispatch (the dense line straddles 8.0 rather
than clearing it; ~1 ns/B of per-match iterator overhead, shared by every pattern) is the other named
follow-up. The arc closed the extractor gap §E.1 named, brought the dense-capture line to rust parity, and
carried anchored matching — the std-compat surface — onto the same one-pass path.

### E.3 The broader duel (apples-to-apples, both engines extracting captures)

`make bench-duel` runs the same patterns through both engines with **rust in `captures_iter` mode** — the
fair comparison, since REAL's `find_iter` always builds the full Match. (An earlier harness timed rust's
`find_iter`, spans only, which under-charged rust and flattered its lead; fixed.) The corrected picture, on
`2026.7.16`:

| pattern | REAL ns/B | rust ns/B | |
| --- | ---: | ---: | --- |
| `[a-z]+` | 2.9 | 15.5 | **REAL 5.3×** |
| `[0-9]+` | 3.6 | 22.4 | **REAL 6.1×** |
| `[^,]+` | 3.7 | 12.2 | **REAL 3.3×** |
| `(\w+)@(\w+)` | 9.5 | 7.0 | rust 1.4× (≈ the §E.2 parity) |
| literal / alternation | | | rust 1.1–1.3× |
| `\b\w+\b` search · `(\w+)_(\w+)` · date no-match | | | rust 3.6× / 2.1× / 208× |

REAL **beats rust's `captures_iter`** on the capture-dense class/digit/field scans — rust pays a per-match
capture cost too. It trails only where already named: the assertion-bearing `\b\w+\b` *search* (its
byte-program has assertions, so the search DFA declines and the Pike VM runs the window), the non-one-pass
`(\w+)_(\w+)` (the `_` conflict), and the no-match date (the inner-literal-prefilter follow-up).

### E.4 The `real-regex` crate, measured natively (criterion)

§E.1–E.3 time the REAL **engine** (C++, in `real_bench`) against rust. `make rust-bench` (criterion, in
`bindings/rust/benches/`) times the published **crate** against the `regex` crate — both in-process Rust, so
this pair has no cross-process FFI asymmetry to correct for. It measures the wrapper's own cost, in two
operations:

- **`find_iter`** (whole-match spans) — after the wrapper's cursor was made to reuse one span buffer and take
  a span-0-only path that materializes no group vector, the crate is now **at parity-to-faster** than the
  pure-Rust `regex` crate: `[a-z]+` ≈ 0.86×, `[0-9]+` ≈ 0.67×, `[^,]+` ≈ 0.23× (REAL ahead). The per-match
  allocation an earlier measurement flagged here is gone.
- **`captures_iter`** (materializing every group into an owned `Captures`) — **no longer allocates per
  match at all.** `Captures` now stores its slots flat and inline (4 groups inline, spilling to the heap
  beyond), so the `malloc` + `free` this line used to pay per match — ~19–27 ns/match of pure allocator
  traffic to carry a *single* span on a groupless pattern — is gone. That was the whole of the
  `captures_iter`-versus-`find_iter` gap, and the older « inherent to returning owned group spans »
  wording is withdrawn twice over: the `regex` crate solves it with `capture_locations` +
  `captures_read` (**`real-regex` exposes the same pair**, plus a streaming `captures_read_iter`), and
  the owned iterator no longer needs the escape hatch to be competitive.

  A later train found one more per-match cost hiding in the same place, and it was not an allocation:
  `SlotStore::from_flat` copied the slot run with `copy_from_slice`, whose **runtime length compiles to a
  `memcpy` call** — to move two `usize` in the shape that dominates, a groupless pattern's group 0. Two plain
  stores replace it. Ablation apportioned that line exactly: of the 171 µs by which `captures_iter` trailed
  `find_iter` on `\b\w+\b`, **114 µs was this one call** and 57 µs the `Captures` object's size, `Drop` glue
  and `Arc` traffic together. The same fault is what the C ABI's own comment forbids ("pairwise specifically,
  NOT one memcpy") — it had survived on the Rust side.

  **arm64, criterion, 64 KiB corpus** — two trains' effect on this bench, against the `regex` crate in
  the same process:

  | criterion row | v2026.7.56 (`9c400e1`) | now (`35cd546`) | vs `regex` now |
  | --- | ---: | ---: | :--- |
  | `find/email` | 132.36 µs | **46.20** | 3.06× behind → **1.07× behind** |
  | `captures/email` | 140.75 | **52.10** | 3.25× behind → **1.21× behind** |
  | `find/word_bound` | 327.05 | **318.80** | 2.42× behind (was 2.48×) |
  | `find/class` | 160.07 | 161.95 | **REAL 1.16× ahead** |
  | `find/digits` | 50.88 | 50.03 | **REAL 1.52× ahead** |
  | `find/fields` | 36.35 | 36.29 | **REAL 4.77× ahead** |
  | `find/literal` | 16.29 | 16.29 | 1.55× behind |
  | `captures/class` | 186.20 | 180.95 | **REAL 1.03× ahead** |
  | `captures/digits` | 55.35 | 54.31 | **REAL 1.41× ahead** |
  | `captures/fields` | 40.49 | 39.66 | **REAL 4.35× ahead** |
  | `captures/literal` | 19.34 | 18.97 | 1.77× behind |

  **`email` is the row this train was about, and it was the one family where `regex` led on both
  operations.** `(\w+)@(\w+)` is `class+ <literal> class+`: the prefilter now places the match start by
  walking the prefix class back from the `@` and confirms by walking the suffix class forward, so a
  confirmed candidate costs two class walks instead of a reverse DFA plus a one-pass extraction — **−65.1 %
  on `find`, −63.0 % on `captures`**, from 3.06× behind to 1.07×. Every other row moves by at most 2.8 %.

  **Protocol, because one number here is context-dependent:** these are interleaved A/B runs — the same
  bench file built against both trees, one criterion group at a time, alternating, two rounds. Run inside
  the FULL suite instead, `find/word_bound` reads 409 µs rather than 319 on the same binary: its absolute
  value depends on what else ran in the process (thermal and cache state), so only a like-for-like
  comparison means anything on that row. The 7.55 → 7.56 column this table used to carry is dropped rather
  than re-derived, since it was not measured under this protocol.

  `captures_read_iter` remains the right tool when the last allocation matters (it materializes no
  `Captures` at all): dense ~100 KB, med of 21, `[a-z]+` **≈0.43×** the wall of `captures_iter`,
  `(\w+) (\w+) (\w+) (\w+)` **≈0.93×** (search-dominated).

- **`compile` and `first_use`** — two groups the scan rows structurally could not see. `find`/`captures`
  build the pattern *outside* the timed closure, and criterion's warm-up absorbs anything the engine builds
  lazily on the first match attempt. Two costs lived in that gap and neither was hypothetical: a quadratic
  Unicode word-subset test cost `\b\w+\b` **105 µs at compile**, and the one-pass table for `(\w+)@(\w+)`
  cost **21.3 ms on first use**. Both are fixed; both were invisible to every row above. `compile` times
  `Regex::new` alone; `first_use` times a *fresh* `Regex::new` plus one short search, so a lazy build is paid
  inside the closure. `first_use` deliberately includes `compile` rather than subtracting it — two medians
  measured separately and subtracted is an estimate carrying both error bars, not a measurement.

  **arm64, criterion, at `9c400e1`.** Ratio is `regex` ÷ REAL, so **> 1 means REAL is ahead**:

  | case | `compile` REAL | `regex` | ratio | `first_use` REAL | `regex` | ratio |
  | --- | ---: | ---: | :--- | ---: | ---: | :--- |
  | `dog` | 530 ns | 1.40 µs | **2.65×** | 825 ns | 1.46 µs | **1.77×** |
  | `[a-z]+` | 643 ns | 4.81 µs | **7.49×** | 1.05 µs | 7.81 µs | **7.41×** |
  | `[0-9]+` | 647 ns | 24.3 µs | **37.6×** | 1.06 µs | 27.3 µs | **25.7×** |
  | `fox\|dog\|cat` | 850 ns | 17.7 µs | **20.8×** | 1.15 µs | 17.8 µs | **15.5×** |
  | `[^,]+` | 2.44 µs | 19.5 µs | **7.98×** | 2.87 µs | 23.1 µs | **8.05×** |
  | `\d{4}-\d{2}-\d{2}` | 5.39 µs | 157 µs | **29.1×** | 5.69 µs | 162 µs | **28.4×** |
  | `\b\w+\b` | 7.12 µs | 238 µs | **33.4×** | 7.56 µs | 267 µs | **35.3×** |
  | `(\w+)@(\w+)` | 11.2 µs | 559 µs | **49.7×** | **2.91 ms** | 589 µs | **0.20× — REAL 4.94× behind** |

  REAL was ahead on **8 of 8** compile rows and **7 of 8** first-use rows, the exception being
  `(\w+)@(\w+)`: **2.91 ms** on first use to build its one-pass capture extractor, against 589 µs for the
  whole of `regex`'s eager work. That was already down from **21.3 ms** (34.9× behind) over five passes —
  flat scratch, sparse signatures, one interned class per byte range, jump-chain resolution in the flood
  (which alone made the flood land *on* the minimal automaton, 660 nodes in and 660 out where it was 2508
  in), and dropping a duplicate Tier-A/Tier-B expansion — and it later reached **2.64× behind**, but behind
  is behind. Declining the table instead is not the answer and was measured: it buys **3.3×** on the scan
  (`find` 135 µs against 443 on a 64 KiB corpus), with a break-even near 900 KB.

  **That row is now at parity, and it took a sixth pass to see why.** The profile said the cost was not the
  table's construction but the DECISION to construct it: `ensure_immutables` built the extractor alongside
  the byte program, so every route needing only the cheap half paid for the expensive one. The measurement
  that settled it was the capture-free twin — `\w+@\w+` has 2 slots, no capture for an extractor to fill,
  and cost the same 1487 µs. Built only where captures are actually extracted through it, a first search
  drops **1490 → 573 µs on arm64 and 1958 → 813 on x86-64** (direct harness, both platforms), and
  `\d{4}-\d{2}-\d{2}` follows it from 296 to 167 — six `\d` occurrences, and a no-match scan never
  extracts. On this bench, in one run with both engines in the same process:

  | criterion row | REAL | `regex` | |
  | --- | ---: | ---: | :--- |
  | `first_use/email` | 604.53 µs | 601.81 | **parity** — CIs overlap ([595.8, 615.4] vs [593.5, 612.5]) |
  | `first_use/word_bound` | 7.73 | 269.85 | REAL **34.9× ahead** |
  | `first_use/no_match` | 5.97 | 170.87 | REAL **28.6× ahead** |
  | `find/word_bound` | 227.69 | 176.81 | **1.29× behind**, from 2.42× |
  | `captures/word_bound` | 295.45 | 179.80 | **1.64× behind**, from 1.82× |

  **`word_bound` was the largest remaining deficit, and profiling refuted the obvious reading of it.**
  The boundaries cost nothing — `\b\w+\b` measured 247.1 µs against `\w+`'s 247.7 on this corpus, so
  the B-1 optimisation that drops a redundant `\b` from a maximal run leaves nothing to win there. The
  cost was the code-point-class scan, isolated on identical match sets: `\w+` 247.7 µs against
  `[a-zA-Z0-9_]+` and `(?a)\w+` at 126.9 — **1.95×**, on a corpus with no non-ASCII byte in it. The
  per-byte shape tested the width (`lead < 0x80`) before the ASCII row, two branches per accepted byte
  against a byte-class loop's one; the row is already 256 entries and no code-point class sets a bit at
  or above 0x80, so testing it first is answer-preserving and moves the width test off the accepted-byte
  path. arm64 wall clock `\b\w+\b` **246.9 → 166.9 µs (−32.4 %)**, `(?i)[a-z]+` −31.2 %, `\p{L}+`
  −30.6 %, `\s+` −13.7 %, `\d+` −5.3 %; `[a-z]+`, `[^,]+` and `dog` unmoved. **x86-64 gcc-14 gains far
  less** — instruction counts (the container's wall clock drifts ~5 %, inside which these deltas sit):
  `\w+` 5 430 510 → 5 302 688 (**−2.35 %**), `\d+` −0.40 %, `[a-z]+` byte-identical. That path goes
  through the gcc-specific body where `in_class` delegates to `width`, a shape gcc already compiled
  well, which is why that body exists. Large on arm64, small on x86-64, a regression on neither.

  Parity, not a lead: the intervals overlap, so the defensible claim is that the gap is gone. The scan rows
  were re-measured to confirm the cost did not simply move — `find/email` 46.52 µs (46.20 before),
  `captures/email` 51.98 (52.10), `find/no_match` 1.67 against the crate's 776 ns — all unchanged. Measured
  on the same arm64 host, one group at a time; NOT under the interleaved A/B protocol the table above
  uses, which is why these sit in their own table rather than as a new column in it.

  Read the two families together: REAL's cost is overwhelmingly *eager and small* and `regex`'s is *eager
  and large*. The one place REAL was worse was a **lazy** build that a short-lived pattern paid in full, and
  it was worse because it was built for callers that could not use it.

So on span throughput the crate is competitive; on full capture extraction use the reusable buffer when it
matters. Either way the pitch is not raw speed but the linear-time / ReDoS-safe guarantee and the
**bounded lookarounds `regex` cannot compile at all**, delivered through a `regex`-shaped API. The numbers are
noisy and machine-dependent (criterion reports CIs); reproduce with `make rust-bench`.

### E.5 The inner-literal prefilter (IL.2): closing the biggest gap line

The duel's worst line was a pattern whose match does **not** begin with a literal — the date
`\d{4}-\d{2}-\d{2}`. REAL scanned every position where a digit could start a match; the `regex` crate memmem'd
the rare `-` and skipped the rest. The inner-literal prefilter (`make bench-duel`) gives REAL the same move: it
extracts a required inner literal, scans for it, reverse-matches the prefix to the match start, and confirms
forward. The line went from **201× rust to parity**:

| duel row (find_iter, captures) | REAL ns/B | rust ns/B | ratio |
| --- | --- | --- | --- |
| `\d{4}-\d{2}-\d{2}` — no match | 0.023 | 0.012 | **1.9×** (was ~201×) |
| `\d{4}-\d{2}-\d{2}` — sparse | 0.31 | 0.08 | 4.1× |
| `(\w+)@(\w+)` — dense | 5.5 | 5.4 | 1.0× (parity) |
| `(\w+)@(\w+)` — sparse | 0.82 | 0.12 | 6.7× |
| `key=(\w+)` | 1.15 | 1.44 | **REAL 1.3×** |
| `[a-z]+` · `[0-9]+` · `[^,]+` (regression check) | 2.2 / 2.8 / 3.0 | 12 / 17 / 9 | REAL 5.4× / 6.1× / 3.1× |
| `dog` (literal, regression check) | 0.72 | 0.57 | 1.3× (unchanged) |

The no-match line is the headline: a haystack the literal never appears in now costs a single memmem (the
reverse DFA is built lazily, only on the first candidate), landing at **1.9× rust** — the V0 target of ≤2× met.
Two placements make the rest hold up across the *whole* matrix, not just the flagship. The route sits **after**
the literal / class-loop fast paths, so an exact literal like `dog` keeps its own path (0.72, unchanged). And
the per-candidate confirm reuses the **forward DFA + one-pass extract** (the dense laddering §7.7 built), not a
raw Pike pass — so `(\w+)@(\w+)` dense lands at **parity** (better than before the route, since the reverse
already supplied the start, skipping the reverse DFA) and `key=` at **REAL 1.3×**. The class / digit / field
rows are unchanged (the route only fires for a required inner literal), and the exhaustive corpus confirms it
byte-identical to the core (serious=0 with the route on, 3.21M cases).

## Unicode — comparative

Every cross-engine harness above (§A, §multi-pattern, §E) runs **ASCII-only patterns over ASCII-only
corpora**. After landing full `\p{}` (general category, script, `sc=`/`scx=` Script_Extensions, 63 binary
properties), that was a blind spot: REAL's Unicode throughput had never been measured against anything.
This section fills the gap. **Measurement only** — this is a snapshot of what the numbers say today, not an
optimization pass; a gap found here is a candidate for a future arc, not fixed in this one. *(One exception:
the `(?i)<literal>` finding below was a P0 correctness-adjacent bug, not a throughput gap, and was fixed
same-day — see that subsection.)*

**⚠ The methodological trap, locked down first.** Every engine bundles a different Unicode Character
Database vintage. `\p{L}+` can therefore match a *different set of code points* on different engines —
different work, not just different speed — so a raw throughput ratio can be comparing apples to a
differently-sized bag of oranges. The rule applied throughout this section: **cross-check the match count
per (pattern, corpus, engine) before trusting a ratio.** Where counts diverge, the ratio is marked
approximate and the cause is stated — it is **not always a UCD-version gap**; two of the divergences below
turn out to be an ASCII-vs-Unicode *semantics* difference (a different, more fundamental gap than a stale
data table).

**Stamp.** REAL `2026.10.5` + tree `3c72c9d6`, both ISAs re-measured for this stamp, on the same
runs-per-ISA / minimum-per-cell protocol as §A. **arm64** table below: Apple clang 16, `-O2`, N = 30
(`make bench-engines`). **x86-64**, same harness and N, on §A's host: g++ 15.3.1 with PCRE2 10.47 (the
version the binary LINKED, printed by the harness) and RE2 2025-11-05: `\w+` mixed **2.759** (pcre2 **1.02×**,
re2 **1.14×**), `\p{L}+` CJK **2.877** (0.80× / re2 **4.72×**), `\p{N}+` **3.302** (0.62× / **1.50×**),
`sc=Han` **3.961** (0.93×), `scx=Cyrl` **4.414** (**1.15×**), `(?i)café` **0.533** (0.94× / **3.71×**), `[à-ÿ]+`
**2.493** (**1.99×** / **6.70×**), literal `你好` **0.416** (pcre2 **1.97×**), `.` emoji **1.758** (pcre2
**4.57×**), ascii witness **0.873** (**5.80×**). Against the last stamp, x86-64 every row within 3.1 % but `.`
emoji +8.0 %, which g++ 15 counts at 18 % FEWER instructions (placement); arm64 within 2 % but `scx=Cyrl` +5.3 %
and `[à-ÿ]+` +4.4 %, both indistinguishable from layout in a layout-robust A/B of `6a80b657` → `4eb057e4`
(−0.2 % each, Apple clang). Under GCC those two rows and `sc=Han` count the last stamp's instructions or fewer:
the trailing-lookaround walk's own membership test had cost them up to +9 % before this stamp's tree. Oracle: exhaustive
`\p{L}` over U+0000..10FFFF (surrogates skipped) — **0 mismatch**.

**`(?i)café` was this document's worst ratio, and this train fixed it — after the obvious diagnosis
turned out to be wrong.** The cost was NOT the case-insensitivity: plain `café` used to be *slower*
than `(?i)café` (1.761 vs 1.221 ns/B on arm64), because the fold turns `é` into a code-point class
and that stops the prefilter's fixed-offset walk before it reaches the high bytes. Decomposed on one
corpus at equal match density: pure literal `café` **0.257**, the same set written out as
`caf[éÉ]` **0.700**, `(?i)café` **1.226** — so the dominant step was emitting the fold as a
code-point class. It need not be one: `é` and `É` are `C3 A9` and `C3 89`, one shared lead and one
differing continuation, which is `byte C3` plus a two-member BYTE class — fixed width, no branch. A
non-ASCII class whose members share one length and differ in exactly ONE byte position is now emitted
that way, and the row reads **0.404** on arm64 (0.26× → **0.84×**) and **0.671** on x86-64 (0.29× →
**0.89×**). The condition is narrow on purpose: mixed lengths would need an ALTERNATION, which is the
shape the 7.61 note already refuses and which measures **3.405** here (`caf(é|É)`) against 0.700 for
the class. What it costs elsewhere is in §A's protocol, with both the harness figure and the
real-translation-unit one.

**A separate defect on that path WAS fixed, and it is invisible in these tables by choice.** The
prefilter ranked every byte ≥ 0x80 as the rarest rank it has, so `café` picked as its `memchr` target
the 0xC3 that leads every accented Latin letter — one byte in six of French prose. Against an ASCII
literal of the same match density in the same corpus it ran **6.3× slower** (1.761 vs 0.279 ns/B);
ranked by selectivity instead, it is **0.258**, at parity. There is no row for it because adding one
to `bench_engines.cpp` moves gcc/x86-64 `words` by +27.7 % (see §A's protocol) — the figures here are
from an isolated probe, and are labelled as such rather than published as a table row.
Engine Unicode Character Database versions:

| engine | UCD version |
| --- | --- |
| REAL | 16.0.0 |
| PCRE2 10.47 (UTF+UCP) | 16.0.0 |
| RE2 | ~15.0/15.1 (no runtime query API — empirical bound: compiles `\p{Kawi}` / `\p{Nag_Mundari}`, Unicode 15.0's new scripts; rejects `\p{Todhri}` / `\p{Sunuwar}`, 16.0's) |
| rust `regex` 1.12.4 / `regex-syntax` 0.8.11 | 16.0.0 (accepts both 16.0-new scripts) |
| `std::regex` | n/a — ECMAScript grammar, no `\p{}` support at all |

**Corpora.** Six ~200 KB (bench_engines.cpp) / ~1.1 MB (duel, matching that harness's own N=20000-repetition
convention) reproducible corpora, generated from name-verified code points — never typed as raw glyphs, to
remove any risk of editor-pipeline mojibake. The C++ side resolves each codepoint via Python's
`unicodedata.name()` first, then embeds it as a UTF-8 hex-escape (`benchmarks/bench_engines.cpp`, `corpus_*`
functions); the Python side uses `\N{...}` named escapes directly, which the Python parser itself validates
at import time (`benchmarks/duel/run_duel.py`). Both are committed and deterministic — no external
downloads, no random seeds.

| corpus | content |
| --- | --- |
| cjk | dense Han ("你好世界") + hiragana ("こんにちは") |
| arabic | RTL Arabic letters + all ten Arabic-Indic digits (U+0660–0669) |
| emoji | astral-plane singles (😀🎉👍) + a ZWJ family sequence (👨‍👩‍👧‍👦) |
| mixed-script | Latin + Han + Cyrillic ("Привет") + emoji, interleaved |
| dense-multibyte (latin-accented) | French-style prose, high 2-byte-UTF-8 density (café/résumé/naïve/façade) |
| ascii-témoin | the existing ASCII corpora, reused as the scale reference |

### `bench_engines.cpp` — REAL vs std::regex vs PCRE2-JIT vs RE2

Per-engine `\p{}` support is **auto-detected by attempting the compile**, not hand-classified — a pattern an
engine fails to compile is `unsupported` for that engine, the same way a binding would report it. `(x)` is
*engine_time / REAL_time* — **> 1 means REAL is faster.** `N = 30`, bootstrap CI omitted here for width (see
raw JSON — every ratio's 95% CI is within ±2% of the point estimate); match counts alongside.

| case | REAL ns/B | std::regex | PCRE2-JIT (UTF+UCP) | RE2 | counts (real/std/pcre2/re2) |
| --- | ---: | ---: | ---: | ---: | --- |
| `\w+` (mixed-script) | 2.060 | 59.94 (**29.10×**) | 1.86 (0.90×) | 3.20 (**1.55×**) | 16218/5406/16218/5406 ⚠ |
| `\b\w+\b` (mixed-script) | 1.972 | 57.57 (**29.19×**) | 3.04 (**1.54×**) | 3.95 (**2.00×**) | 16218/5406/16218/5406 ⚠ |
| `\w++` (mixed-script) | 2.061 | unsupported | 1.86 (0.90×) | unsupported | 16218/—/16218/— |
| `\w{2,}` (mixed-script) | 2.607 | 59.60 (**22.86×**) | 1.77 (0.68×) | 3.93 (**1.51×**) | 16218/5406/16218/5406 ⚠ |
| `\p{L}+` (CJK) | 1.932 | unsupported | 1.34 (0.69×) | 13.18 (**6.82×**) | 12904/—/12904/12904 |
| `\p{N}+` (arabic digits) | 1.973 | unsupported | 1.61 (0.82×) | 5.22 (**2.65×**) | 6250/—/6250/6250 |
| `\p{sc=Han}` (CJK) | 2.907 | unsupported | 2.14 (0.74×) | unsupported | 25808/—/25808/— |
| `\p{scx=Cyrl}` (mixed-script) | 3.432 | unsupported | 2.68 (0.78×) | unsupported | 32436/—/32436/— |
| `(?i)café` (accented) | 0.421 | unsupported | 0.34 (0.81×) | 1.31 (**3.11×**) | 3509/—/3509/3509 |
| `[à-ÿ]+` (accented) | 1.673 | 87.67 (**52.40×**) | 2.06 (**1.23×**) | 12.52 (**7.48×**) | 38599/38599/38599/38599 |
| literal `你好` (CJK) | 0.374 | 28.95 (**77.41×**) | 0.59 (**1.58×**) | 2.62 (**7.01×**) | 6452/6452/6452/6452 |
| `.` (emoji, one codepoint) | 1.246 | 58.73 (**47.13×**) | 3.80 (**3.05×**) | 18.92 (**15.18×**) | 68306/200039/68306/68306 ⚠ |
| ascii witness `[a-z]+` | 0.776 | 89.69 (**115.58×**) | 2.25 (**2.90×**) | 13.96 (**17.99×**) | 42108/42108/42108/42108 |

*(This table carried the `\w+` row TWICE until this stamp — 1.946 and 2.239 ns/B, one left behind by
an earlier row replacement. `verify_unicode_ratios.py` could not catch it: each duplicate was
internally consistent with its own ns/B pair, so the check it performs was satisfied by both.)*

⚠ **Two rows have divergent counts — flagged, not glossed over:**

- **`\w+` (mixed-script): 16218 (REAL/PCRE2) vs 5406 (std/RE2).** *Not* a UCD-vintage gap — RE2's `\w` is
  ASCII-only by construction (`[0-9A-Za-z_]`) regardless of Unicode data version, and `std::regex` here runs
  plain ECMAScript grammar. REAL and PCRE2-JIT (`PCRE2_UCP`) both treat `\w` as Unicode-aware. The
  27.46×/0.85× ratios above compare *different definitions of "word character"* — informative about each
  engine's default, not a clean speed comparison.
- **`.` (emoji corpus): 68306 (REAL/PCRE2/RE2) vs 200039 (std::regex).** `std::regex` operates byte-level:
  `.` matches one *byte*, not one *code point*, so on 4-byte-UTF-8 emoji it counts ~2.9× too many "matches" —
  not the same pattern semantically, so the 27.46× speed ratio is not comparing equal work. It is not a
  mitigating factor for `std::regex`, though: doing ~2.9× more (trivial, byte-level) matches and still
  landing 27.46× slower than REAL's real per-codepoint decode is a clean loss either way, just not a
  precisely-quantifiable one from this row alone. REAL vs PCRE2/RE2 on this row is unaffected (those three
  agree on the count).

**Honest read (verdict brut).** **REAL leads PCRE2-JIT on five of thirteen rows on both ISAs** —
`\b\w+\b` (**1.45×** arm64 / **1.42×** x86-64), `[à-ÿ]+` (**1.05×** / **1.66×**, a hair on arm64), the CJK
literal `你好` (**1.50×** / **1.73×**), `.` on emoji (**1.80×** / **4.39×**) and the ascii witness (**1.90×** /
**4.34×**) — reaches parity on `(?i)café` on x86-64 (0.83× / 1.00×), and trails it on the rest: the `\w`
quantifier rows (0.65×–0.96×) and the `\p{}`/script band (0.55×–0.99×). **REAL stays ahead of RE2** on every
comparable row but one (1.43×–11.72× arm64, 1.07×–16.28× x86-64): the count-divergent `\w{2,}` on x86-64 reads
RE2's by 0.79×, where RE2's ASCII-only `\w` finds a third of the matches REAL's Unicode `\w` does. REAL
**crushes `std::regex`** where it compiles at all. So the frame holds where it always did — REAL is
linear-time-safe and `\p{}`-complete, and **PCRE2-JIT is still the throughput leader on the property/script
band** — with the boundary, literal, accented-class and single-code-point rows REAL's. x86-64 is §A's host,
the same as at the last stamp.

### `duel` — REAL vs rust `regex` (`make bench-duel`, `N=20000` repetitions)

The cleanest Unicode comparison: rust's crate is Unicode-codepoint-aware by default for both `\w` and `.`,
so — unlike the three-way table above — **every row's match count agrees** (`match✓ yes`, all nine rows; no
divergence to flag here). REAL `find_iter` vs rust `find_iter`, min-of-15.

| case | REAL ns/B | rust ns/B | winner |
| --- | ---: | ---: | :--- |
| `\w+` (mixed-script) | 2.06 | 5.50 | **REAL 2.7×** |
| `\p{L}+` (CJK) | 1.57 | 5.84 | **REAL 3.7×** |
| `\p{N}+` (arabic digits) | 2.13 | 3.88 | **REAL 1.8×** |
| `\p{sc=Han}` (CJK) | 2.86 | 7.08 | **REAL 2.5×** |
| `\p{scx=Cyrl}` (mixed-script) | 3.15 | 8.03 | **REAL 2.5×** |
| `(?i)` accented literal | 0.39 | 1.38 | **REAL 3.5×** |
| `[a-y]` accented class | 1.90 | 10.90 | **REAL 5.7×** |
| CJK literal | 0.43 | 0.90 | **REAL 2.1×** |
| `.` (emoji, one codepoint) | 2.10 | 16.54 | **REAL 7.9×** |

**REAL leads all nine rows on both ISAs.** This table is the cleaner Unicode comparison than the three-way one
above, because rust's Unicode-aware defaults for `\w` and `.` remove the semantics confound entirely — every
row's match count agrees. Against the last stamp the arm64 rows are within 1.3 % but `(?i)` accented, 0.366 →
0.391 (+6.8 %), the same binary placement as §E's capture rows above. **x86-64, same harness** (g++ 15.3.1,
`-O3`/LTO, §A's host, minimum of six runs): `\w+` **2.64** (2.8×), `\p{L}+` **2.46** (2.8×), `\p{N}+` **3.36** (1.3×),
`sc=Han` **4.26** (2.0×), `scx=Cyrl` **4.89** (2.0×), `(?i)` accented **0.47** (3.3×), accented class **2.75**
(4.5×), CJK literal **0.52** (1.9×), `.` emoji **2.08** (9.5×) — nine of nine REAL's on that leg too.

### A significant find, fixed same-day: `(?i)<literal>` was quadratic, not linear — and it was not Unicode-specific

The `(?i)café` row above was not just "REAL is slow here" — it scaled **badly**. A direct sweep (`real_bench`
alone, `(?i)café` over a French-prose corpus, min-of-15) showed ns/byte roughly **doubling every time the
corpus doubled**, i.e. total scan time was quadratic:

| corpus size | ns/byte (pre-fix) |
| ---: | ---: |
| 28.5 KB | 8.36 |
| 57 KB | 14.59 |
| 114 KB | 27.72 |
| 228 KB | 60.00 |
| 456 KB | 121.24 |
| 912 KB | 242.68 |
| 1.8 MB | 483.60 |

Scoped with three follow-up probes on the same machine:

- **`café` (the same literal, no `(?i)`): perfectly linear**, flat 0.84 ns/B from 28.5 KB to 1.8 MB. The
  non-ASCII literal itself was not the problem.
- **`(?i)cafe` (pure ASCII, case-insensitive): the *same* quadratic blowup** (7.75 → 60.66 → 483.90 ns/B
  across the same size range). **This ruled out Unicode as the cause** — it was a case-insensitive-**literal**
  bug, plain and simple, that this Unicode arc happened to be the first to notice (via `(?i)café`).
- **`(?i)[a-z]+` (case-insensitive, but a class, not a literal): perfectly linear**, flat ~8.4–9.4 ns/B.

So the bug was precisely scoped to **`(?i)` applied to a literal** (ASCII or not) — plain literals and
case-insensitive classes were both unaffected — a genuine gap in the linear-time guarantee the rest of the
engine holds to, discovered as a side effect of this arc rather than its target.

**Root cause and fix (P0, same day — `b6c2a0e` + `4e98b75`).** An icase literal loses its exact-prefix hint
(case-folding needs a small first-byte *set*, e.g. `{c, C}`, not one byte), routing through
`find_bytes_cascade`: one `memchr` per set member, handed the **entire remaining haystack** as its search
window on every call. Members enumerate in ascending byte value, so for `{c, C}` the uppercase byte is
always checked *first* — with the full window — before the far commoner lowercase byte gets a chance to
narrow it. On a haystack sparse in true matches (the common shape — a rare literal in a large text) with the
uppercase fold variant absent from a stretch of it, every rejected candidate paid a full
remaining-haystack `memchr` for a byte that was never there: O(n) candidates × O(n) scan = O(n²). The fix
grows the cascade's window **exponentially** (galloping search, seed 128 B after x86 tuning) instead of
handing it the whole remainder up front, bounding one call to ~2× the distance to the actual hit regardless
of any member's frequency — and bills its cost to the existing deterministic work-counter gate
(`prefilter_note_scan`), which had never covered this function, the actual reason the linearity gate never
caught it.

Post-fix, the same sweep is flat:

| corpus size | ns/byte (post-fix) |
| ---: | ---: |
| 28.5 KB | 1.19 |
| 57 KB | 1.07 |
| 114 KB | 1.01 |
| 228 KB | 0.98 |
| 456 KB | 0.97 |
| 912 KB | 0.96 |
| 1.8 MB | 0.96 |

~500× faster at 1.8 MB (483.6 → 0.96 ns/B), converging rather than growing — genuinely linear, not just
"still quadratic but with a smaller constant." The tuning cost an honest, disclosed, non-eliminable ~4%
on cascade-favorable cases versus never having bounded the window at all (measured against the pre-P0-fix
baseline) — the price of closing the O(n²) hole without reopening it in reverse. Full mechanism, the seed
trade-off measurement (64→1024), and the x86-64/arm64 A/B are in the `b6c2a0e`/`4e98b75` commit messages.

Reproduce: `make bench-engines` / `make bench-duel` (§Methodology below); the scaling sweep above is a
manual `real_bench` loop, not yet wired into either harness as a standing row (worth doing as a regression
tripwire — the deterministic work-counter test added alongside the fix, not this wall-clock sweep, is the
actual gate).

## Methodology & reproduction

- **Goal.** A competitive snapshot *and* a same-machine regression tripwire — not a
  benchmark contest. Compare a fresh run to these tables on the same machine/compiler;
  a single case that jumps well outside run-to-run noise after a change is the signal.
- **Reproduce.** `make bench-engines` builds `benchmarks/bench_engines.cpp` with
  `-I include` and compiles in PCRE2/RE2 **only when `pkg-config` locates them** (so the
  table degrades gracefully to REAL-vs-`std::regex` on a bare machine). `make python-bench`
  builds the abi3 binding and runs `benchmarks/bench.py` against the interpreter's own `re`,
  then the fuzzed-corpus variant `benchmarks/fuzz_bench.py` over randomly fuzzed `(pattern,
  text)` pairs. `make bench-duel` generates the §E
  REAL-vs-rust table (`benchmarks/duel/`, ns/byte with match counts cross-checked; the
  rust harness needs a Rust toolchain). The same two commands (`make bench-engines`,
  `make bench-duel`) also produce the **Unicode — comparative** section's rows above — no
  separate target; the Unicode corpora/patterns are additional cases inside the same two
  harnesses.
- **Equality first.** A ratio between two sides that found different matches compares different
  work, so every harness that prints one checks the answers too. This bullet once claimed that for
  two harnesses while one compared nothing and the other a warm-up count; what each checks now:
  `bench-duel` folds every match's span into a digest on both sides and exits 1 when they differ;
  `bench-matrix` / `matrix-gate`, `bench-percall`, `fuzz_bench.py`, the Rust binding's criterion
  bench and the profile grid compare spans (or the full answer) between their two sides and fail
  on a difference; `bench-multipattern` and `s2a_measure` compare which patterns matched;
  `bench-layout` stops when two builds answer differently. `bench-engines` compares match counts
  across four engines whose semantics differ by design (ASCII `\w`, byte-level `.`), so it flags a
  divergent row rather than failing, and the Unicode section below explains each flag.
- **Not gated.** These *absolute-throughput* targets are excluded from `full-local-gate`
  on purpose: a noisy wall-time measurement must never turn a clean build red.
- **Measure x86-64 on a native host, not in Docker on a second machine.** Same six cores
  either way, but the worst within-arm spread across every
  row is **1.045×** against the container's **1.98×** — the difference between an
  instrument and a lottery. **This rule once carried a second justification that does not hold and
  has been withdrawn:** g++ 13.3 was said to be "the same compiler that builds the manylinux
  wheels", and it is not — the published wheel is built by the manylinux image's own toolchain
  (`GCC 14.2.1` in the `2026.8.16` wheel, read from its `.comment` section, not inferred). g++ 13.3
  was this baseline's *fixed* compiler, chosen so cells stay comparable across stamps, until the `aa22707`
  stamp moved x86-64 to g++ 15.3.1 on an idle host (the previous one was saturated) and so does not read its
  x86-64 cells against the last stamp's; a row here
  describes that baseline and never the artifact a `pip install` delivers. The rule stands on the
  spread and the sign error below, which is all it ever needed. Use Docker only when the native host is unavailable, and then only
  with the workaround below. The two disagree on more than precision: on the `cp_class_hi_width`
  attribute A/B the container reported `(?i)cafe` **improving 25 %** where the native host
  measured it **regressing 217 %**, which is the sign error that matters most.
- **In Docker as a fallback, take the minimum across RUNS, not across samples within one.**
  The harness already reports `min(samples)`, which is the right robust statistic and is
  enough on bare arm64. It is not enough in the x86-64 container, and the reason is
  specific rather than general: interference there arrives as **episodes lasting seconds**,
  long enough to cover several consecutive cases entirely, so every sample of those cases
  is contaminated and the minimum has nothing clean to fall back on. Measured across 16
  runs of one fixed binary, an episode inflated seven *contiguous* rows by up to 1.98×
  while the rows before and after it — including the ASCII witness, which read its fastest
  of all 16 — were untouched. Contiguity in **execution order** is the tell: a genuine
  pattern-specific effect would follow the pattern families, and this does not. The
  minimum across independent runs is stable to **0.2 %** on every row and both with and
  without ASLR (which was tested and refuted as the cause), so 4–5 runs per arm restores a
  usable instrument.
- **On GCC, an A/B measured inside an exhausted inlining budget is a lottery draw.** Past
  `--param inline-unit-growth` (default 40 %) GCC declines in traversal order, so an unrelated change
  re-shuffles which inlinings survive: a pattern that never executes the changed code has moved by
  **+220 %**, and one row read −25 % in a container against +217 % on the native host. **Largely cured** —
  keying the compile-time scratch on dimensions took refusals in a 32-pattern TU from 19 195 to 1457
  and flattened the timing — but not abolished: a TU of *heterogeneous* compile-time patterns still
  approaches the cap, and a residual bump survives at one sampled size. So the discipline stands
  regardless of the margin. **Before believing any delta, read the rows the change cannot reach; if
  they moved too, the measurement is a draw, not a result.** It has caught three would-be results
  here, one of them a 19 % "gain" that was the gauge moving with it. Mechanism, cliff map and the
  eight refuted escapes: `docs/design.dox` §10.1.
- **That instrument's floor is ±3 %, and it is code layout, not noise.** Comparing two
  *different* binaries, rows the change cannot reach still move: on the `9a341ca` A/B,
  `the|fox|dog` read +3.0 %, `[0-9a-f]{8}` −2.1 % and `[a-z]+(?=[a-z])` −2.4 %, none of
  which contain a code-point class. Those rows are the floor gauge — always read them
  before believing a small delta on the rows under test, and treat anything inside ±3 % as
  bounded rather than measured. This is the same layout sensitivity recorded in O2r-1.
- **Two deterministic instruments sit beside the wall clock, and they answer questions it cannot.**
  `make route-probe` and `make alloc-probe` compose patterns from one shared generator
  (`benchmarks/pattern_gen.hpp`, so both tables describe the same population) and report,
  respectively, which dispatch route each composition reaches and how many heap allocations its
  search performs. Both are exact and identical run to run: unlike a timing bench they cannot
  produce a red that means "the runner was busy", which is what makes them the right instrument for
  a question about *shape* rather than speed. `route-probe` answers "does anything reach this
  route?" It reported **6 of 20 unreached** and every one of them was the probe's own blind spot, not
  the engine's: three needed a subject over 512 bytes (the lazy-DFA routing floor) against a corpus
  that topped out at 41, and three needed a seed shape nobody had written — a whole-pattern `.` or
  negated class, and a delimited possessive whose loop is `*+` rather than `++`, which the recognizer
  requires and which reading the seed list would never have revealed. It reports **0 of 20** now, and
  the lesson is worth more than the number: a tool that cannot reach a route must never be read as
  evidence the engine cannot either. `alloc-probe` asserts the stronger property: **how many allocations a
  search performs must be a property of the ROUTE, not of the pattern that took it.** Every
  non-`general_*` route must be flat zero; the general VM is allowed a budget, but not a spread.
  That invariant is what caught the capture pool growing by doubling mid-search — `general_full`
  read min 4 / median 7 / max 17, and reserving a block budget in `reset()` took it to a flat
  **5 / 5 / 5**, which is the figure a later regression should be read against. Neither probe ever
  exits non-zero: an unreached route or a spread is a question for a human, and gating on it would
  pin today's dispatch shape as if it were the contract.
- **Allocation counts and timings must never come from one binary.** Counting allocations means
  replacing global `operator new`, which is not inlinable and dominates the very wall clock it was
  linked in to explain: one `regex_set` construction measured 7191 µs with the counter linked and
  2601 µs without — a **2.8× inflation** that was read, and published, as a property of the code.
  The mistake was made three times in one session, twice *after* the hazard had been written down,
  so it is a compile error now rather than a note: `benchmarks/measure.hpp` accepts
  `REAL_BENCH_TIME` xor `REAL_BENCH_ALLOCS` and `#error`s on both. Two binaries, two runs, two
  tables — and say in the write-up which number came from which, because a reader cannot tell
  afterwards.
- **A FIRST-search measurement needs one live regex per sample, never construct-and-destroy in a
  loop.** The per-regex caches (byte program, lazy alphabet, one-pass table, Aho-Corasick automaton)
  are invalidated by PROGRAM ADDRESS, so a loop that builds a regex, searches, destroys it and builds
  another can hand the next iteration the previous one's address — and some iterations then skip the
  build the measurement exists to time. Taking `min` over such a loop selects precisely the
  iterations that skipped it. The shape that works: construct N regexes and keep them ALL alive, then
  time one search each and divide.

      std::vector<real::regex> v;  v.reserve(N);
      for (int i = 0; i < N; ++i) { v.emplace_back(pattern); }   // all live: no address reuse
      const auto t0 = clock::now();
      for (auto& re : v) { (void) re.search(subject); }          // exactly one first search each
      const auto t1 = clock::now();

  Verify the sample count in the output (`200/200 matched`): a variant that stops matching never
  reaches the route being measured, and a subject with no candidate never builds anything, so either
  silently measures nothing. Both mistakes were made here in one sitting.
- **Rebuilding the "before" arm with `git checkout <file>` AFTER committing the change measures the
  change against itself.** It restores the COMMITTED content, which now contains the change. The two
  arms then read identically, and that reads exactly like a refutation. Build both arms from explicit
  revisions instead — `git archive <rev> include | tar x -C <dir>` — so each is named rather than
  assumed. This produced a retraction of a correct result before the second measurement caught it.
- **`callgrind_annotate` marks recursion depth with a trailing `'2`, and those lines are the SAME
  function.** Summing them double-counts. A function reported at "20 %" across two such lines is at
  10 %, and a cost model built on the inflated figure will point at the wrong place.
- **Every table in this document measures ns/BYTE over a 200 KB subject, and that is the only regime
  where REAL beats PCRE2-JIT.** Measured per CALL on short subjects — the way a validation pattern is
  actually used, once per record on a field of a few dozen bytes:

  | subject | REAL arm64 | PCRE2 | | REAL x86-64 | PCRE2 | |
  | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
  | 8 B | 42.6 ns | 15.8 | **0.37×** | 152.8 ns | 38.2 | **0.25×** |
  | 64 B | 60.2 | 32.4 | 0.54× | 178.3 | 62.1 | 0.35× |
  | 256 B | 142.6 | 107.3 | 0.75× | 282.7 | 161.2 | 0.57× |
  | 1 KB | 388.4 | 353.9 | 0.91× | 670.1 | 548.3 | 0.82× |
  | 16 KB | 5252.8 | 5237.3 | 1.00× | 8467.3 | 8325.1 | 0.98× |

  REAL's per-byte rate is the better one — that is what §A shows and it is true. But PCRE2's fixed
  per-call cost is far lower, so **REAL does not overtake it until roughly 16 KB**, and §A only ever
  measures 200 KB. The pattern above is `^[a-z]+$`, the row §A publishes as REAL 1.35× ahead.

  The anchored SHAPES are worse than the classes, and by an order of magnitude: on an 11-byte subject
  `^[0-9]{4}-[0-9]{2}-[0-9]{2}$` costs **303 ns on arm64 and 603 on x86-64 against PCRE2's 16 and 38**
  — 19× and 16× behind. `^[0-9a-f]{8}$` and `^(?:alpha|bravo|charlie)$` read the same way. Those
  shapes are on the general VM: the anchoring work covered the two class routes only.

  Even the unanchored witness `[a-z]+` reads 0.40× / 0.26×, so a fixed per-call cost of roughly 27 ns
  on arm64 and 115 on x86-64 is paid before any pattern-specific work. Stated here rather than in a
  row, because no row in this document is positioned to show it: a short-subject, per-call table is
  the missing measurement, not a missing optimisation.

- **The harness cannot be instrumented to measure faster, and this is measured rather than feared.**
  Tonight's refutations kept turning on the same mechanism — a change to a header included everywhere
  moves §A rows it cannot reach — so the obvious next step was a cheaper instrument: a runtime
  `BENCH_REAL_ONLY` switch in `bench_engines.cpp`, timing only REAL while every other engine stays
  compiled and linked, so the translation unit keeps its shape. **It moved `digits` by 12 %.** Same
  engine tree, same corpus: 1.788 / 1.791 / 1.789 ns/B with the harness as it ships, 1.999 / 1.998 /
  2.008 with the switch added, against a 1.786 baseline. One function, two branches and a `getenv`
  spend from the same per-unit inline budget the engine's own inlining comes out of.

  So the rule is: **numbers taken with a modified harness cannot be compared against numbers taken
  with the shipped one**, in either direction, and an isolated probe is worse still — it reported
  gains on BOTH platforms for a decoder change that lost on both. Measure engine changes with
  `make bench-engines` unmodified, both arms, or do not claim them. The corollary is uncomfortable
  and stated anyway: there is no cheap instrument for this engine, and the expensive one is the only
  honest one.

- **§A's `fields [^,]+` row is not a like-for-like comparison, and the gap is the difference in work.**
  A negated class matches every code point but the excluded ones, so it routes to the code-point scan
  rather than the byte class loop: 5.448 ns/B against 1.894 for `[a-z]+` on x86-64, 3.450 against
  1.862 on arm64. Scanning it as bytes would be sound *if* every non-ASCII code point were a member --
  which it is -- but it is not equivalent, because REAL excludes INVALID UTF-8 from a code-point
  class and a byte scan would not. Measured: on `ab,c\xC3d,ef` the row's own pattern yields
  `[3,4)` and `[5,6)`, breaking at the malformed byte, where a byte scan would return `[3,6)`.
  So the 0.60× against PCRE2-JIT on that row is REAL doing encoding validation the comparison does
  not require of the other engine, not REAL being slower at the same task. Read it that way before
  treating it as a deficit, and if it is ever "fixed", check what the malformed-UTF-8 matrix says
  first.
- **Matrices sweep sizes.** A hot-path optimisation's cost or benefit can invert across
  the haystack size (a per-search setup that amortises on 2 MB can dominate 16 KB) and
  across match density — so a bench that measures one slice hides a regression in another.
  The inner-literal route's veto is therefore a **4-D matrix**, `benchmarks/matrix4d/`
  (`make bench-matrix`; pattern × size {16 KB…2 MB} × match/no-match × density). Unlike the
  absolute benches it **is** gated (`make matrix-gate`, a fast subset in `full-local-gate`):
  it compares the route to the core *on the same machine* — a ratio, robust to noise — and
  exits non-zero on any cell where the route regresses the core or gates a no-match search.
  Every future hot-path arc is measured against the full matrix before it ships.
