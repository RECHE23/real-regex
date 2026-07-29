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

## Conditions of this baseline

| | |
| --- | --- |
| Version | REAL `2026.7.61` — **re-measured for this stamp: §E.4 only** (the crate's own criterion rows, arm64, at `3cb085a`), by an interleaved A/B against `v2026.7.60` with the same bench file on both sides, one group at a time, machine otherwise idle: two rounds per group and **four rounds of 5 s for `captures`**, which was load-bearing — `captures/word_bound` read +3.1 % on two rounds and **−0.3 % on four**. No row regresses; every row other than the four `icase_class` gains lands within ±1.6 %. **The row this train was about:** 7.60 added an `icase_class` family because the suite had no case-insensitive pattern at all, a gap two compile-cost defects had already come through, and it read 3.37× behind the `regex` crate immediately. The profile refuted the obvious explanation — `(?i)[a-z]+` was not on the code-point scan but on the lazy DFA (`lazy_dfa::anchored_end`, 23.5 %). `find/icase_class` **631.8 → 252.1 µs (−60.1 %)**, and **3.37× behind becomes 1.34×**; `captures/icase_class` **680.0 → 295.8 (−56.5 %)**, 3.66× → 1.57×; `compile/icase_class` −30.8 % (9.66× ahead → 14.01×); `first_use/icase_class` −39.6 %. Under icase `[a-z]` gains the long s and the Kelvin sign, both MULTI-BYTE, so the class was expanded to a byte-level alternation — 8 byte classes and 18 instructions against 1 and 5 for `[a-z]+` — which stopped being a class loop and matched no route at all: two rare fold partners were costing the route. An ASCII bitmap with a few non-ASCII members IS a code-point class and is now emitted as one. arm64 walk `(?i)[a-z]+` −57.7 % and `(?i)[a-zA-Z0-9_]+` −58.4 %; x86-64 instructions −58.2 % and −58.7 %; `[a-z]+`/`\w+`/`.`/`[^,]+`/`dog`/`(?i)dog`/`\b\w+\b` at 0.0 % on arm64 and within 0.7 % on x86-64. **`real::dfa` widened, not narrowed:** it refused `klass_cp` outright, so this change would have made those patterns unconstructible there — a capability regression, not an acceptable price — and the refusal was a limit of the entry point, so `dfa_flatten` now expands `klass_cp` through the same `build_byte_program` the lazy DFA has always used. Text-mode `\d`/`\s`, Unicode properties, non-ASCII classes and folded ASCII classes all build there now, where that API had never accepted one. §A, §Unicode, §B and §multi-pattern carry their earlier figures unchanged. Per-train benchmark-impact log: CHANGELOG.md (full release notes: docs/release-notes/ + GitHub Releases). **A second-order cost the fuzzer found**, within the hour and through a path the change never touched: `regex_set` builds one of these DFAs internally, so widening what the DFA ACCEPTS widened what it ATTEMPTS — on `^\w` it spent 27.9 s where it had errored immediately. `max_dfa_states` bounds the RESULT of subset construction and nothing bounded the WORK, which is superlinear: a folded ASCII class expands to 26 instructions and builds in 0.04 ms, `\d+` to 261 and 1.88 ms, text-mode `\w+` to **3434 and 418 ms**. `max_dfa_byte_program` = 512 sits between them, and everything it refuses was already refused before; a repeat multiplies the expansion, so `\d{2}` is already 517. Reproducer **27.91 s → 0.33 s**. **Disclosed, not chased:** `(\w+)@(\w+)` first use stays **2.64× behind**, `no_match` 2.09×/2.05× (a ratio on 1.6 µs against 778 ns), `captures/word_bound` 1.82×, `find/literal` 1.57×. |
| Machines | §A on **two ISAs**: devbox (`x86-64`, g++ 13.3.0) *and* Apple M1 Pro (`arm64`, Apple clang 16). §B / §E on M1 Pro (§E's x86-64 leg noted inline where it diverges — see §E). §multi-pattern measured on **x86-64 devbox** (g++ 13.3, RE2 + Hyperscan 5.4) |
| Engines | `std::regex`; **PCRE2 10.47, JIT on, both ISAs** (built from source on x86-64 to pin the exact version); RE2 (10.0 on x86-64, 11.0 on arm64 — version-differs-by-leg, uncontested given the margins). Multi-pattern: RE2::Set, Hyperscan (optional). §E: rust `regex` 1.12.4 |
| Python | CPython 3.14.6, `re` (stdlib) vs the in-place REAL `2026.7.55` extension (§B re-measured for this stamp, arm64 M1 Pro, N = 40 paired samples, bootstrap CI) |
| Method | §A: median of N = 30 paired batches (both ISAs, this re-stamp), bootstrap CI; match counts equal on every case, both ISAs. §E: best-of-15, REAL `count_matches` vs rust `find_iter`/`captures_iter`, match counts equal. §multi-pattern: best-of-7, `make bench-multipattern`. **Every ratio below is computed programmatically from the raw ns/B pair — `benchmarks/verify_bench_ratios.py` re-derives and checks all of them** |

## A. C++ engine throughput

Each engine compiles the pattern once, then counts all non-overlapping matches over the same corpus; only
the scan is timed. `ns/B` is nanoseconds per corpus byte (lower is better). `(x)` is *engine_time /
REAL_time* — **> 1 means REAL is faster**. Match counts agreed across all four engines on every case, on
both ISAs, on the same `3cd9c81` (v2026.7.55) tree for this re-stamp.

**x86-64** — devbox, g++ 13.3.0, N = 30, PCRE2 10.47-JIT, RE2 10.0:

| case | REAL ns/B | std::regex | PCRE2-JIT | RE2 |
| --- | ---: | ---: | ---: | ---: |
| words `[a-z]+` | 4.15 | 29.03 (**6.99×**) | 6.72 (**1.62×**) | 28.32 (**6.82×**) |
| digits `[0-9]+` | 2.36 | 24.57 (**10.40×**) | 3.82 (**1.61×**) | 16.73 (**7.08×**) |
| fields `[^,]+` | 5.49 | 24.85 (**4.53×**) | 5.17 (0.94×) | 22.79 (**4.15×**) |
| alternation `the\|fox\|dog` | 2.31 | 30.21 (**13.08×**) | 2.36 (**1.02×**) | 10.39 (**4.50×**) |
| date `{4}-{2}-{2}` | 1.17 | 18.60 (**15.91×**) | 0.69 (0.59×) | 4.09 (**3.50×**) |
| hex `[0-9a-f]{8}` | 1.36 | 20.95 (**15.42×**) | 1.99 (**1.47×**) | 4.09 (**3.01×**) |
| literal | 0.45 | 15.57 (**34.40×**) | 0.67 (**1.48×**) | 2.41 (**5.32×**) |
| lookahead `[a-z]+(?=[a-z])` | 8.95 | 77.78 (**8.69×**) | 7.21 (0.81×) | unsupported |

**arm64** — Apple M1 Pro, Apple clang 16, N = 30, PCRE2 10.47-JIT, RE2 11.0:

| case | REAL ns/B | std::regex | PCRE2-JIT | RE2 |
| --- | ---: | ---: | ---: | ---: |
| words `[a-z]+` | 2.27 | 94.59 (**41.67×**) | 2.34 (**1.03×**) | 14.03 (**6.18×**) |
| digits `[0-9]+` | 1.43 | 86.35 (**60.31×**) | 1.44 (**1.00×**) | 8.68 (**6.06×**) |
| fields `[^,]+` | 3.11 | 76.54 (**24.60×**) | 1.87 (0.60×) | 11.10 (**3.57×**) |
| alternation `the\|fox\|dog` | 1.89 | 116.03 (**61.52×**) | 1.56 (0.83×) | 6.18 (**3.28×**) |
| date `{4}-{2}-{2}` | 0.76 | 73.00 (**95.72×**) | 0.39 (0.51×) | 3.43 (**4.49×**) |
| hex `[0-9a-f]{8}` | 1.43 | 80.90 (**56.64×**) | 1.25 (0.87×) | 3.42 (**2.39×**) |
| literal | 0.25 | 31.25 (**126.61×**) | 0.48 (**1.95×**) | 1.36 (**5.50×**) |
| lookahead `[a-z]+(?=[a-z])` | 6.18 | 163.17 (**26.39×**) | 3.67 (0.59×) | unsupported |

**Reading — verdict brut, no dressing up.**

- **REAL ≫ `std::regex`**, always: 4.5–34.4× on x86-64, 24.6–126.6× on arm64 (libc++'s `std::regex`
  falls even further behind on arm64). Never below 4.5×.
- **REAL > RE2**, always where RE2 supports the pattern: 3.0–7.1× on x86-64, 2.4–6.2× on arm64.
- **REAL vs PCRE2-JIT is close, ISA-dependent, and mixed — and this train moved two rows across the
  line.** **`literal` crossed on both ISAs** and is now REAL's, decisively: **1.48× x86-64, 1.95×
  arm64**, where the `2026.7.51` stamp had it as PCRE2 ground (0.80× / 0.77×). That is the
  one-search exact-literal route on both legs, plus the two-byte NEON prefilter on arm64.
  **`alternation` crossed on x86-64 only** (0.87× → **1.02×**, a bare win) and remains a loss on arm64
  (0.83×). Going the other way, **`fields` lost its thin x86-64 lead** (1.06× → 0.94×) — a layout
  effect the Version row documents with instruction-count evidence, not a scan that got slower.
  Elsewhere REAL leads on `words` (1.62× / 1.03×), `digits` (1.61× / **1.00×**, an exact tie on arm64)
  and `hex` (1.47× / 0.87× — x86 only); `date` remains PCRE2 ground on both (0.59× / 0.51×).
- **The lookahead line stays in PCRE2-JIT's order of magnitude** after P3c: **9.11 / 6.81 ns/B**
  (0.74× / 0.54×) instead of the pre-P3c general-VM order (~92 / ~48 ns/B). REAL does a **bounded
  lookaround in linear time**; PCRE2 is still faster here but by **backtracking** (itself ReDoS-able on a
  crafted lookaround), and **RE2 and the rust crate cannot compile the pattern at all** (`unsupported`).
  `find_iter` / Python `finditer` do not get the P3c fast path by construction (return type fixed at
  compile time so pure `[a-z]+` does not regress) — this row is `count_matches` only, stated plainly so it
  is not read as a `find_iter` number.

## Multi-pattern — which-matched + extraction (Stage-1 `regex_set`)

Reproduce with **`make bench-multipattern`** (RE2 and Hyperscan optional via pkg-config). Informational
only — not a CI gate. Absolute MB/s track the host; the durable content is the **shape** and the
equal-set / equal-count asserts.

**Semantics (round-3, equal counts):**

| Table | Question | Engines | Forced full scan? |
| --- | --- | --- | --- |
| **A — filtre / IDS** | which-matched (which patterns hit ≥ once) | REAL N-walks (`regex_set`), RE2::Set, Hyperscan `SINGLEMATCH` | yes — 8 present + (N−8) absent |
| **B — extraction** | all non-overlapping matches | REAL `count_matches` N-walks, RE2 `FindAndConsume` N-walks | inherent (present patterns only) |

**x86-64 devbox** (g++ 13.3, RE2, Hyperscan 5.4, 1 MiB log-like corpus, best-of-7 MB/s, higher is better):

TABLE A — which-matched (sets equal when all engines compile):

| N | HS single | RE2::Set | REAL N-walks |
| ---: | ---: | ---: | ---: |
| 16 | ~336 | ~440 | **~559** (fastest) |
| 32 | ~320 | ~452 | ~195 |
| 64 | ~380 | ~451 | ~83 |
| 128 | ~380 | ~452 | ~39 |

TABLE B — extraction non-overlapping (counts equal REAL/RE2):

| N | REAL N-walks | RE2 N-walks |
| ---: | ---: | ---: |
| 4 | ~160 | ~70 |
| 8 | ~107 | ~41 |

**Reading — capacity first, speed second:**

- **Architectural gap:** single-pass engines (RE2::Set, Hyperscan) stay **flat** in N; pure N-walks
  **degrade** hard (e.g. ~421 → 41 MB/s from N=32 → 256 on arm64). Stage-2 fused which-matched
  is **flat** and closes most of that gap.
- **Stage-2 fused + set-level first-byte skip** (same host/harness, arm64 M1,
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
- **`\b`/`\B` wrap on shape fast-paths** (same host, arm64 M1, post-Stage-2 tree):
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
| date · search @100KB | 1.15 ms | 2.8 µs | **402.82×** ↑ (was 7.84×) |
| date · findall groups | 1.23 ms | 3.4 µs | **356.33×** ↑ (was 8.25×) |
| sub · dates with refs | 1.24 ms | 27.2 µs | **45.58×** ↑ (was 7.71×) |
| word starts ASCII · findall (multiline) | 444.6 µs | 15.6 µs | **28.57×** |
| literal · hit @1MB | 695.9 µs | 59.0 µs | **11.85×** ↑ (was 1.45×) |
| literal · miss @1MB | 696.6 µs | 58.6 µs | **11.85×** |
| digits · sparse findall @100KB | 1.14 ms | 243.9 µs | **4.66×** |
| sub · spaces @100KB | 2.25 ms | 674.7 µs | 3.34× |
| alternation · findall @100KB | 804.6 µs | 305.3 µs | 2.64× |
| emails · findall groups | 1.48 ms | 857.5 µs | 1.73× ↑ (was 1.09×) |
| split · commas @100KB | 77.4 µs | 46.9 µs | **1.65×** ↑ (was **0.24×** — now a win) |
| hex ids · findall | 247.9 µs | 155.0 µs | 1.60× |
| words · findall @1KB | 15.8 µs | 10.6 µs | 1.50× |
| words · findall @10KB | 151.9 µs | 115.0 µs | 1.32× |
| words · findall @100KB | 1.49 ms | 1.19 ms | 1.26× |
| words · dense findall @100KB | 1.54 ms | 1.22 ms | 1.25× |
| words · findall @1MB | 15.86 ms | 13.21 ms | 1.20× |
| non-space · Unicode findall | 1.74 ms | 1.60 ms | 1.09× |
| literal · anchored miss @1MB | 178 ns | 218 ns | **0.82×** |
| `(a+)+b` · re n=24 / REAL n=10k (prefilter) | 1048.33 ms | **706 ns** | **~1.5×10⁶×** (ReDoS) |

**Geometric-mean speedup over `re`: 4.78× (CI [2.32, 12.62] clears 1.0 — PASS)**, up from 2.34× at the
`2026.7.25` stamp. Two headline movements, both from the routes the recent trains rebuilt:

- **The `\d{n}` date rows went from single-digit to three-figure**: search @100KB 7.84× → **402.82×**
  (1.15 ms → **2.8 µs**), findall-groups 8.25× → **356.33×**, sub-with-refs 7.71× → **45.58×**. These
  patterns have a required inner literal (`-`) and no leading one, so they are exactly the shape the
  inner-literal route plus its warm-aware floor now serve; `re` scans, REAL memchrs.
- **The fixed-literal rows followed the engine work**: `literal hit @1MB` 1.45× → **11.85×**
  (695.9 µs → **59.0 µs**), with the new `literal miss @1MB` row at the same 11.85×. That is the
  one-search exact-literal route and the two-byte NEON prefilter reaching the binding.

**Only one case is still slower than `re`, down from two.** `split · commas @100KB` — the worst row at the
last stamp, **0.24×** — is now a **1.65× win**; what remains is **`anchored miss @1MB` at 0.82×**, a
178 ns-versus-218 ns contest where CPython's C engine has the lower fixed per-call cost and there is no scan
to amortise it over. That one is an accepted trade, not a target.

Two rows moved *down* in ratio and the reason is not REAL: `digits sparse` 9.60× → 4.66× and `words dense`
1.48× → 1.25× — `re`'s own times improved between the stamps (digits sparse 1.50 ms → 1.14 ms), so the
quotient narrowed while REAL's absolute stayed flat-to-better. Ratios against a moving baseline are
disclosed as such rather than read as a regression.

On the fuzzed corpus (`benchmarks/fuzz_bench.py`, 2886 comparable cases): aggregate wall time **REAL 241 ms
vs `re` 45.6 s (189×)**, and `re` hit **85 catastrophic blow-ups where REAL stayed linear**. Honest detail
from the same run: on the *median* fuzz case — a tiny subject where nothing amortises — the per-op ratio is
**0.44×**, i.e. REAL is slower; the aggregate win is entirely the tail `re` cannot survive. Bounded-lookahead
throughput is flat at ~19 MB/s from 1 KB to 1 MB (linear fit R² = 1.0000 — O(n), not backtracking).

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

**Scope of this re-measure:** arm64 Apple M1 Pro, Apple clang, `-O3`, matching-only
after compile, median of 31 (3 consistent rounds). §A/§Unicode are now re-stamped at
`2026.7.55`; this section's own two-leg numbers were measured at `2026.7.51` and are
unchanged by that train (neither leg touches the bare-VM or prefilter paths timed here). x86-64 devbox cross-check (same method): prefilter
~1.2 µs / bare VM ~10.6 ms at N=100K. Prefilter leg is also what `make bench-engines`
emits under `redos`.

| engine / path | input | time |
| --- | --- | ---: |
| REAL `(a+)+b` (literal prefilter, no `b`) | N = 100 000 | **~2.1 µs** (reject; ~2 µs best) |
| REAL `(a+)+` (bare VM, no required literal) | N = 100 000 | **~4.9 ms** (linear) |
| REAL `(a+)+` (bare VM, linearity check) | N = 1 000 000 | **~49 ms** (≈10× → linear) |
| RE2 `(a+)+b` | N = 100 000 | ~0.22 ms (linear; this harness) |
| `std::regex` (libstdc++) | N = 26 | 4107 ms (backtracks; libc++ instead *refuses* at "complexity exceeded") |
| Python `re` | n = 24 | 1397.76 ms (and climbing exponentially) |

REAL and RE2 stay linear; the backtracking engines (`std::regex`, `re`) either refuse
or blow up at trivially small inputs. The prefilter makes the classic demo *faster*
than older docs claimed (~0.52 ms was a stale figure that measured neither leg); the
bare-VM row is the guarantee without that short-circuit. This is the property REAL
is built to guarantee.

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
linear-time-guarantee axis. This duel is honest about where REAL loses: the same patterns run through both
engines over the same corpora (~1 MB), best of 15 batches, match counts cross-checked equal.
**Stamp (re-stamp):** REAL **`3cd9c81`** (v2026.7.55 — the perf train: one-search exact-literal route,
two-byte NEON literal prefilter, per-slot DFA ownership; and, from the two trains before it, the
warm-aware inner-literal haystack floor that most of the sparse-row movement below belongs to),
`rust regex 1.12.4` (`find_iter` on `regex::bytes`), both `-O3`/LTO, both ISAs measured this pass. **Method:** REAL matching-only (`count_matches`) vs rust
`find_iter` (spans) — engine scan cost, no capture-slot fill on either side; every ratio below is
`rust_ns/REAL_ns` computed from a JSON dump (`run_duel.py --json`), not typed by hand —
`benchmarks/verify_bench_ratios.py` re-checks it. Corpora = `benchmarks/duel/run_duel.py`. Reproduce:
rebuild `benchmarks/duel/real_bench` against current headers (`-O3 -flto`), `cargo build --release` in
`benchmarks/duel/rust_bench`, then `python3 benchmarks/duel/run_duel.py [--json out.json]` (or
`make bench-duel` for the related find_iter/captures apples-to-apples table in §E.3).

**arm64** — Apple M1 Pro, Apple clang 16, `-O3 -flto`:

| case | REAL ns/B | rust ns/B | winner |
| --- | ---: | ---: | :--- |
| literal `dog` | 0.318 | 0.561 | **REAL 1.8×** |
| alternation `fox\|dog\|cat` | 1.021 | 1.294 | **REAL 1.3×** |
| class `[a-z]+` | 1.641 | 12.199 | **REAL 7.4×** |
| digits `[0-9]+` | 2.119 | 17.228 | **REAL 8.1×** |
| fields `[^,]+` | 2.894 | 9.382 | **REAL 3.2×** |
| word-boundary `\b\w+\b` | 5.388 | 10.999 | **REAL 2.0×** |
| email `(\w+)@(\w+)` | 4.651 | 5.236 | REAL 1.1× |
| ident `(\w+)_(\w+)` | 53.542 | 30.786 | rust 1.7× |
| date no-match `\d{4}-\d{2}-\d{2}` | 0.023 | 0.012 | rust 1.9× |
| date sparse `\d{4}-\d{2}-\d{2}` | 0.128 | 0.077 | rust 1.7× |
| email sparse `(\w+)@(\w+)` | 0.127 | 0.121 | rust 1.0× |
| key= `key=(\w+)` | 1.017 | 1.444 | **REAL 1.4×** |

**x86-64** — devbox, g++ 13.3.0, `-O3 -flto`:

| case | REAL ns/B | rust ns/B | winner |
| --- | ---: | ---: | :--- |
| literal `dog` | 0.489 | 0.841 | **REAL 1.7×** |
| alternation `fox\|dog\|cat` | 1.480 | 2.141 | **REAL 1.4×** |
| class `[a-z]+` | 2.712 | 19.059 | **REAL 7.0×** |
| digits `[0-9]+` | 3.293 | 23.471 | **REAL 7.1×** |
| fields `[^,]+` | 4.089 | 15.874 | **REAL 3.9×** |
| word-boundary `\b\w+\b` | 4.592 | 16.848 | **REAL 3.7×** |
| email `(\w+)@(\w+)` | 5.242 | 6.321 | **REAL 1.2×** |
| ident `(\w+)_(\w+)` | 113.178 | 46.355 | rust 2.4× |
| date no-match `\d{4}-\d{2}-\d{2}` | 0.018 | 0.016 | rust 1.1× |
| date sparse `\d{4}-\d{2}-\d{2}` | 0.144 | 0.102 | rust 1.4× |
| email sparse `(\w+)@(\w+)` | 0.138 | 0.158 | REAL 1.1× |
| key= `key=(\w+)` | 1.287 | 2.162 | **REAL 1.7×** |

**Reading — verdict brut. The ISA no longer splits this table the way it did; the trains since `a994ff9`
closed most of the rows that used to flip.**

- **REAL now leads 8 of 12 rows on arm64 and 9 of 12 on x86-64.** The class/digit/field/word-boundary
  block is unchanged in shape and still the widest margin (`class` 7.4× arm64 / 7.0× x86-64; `digits`
  8.1× / 7.1×; `fields` 3.2× / 3.9×; `word-boundary` 2.0× / 3.7×).
- **Three rows that used to flip sign between ISAs are now REAL's on both.** `literal` (was rust 1.4×
  arm64 / REAL 1.1× x86 → now **REAL 1.8× / 1.7×**) and `alternation` (rust 1.1× / REAL 1.5× → **REAL
  1.3× / 1.4×**) are the one-search exact-literal route, with the NEON prefilter on top for the arm64 leg;
  `email` dense (tie / rust 1.1× → **REAL 1.1× / 1.2×**) is the same route reaching the confirm.
  A single-machine number would no longer mis-state these — they agree across ISAs now.
- **The sparse-match rows — rust's clearest wins at the last stamp — have largely closed.** `email sparse`
  went from rust 4.8× arm64 / 6.3× x86-64 to **rust 1.0× arm64 (a tie) / REAL 1.1× x86-64**, an absolute
  1.014 → 0.138 ns/B on the x86 leg; `date sparse` from 2.9× / 3.6× to **1.7× / 1.4×**. That is the
  warm-aware inner-literal floor (the route now fires on a reused haystack instead of deferring to the
  core), not the literal work of this train — credited where it belongs.
- **rust's remaining wins are two, and one of them got worse.** `ident` (dense `_`-joined `\w`, no useful
  prefilter for either engine) is still its biggest and **widened**: 1.6× → **1.7×** arm64, 2.1× →
  **2.4×** x86-64, on an absolute that rose 100.5 → 113.2 ns/B on x86. That is the already-named
  non-one-pass residual (the `_` conflict), and it is now the table's worst row — stated, not buried.
  `date no-match` stays rust's on both (1.9× arm64, 1.1× x86-64), both near-ties on an 0.02 ns/B scan.
- **`key=` remains REAL's ISA-stable win** (1.4× arm64, 1.7× x86-64) — the inner-literal prefilter route
  (§E.5) still generalizes across ISAs better than the sparse rows do.

Two things carry a caveat, not a verdict:

- **The date-no-match / date-sparse split is the same distinction §E.5 already named** — a no-match scan
  (both engines only reject; REAL's rare-required-byte path is close either way, rust 1.9× arm64 vs
  **REAL 1.1× x86-64** — itself one of the four sign-flips above) is not the same measurement as a
  sparse-but-real-match scan (rust's variable-offset literal prefilter has no REAL equivalent yet, 2.9–3.6×
  behind on *both* ISAs) — collapsing them into one "date" story would hide that gap.
- **Capture apples-to-apples** (REAL `find_iter` full Match vs rust `captures_iter`) is §E.1–E.3 — email
  dense is ~parity with rust captures there; the tables above are span/count-only so they do not charge
  either side for group fill.

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

  **arm64 M1 Pro, criterion, 64 KiB corpus** — two trains' effect on this bench, against the `regex` crate in
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

  **arm64 M1 Pro, criterion, at `9c400e1`.** Ratio is `regex` ÷ REAL, so **> 1 means REAL is ahead**:

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

  REAL is ahead on **8 of 8** compile rows and **7 of 8** first-use rows. The exception is the one that
  matters most to state plainly: `(\w+)@(\w+)` still pays **2.91 ms** on first use to build its one-pass
  capture extractor, against 589 µs for the whole of `regex`'s eager work. That is down from **21.3 ms**
  (34.9× behind) over five passes — flat scratch, sparse signatures, one interned class per byte range,
  jump-chain resolution in the flood (which alone made the flood land *on* the minimal automaton, 660 nodes
  in and 660 out where it was 2508 in), and dropping a duplicate Tier-A/Tier-B expansion — but 4.94× behind
  is still behind. Declining the table instead is not the answer and was measured: it buys **3.3×** on the
  scan (`find` 135 µs against 443 on a 64 KiB corpus), with a break-even near 900 KB.

  Read the two families together: REAL's cost is overwhelmingly *eager and small*, `regex`'s is *eager and
  large*, and the one place REAL is worse is a **lazy** build that a short-lived pattern pays in full.

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

**Stamp.** REAL `2026.7.55`, both ISAs re-measured for this stamp. **arm64** table below: Apple M1 Pro,
Apple clang 16, `-O2`, N = 30 (`make bench-engines`). **x86-64**, same harness and N, g++ 13.3 with
PCRE2 10.47 (from source) and RE2 10.0 — full four-engine run this time, not REAL-only: `\w+` mixed
**6.10** (pcre2 0.60×, re2 0.82×), `\p{L}+` CJK **6.94** (0.43× / re2 **2.77×**), `\p{N}+` **6.79**
(0.39× / 1.10×), `sc=Han` **12.57** (0.39×), `scx=Cyrl` **11.03** (0.61×), `(?i)café` **1.97**
(0.32× / 1.49×), `[à-ÿ]+` **9.03** (0.69× / 2.81×), literal `你好` **0.89** (pcre2 **1.23×** — the same
crossing as arm64), `.` emoji **7.93** (pcre2 **1.37×**), ascii witness **4.04** (**1.85×**). Oracle:
exhaustive `\p{L}` over U+0000..10FFFF (surrogates skipped) — **0 mismatch**.
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
| `\w+` (mixed-script) | 3.765 | 61.82 (16.42×) | 1.94 (**0.52×**) | 3.33 (0.88×) | 16218/5406/16218/5406 ⚠ |
| `\p{L}+` (CJK) | 4.518 | unsupported | 1.37 (**0.30×**) | 13.66 (3.02×) | 12904/—/12904/12904 |
| `\p{N}+` (arabic digits) | 2.308 | unsupported | 1.63 (**0.71×**) | 5.30 (2.30×) | 6250/—/6250/6250 |
| `\p{sc=Han}` (CJK) | 7.771 | unsupported | 2.14 (**0.28×**) | unsupported | 25808/—/25808/— |
| `\p{scx=Cyrl}` (mixed-script) | 5.825 | unsupported | 2.69 (**0.46×**) | unsupported | 32436/—/32436/— |
| `(?i)café` (accented) | 1.296 | unsupported | 0.34 (**0.26×**) | 1.31 (1.01×) | 3509/—/3509/3509 |
| `[à-ÿ]+` (accented) | 6.748 | 89.02 (13.19×) | 2.08 (**0.31×**) | 12.75 (1.89×) | 38599/38599/38599/38599 |
| literal `你好` (CJK) | 0.477 | 29.58 (62.01×) | 0.58 (**1.22×**) | 2.62 (5.49×) | 6452/6452/6452/6452 |
| `.` (emoji, one codepoint) | 4.217 | 59.93 (14.21×) | 3.71 (**0.88×**) | 18.79 (4.46×) | 68306/200039/68306/68306 ⚠ |
| ascii witness `[a-z]+` | 2.273 | 94.89 (41.75×) | 2.27 (**1.00×**) | 14.20 (6.25×) | 42108/42108/42108/42108 |

*(Same convention as §A: `(x) = engine_time / REAL_time`, **> 1 means REAL is faster**; **bold** marks the
PCRE2-JIT column, REAL's main competitor throughout this document. Every ratio here is computed
programmatically from the ns/B pair — `benchmarks/verify_unicode_ratios.py` re-derives and checks all of
them against this table.)*

⚠ **Two rows have divergent counts — flagged, not glossed over:**

- **`\w+` (mixed-script): 16218 (REAL/PCRE2) vs 5406 (std/RE2).** *Not* a UCD-vintage gap — RE2's `\w` is
  ASCII-only by construction (`[0-9A-Za-z_]`) regardless of Unicode data version, and `std::regex` here runs
  plain ECMAScript grammar. REAL and PCRE2-JIT (`PCRE2_UCP`) both treat `\w` as Unicode-aware. The
  15.53×/0.90× ratios above compare *different definitions of "word character"* — informative about each
  engine's default, not a clean speed comparison.
- **`.` (emoji corpus): 68306 (REAL/PCRE2/RE2) vs 200039 (std::regex).** `std::regex` operates byte-level:
  `.` matches one *byte*, not one *code point*, so on 4-byte-UTF-8 emoji it counts ~2.9× too many "matches" —
  not the same pattern semantically, so the 14.82× speed ratio is not comparing equal work. It is not a
  mitigating factor for `std::regex`, though: doing ~2.9× more (trivial, byte-level) matches and still
  landing 14.82× slower than REAL's real per-codepoint decode is a clean loss either way, just not a
  precisely-quantifiable one from this row alone. REAL vs PCRE2/RE2 on this row is unaffected (those three
  agree on the count).

**Honest read (verdict brut).** **REAL no longer trails PCRE2-JIT on every row — but on all but one it
still does.** The exception is the one this train targeted: the **CJK literal `你好` crossed on both ISAs**
(arm64 0.50× → **1.22×**, x86-64 **1.23×**), which is the two-byte NEON prefilter doing exactly what it was
built for — a multi-byte needle whose lead byte alone is a weak filter. The ascii witness is now an exact
tie (**1.00×**) and `.` (emoji) sits at 0.88× arm64 but **1.37× on x86-64**. Everything else is unchanged in
shape: the `\p{}`/script band stays ~0.26×–0.71× (still 1.4×–3.8× slower than JIT), the accented class rows
0.31×. **REAL stays ahead of RE2** on comparable rows except count-divergent `\w+` (1.01×–6.25×), and
**crushes `std::regex`** where `\p{}` is unsupported. So the honest frame holds where it always did — REAL is
linear-time-safe and `\p{}`-complete, and **PCRE2-JIT is still the Unicode throughput leader on the
property/script band** — with one row now genuinely REAL's. Arm64 residual on `\p{N}+`/`sc=Han`/`scx=Cyrl`
(page-only / single-cp under K=32) persists; the x86-64 leg, now measured four-engine rather than REAL-only,
reads the same band (0.39×–0.61×) rather than contradicting it.

### `duel` — REAL vs rust `regex` (`make bench-duel`, `N=20000` repetitions)

The cleanest Unicode comparison: rust's crate is Unicode-codepoint-aware by default for both `\w` and `.`,
so — unlike the three-way table above — **every row's match count agrees** (`match✓ yes`, all nine rows; no
divergence to flag here). REAL `find_iter` vs rust `find_iter`, min-of-15.

| case | REAL ns/B | rust ns/B | winner |
| --- | ---: | ---: | :--- |
| `\w+` (mixed-script) | 3.32 | 5.38 | **REAL 1.6×** |
| `\p{L}+` (CJK) | 3.47 | 5.78 | **REAL 1.7×** |
| `\p{N}+` (arabic digits) | 2.76 | 3.77 | **REAL 1.4×** |
| `\p{sc=Han}` (CJK) | 7.08 | 6.86 | rust 1.0× |
| `\p{scx=Cyrl}` (mixed-script) | 5.40 | 7.76 | **REAL 1.4×** |
| `(?i)` accented literal | 1.10 | 1.43 | **REAL 1.3×** |
| `[a-y]` accented class | 5.92 | 10.72 | **REAL 1.8×** |
| CJK literal | 0.45 | 0.87 | **REAL 1.9×** |
| `.` (emoji, one codepoint) | 3.94 | 15.92 | **REAL 4.0×** |

**No longer an even split — REAL leads eight of the nine rows.** Both rows rust used to hold have crossed:
`\p{L}+` CJK (rust 1.1× → **REAL 1.7×**) and the **CJK literal (rust 1.7× → REAL 1.9×**, an absolute
1.94 → 0.45 ns/B) — the latter is the two-byte NEON prefilter on a multi-byte needle, the shape it was
built for. Only `\p{sc=Han}` is rust's now, and barely (1.0×, a tie on a 7 ns/B scan). As before, this
table is the cleaner Unicode comparison than the three-way one above, because rust's Unicode-aware
defaults for `\w` and `.` remove the semantics confound entirely — every row's match count agrees.
**x86-64, same harness** (g++ 13.3, `-O3`/LTO): `\w+` **5.80** (REAL 1.6×), `\p{L}+` **6.29** (1.4×),
`\p{N}+` **7.37** (rust 1.3×), `sc=Han` **12.04** (rust 1.1×), `scx=Cyrl` **10.49** (1.1×), `(?i)`
accented **1.65** (1.2×), accented class **7.74** (2.0×), CJK literal **0.84** (**1.5×** — the same
crossing), `.` emoji **6.51** (3.8×) — seven of nine REAL's on that leg.

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
trade-off measurement (64→1024), and the x86/M1 A/B are in the `b6c2a0e`/`4e98b75` commit messages.

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
- **Equality first.** Both harnesses verify identical results (and per-engine match
  counts) before timing, so a divergence shows up as a correctness failure, not a
  misleading speed number.
- **Not gated.** These *absolute-throughput* targets are excluded from `full-local-gate`
  on purpose: a noisy wall-time measurement must never turn a clean build red.
- **Matrices sweep sizes.** A hot-path optimisation's cost or benefit can invert across
  the haystack size (a per-search setup that amortises on 2 MB can dominate 16 KB) and
  across match density — so a bench that measures one slice hides a regression in another.
  The inner-literal route's veto is therefore a **4-D matrix**, `benchmarks/matrix4d/`
  (`make bench-matrix`; pattern × size {16 KB…2 MB} × match/no-match × density). Unlike the
  absolute benches it **is** gated (`make matrix-gate`, a fast subset in `full-local-gate`):
  it compares the route to the core *on the same machine* — a ratio, robust to noise — and
  exits non-zero on any cell where the route regresses the core or gates a no-match search.
  Every future hot-path arc is measured against the full matrix before it ships.
