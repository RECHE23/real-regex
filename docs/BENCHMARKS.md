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
| Version | REAL `2026.7.31` + Arc B (`e38f516`) — §multi-pattern Stage-2 + Arc I/II + Arc B `\b` unlock; §E re-duel post-Arc B (this train). §A absolute ns/B still the dual-ISA 2026.7.26 campaign (ratios durable) |
| Machines | §A on **two ISAs**: devbox (`x86-64`, g++ 13.3) *and* Apple M1 Pro (`arm64`, Apple clang 16). §B / §E on M1 Pro. §multi-pattern measured on **x86-64 devbox** (g++ 13.3, RE2 + Hyperscan 5.4) |
| Engines | `std::regex`; **PCRE2 10.47, JIT on, both ISAs** (built from source on x86-64 to pin the exact version); RE2 (10.0 on x86-64, 11.0 on arm64 — version-differs-by-leg, uncontested given the margins). Multi-pattern: RE2::Set, Hyperscan (optional). §E: rust `regex` 1.12.4 |
| Python | CPython 3.14, `re` (stdlib) vs the REAL `2026.7.25` abi3 wheel (§B — not re-measured this train) |
| Method | §A: median of N ≥ 15 paired batches (x86-64 N = 30, arm64 N = 15), bootstrap CI; match counts equal. §E: best-of-15, REAL `count_matches` vs rust `find_iter`, match counts equal. §multi-pattern: best-of-7, `make bench-multipattern`. **Ratios / shapes are durable; absolute ns/B and MB/s track the host** |

## A. C++ engine throughput — and the SIMD candidate arc closes part of the PCRE2 gap

Each engine compiles the pattern once, then counts all non-overlapping matches over the same corpus; only
the scan is timed. `ns/B` is nanoseconds per corpus byte (lower is better). `(x)` is *engine_time /
REAL_time* — **> 1 means REAL is faster**. Match counts agreed across all four engines on every case, on
both ISAs, on the same 2026.7.26 tree (see the Version row for the two legs' exact commits).

**x86-64** — devbox, g++ 13.3, N = 30, PCRE2 10.47-JIT, RE2 10.0:

| case | REAL ns/B | std::regex | PCRE2-JIT | RE2 |
| --- | ---: | ---: | ---: | ---: |
| words `[a-z]+` | 3.69 | 8.21× | **2.32×** | 7.66× |
| digits `[0-9]+` | 2.03 | 12.29× | **1.77×** | 8.21× |
| fields `[^,]+` | 4.68 | 5.48× | **1.45×** | 4.89× |
| alternation `the\|fox\|dog` | 2.07 | 14.78× | **1.32×** | 5.03× |
| date `{4}-{2}-{2}` | 2.94 | 6.48× | 0.23× | 1.39× |
| hex `[0-9a-f]{8}` | 1.38 | 15.82× | **1.33×** | 2.95× |
| literal | 0.73 | 21.49× | 0.79× | 3.25× |
| lookahead `[a-z]+(?=[a-z])` | 91.85 | 0.84× | 0.07× | unsupported |

**arm64** — Apple M1 Pro, Apple clang 16, N = 15, PCRE2 10.47-JIT, RE2 11.0:

| case | REAL ns/B | std::regex | PCRE2-JIT | RE2 |
| --- | ---: | ---: | ---: | ---: |
| words `[a-z]+` | 2.61 | 36.03× | **0.89×** | 5.41× |
| digits `[0-9]+` | 1.56 | 54.02× | **0.93×** | 5.66× |
| fields `[^,]+` | 3.05 | 25.04× | **0.62×** | 3.62× |
| alternation `the\|fox\|dog` | 1.56 | 72.41× | **1.01×** | 3.99× |
| date `{4}-{2}-{2}` | 2.06 | 35.23× | 0.19× | 1.66× |
| hex `[0-9a-f]{8}` | 1.42 | 57.78× | 0.88× | 2.42× |
| literal | 0.61 | 51.61× | 0.81× | 2.22× |
| lookahead `[a-z]+(?=[a-z])` | 48.36 | 3.30× | 0.08× | unsupported |

**Reading — what is robust, and what depends on the ISA.**

- **REAL ≫ `std::regex`**, always: 5.5–21.5× on x86-64, 25–72× on arm64 (libc++'s `std::regex` falls even
  further behind on arm64). Never below 5.5×.
- **REAL > RE2**, always: 1.4–8.2× on x86-64, 1.7–5.7× on arm64.
- **The SIMD candidate arc (mask-carried block scan+verify, `run_alternation`/`run_fixed_shape`) moved two
  lines past PCRE2-JIT this train.** Alternation now leads on **both** ISAs — x86-64 0.73× → **1.32×**,
  arm64 0.78× → **1.01×** (a real crossing, though thin enough on arm64 to call a tie, not a rout: stated
  plainly rather than rounded up). The quantified-class line (`hex`) crosses on x86-64 (0.59× → **1.33×**)
  but **not** on arm64 (0.52× → 0.88×, still JIT-ahead) — the ISA-dependence from the prior baseline holds
  there. `date` (a related fixed-shape but outside this arc's scope) is flat on both ISAs, as expected.
- **REAL vs PCRE2-JIT still flips with the ISA on the plain class scans** — the pre-existing, unrelated-to-
  this-arc story. On **x86-64** REAL leads `[a-z]+` (2.32×), `[0-9]+` (1.77×), `[^,]+` (1.45×); on **arm64**
  the JIT retakes all three (0.89×, 0.93×, 0.62×). *Same engine, same PCRE2 version (10.47), same corpus,
  same tree* — the difference is purely the JIT's per-ISA code quality. PCRE2 still leads `date` and
  `literal` on both ISAs (0.19–0.81×) — untouched ground.
- **The lookahead line is about safety first; raw speed is API-dependent** — stated plainly. REAL does a
  **bounded lookaround in linear time**; PCRE2 does it faster but by **backtracking** (so PCRE2 is itself
  ReDoS-able on a crafted lookaround), and **RE2 and the rust crate cannot do it at all** (`unsupported`).
  The §A table above is a **pre-P3c stamp** (general-VM order, ~50–90 ns/B). After P3c, the trailing-LA
  class+ shape (`[a-z]+(?=[a-z])`) takes a once-per-walk monomorphic fast path on **matching-only**
  surfaces: `count_matches` / `search` / `match` / `replace` / `find_all` (~8 ns/B on x86 matching-only —
  ~11× the general VM). **`find_iter` / Python `finditer` deliberately stay on the pure monomorphic
  walk** (return type fixed at compile time so pure `[a-z]+` does not regress) and therefore do **not**
  get that win — correctness identical, throughput not. Benches must use `count_matches` (matching-only,
  equitable with PCRE2/RE2 counters); `find_all().size()` is confounded by Match-vector cost. Re-stamp
  §A after the P3c train before treating the lookahead row as competitive copy.

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
- **Stage-2 fused + Arc I first-byte skip** (same host/harness, arm64 M1,
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
- **Arc II B1 — `\b`/`\B` wrap on shape fast-paths** (same host, arm64 M1, post-Stage-2 tree):
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
| word starts · findall (multiline) | 584.4 µs | 19.5 µs | **30.16×** |
| digits · sparse findall @100KB | 1.50 ms | 156.6 µs | **9.60×** |
| date · findall groups | 1.61 ms | 194.9 µs | **8.25× ↑** (was 3.42×) |
| date · search @100KB | 1.51 ms | 192.6 µs | **7.84× ↑** (was 3.28×) |
| sub · dates with refs | 1.64 ms | 212.7 µs | **7.71× ↑** (was 3.42×) |
| sub · spaces @100KB | 2.99 ms | 621.3 µs | 4.82× |
| alternation · findall @100KB | 1.06 ms | 518.3 µs | 2.05× |
| hex ids · findall | 330.5 µs | 192.2 µs | 1.73× |
| words · dense findall @100KB | 2.05 ms | 1.37 ms | 1.48× |
| literal · hit @1MB | 920.5 µs | 634.1 µs | 1.45× |
| non-space · Unicode findall | 2.33 ms | 1.89 ms | 1.22× |
| emails · findall groups | 1.96 ms | 1.82 ms | 1.09× |
| literal · anchored miss @1MB | 236 ns | 281 ns | **0.84×** |
| split · commas @100KB | 102.2 µs | 421.7 µs | **0.24×** |
| `(a+)+b` · re n=24 / REAL n=10k | 1397.76 ms | 46.0 µs | **30408×** (ReDoS) |

**Geometric-mean speedup over `re`: 2.34× (CI [1.51, 3.76] clears 1.0 — PASS).** The headline change since
the 2026.7.16 baseline: the **`\d{n}` quantifier / capture path roughly doubled** — date search 3.28 → **7.84×**,
date findall-groups 3.42 → **8.25×**, sub-with-dates 3.42 → **7.71×** — while emails-with-groups holds its
earlier flip to a win (1.09×). Two cases remain *slower* than `re`: **comma split (0.24×)** and **anchored
miss (0.84×)** — high-volume `split` / tiny anchored matches where CPython's C engine has the lower per-match
constant. REAL's edge widens on sparse/rare-byte scans, sub, and anything pathological (`(a+)+b`: 30408× —
`re` blows up, REAL stays linear; on a fuzzed corpus `re` hit 85 catastrophic blow-ups, REAL none); the two
losses are an accepted trade for linear-time safety.

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

The catastrophic backtracking case `(a+)+b` over `"a"×N` (no `b`, so no match):

| engine | input | time |
| --- | --- | ---: |
| REAL | N = 100 000 | **0.521 ms** (linear) |
| RE2 | N = 100 000 | 0.162 ms (linear) |
| `std::regex` (libstdc++) | N = 26 | 4107 ms (backtracks; libc++ instead *refuses* at "complexity exceeded") |
| Python `re` | n = 24 | 1397.76 ms (and climbing exponentially) |

REAL and RE2 stay linear; the backtracking engines (`std::regex`, `re`) either refuse
or blow up at trivially small inputs. This is the property REAL is built to guarantee.

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
**Stamp (re-duel):** REAL **`e38f516`** = `2026.7.30` + Arc B, `rust regex 1.12.4` (`find_iter` on
`regex::bytes`), Apple M1 Pro, both `-O3`/LTO. **Method:** REAL matching-only (`count_matches`) vs rust
`find_iter` (spans) — engine scan cost, no capture-slot fill on either side. Corpora = `benchmarks/duel/run_duel.py`.
Reproduce: rebuild `benchmarks/duel/real_bench` against current headers (`-O3 -flto`),
`cargo build --release` in `benchmarks/duel/rust_bench`, then the §E cases (or `make bench-duel` for the
related find_iter/captures apples-to-apples table in §E.3).

| case | REAL ns/B | rust ns/B | winner | Δ REAL vs 7.11 |
| --- | ---: | ---: | :--- | :--- |
| class `[a-z]+` | 1.772 | 3.166 | **REAL 1.8×** | ↓1.4× (was 2.47) |
| digits `[0-9]+` | 2.266 | 8.140 | **REAL 3.6×** | ↓1.3× (was 2.98) |
| fields `[^,]+` | 2.830 | 2.805 | ~tie (rust 1.0×) | ↓1.1× (was 3.09) |
| alternation `fox\|dog\|cat` | 1.035 | 0.869 | rust 1.2× | ↓1.3× (was 1.36) |
| literal `dog` | 0.691 | 0.274 | rust 2.5× | ~flat (was 0.73) |
| ident `(\w+)_(\w+)` | 48.518 | 4.686 | rust 10.4× | ↓1.2× only (was 59.9) |
| word-boundary `\b\w+\b` | 4.662 | 2.684 | rust 1.7× | **↓8.9×** (was 41.5) |
| bare `\w+` (residual probe) | 4.678 | 3.151 | rust 1.5× | *(new row)* |
| email `(\w+)@(\w+)` | 5.269 | 1.798 | rust 2.9× | **↓8.5×** (was 44.6) |
| date (no-match) `\d{4}-\d{2}-\d{2}` | 0.023 | 0.013 | rust 1.8× | **↓20×** (was 0.45) |

**Reading (post lazy-DFA + Arc B, same-host re-duel):**

- **Veto A holds (captures are not the 12–25× story).** Email `(\w+)@(\w+)` is **5.3 ns/B** (was 44.6) —
  **2.9×** behind rust spans, not 25×. S-MEASURE already showed slot-tracking alone is only ~1.0–1.3×;
  the remaining email gap is denser DFA / prefilter work, not capture slots. **Ident** `(\w+)_(\w+)` is the
  outlier still at **~48 ns/B / rust 10×** on this dense `id_42` corpus (only mild Δ vs 7.11) — a separate
  residual (dense `_`-joined `\w` scans), not a reason to re-open “slots first.”
- **Arc B closed the `\b` lockout; residual is raw `\w+`.** `\b\w+\b` **4.662 ≈ bare `\w+` 4.678** (parity —
  the B-1 simplify is live). vs rust that is **1.5–1.7×**, not 15×. The open word gap is **Unicode
  `klass_cp` / word-class throughput vs rust’s DFA**, distinct from word-boundary handling.
- **Class scans still lead:** `[a-z]+` / `[0-9]+` **REAL 1.8× / 3.6×**.
- **Still open vs rust (honest):** literal / alternation prefilters (Teddy/memchr — rust 1.2–2.5×);
  raw `\w+` klass_cp vs DFA (~1.5×); dense ident `(\w+)_(\w+)` on this corpus; date no-match is now
  **1.8×** (rare-byte path), not 37×.

Two rows carry a caveat, not a verdict:

- The **date row is a no-match scan** — the corpus contains *no* `yyyy-mm-dd`, so both engines only reject.
  rust’s required-literal prefilter jumps on the fixed `-`. REAL’s rare-required-byte path now lands at
  **0.023 ns/B** (within **1.8×** of rust). On a corpus with sparse real dates REAL can *beat* rust
  (~1.8× — memchr on the rare byte, then digit verify). Explicit `[0-9]{4}-…` gets the fixed-offset hint;
  text Unicode `\d` is variable-width `klass_cp` when the hint would be unsound.
- **Capture apples-to-apples** (REAL `find_iter` full Match vs rust `captures_iter`) is §E.1–E.3 — email
  dense is ~parity with rust captures; the table above is span/count-only so it does not charge either
  side for group fill.

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
  pure-Rust `regex` crate: `[a-z]+` ≈ 0.8×, `[0-9]+` ≈ 0.7× (REAL ahead). The per-match allocation an earlier
  measurement flagged here is gone.
- **`captures_iter`** (materializing every group) — the crate still allocates a group vector per match (the
  groups have to be stored somewhere), so capture-dense extraction trails `regex` by roughly 2×. The buffer
  reuse cut it (`[a-z]+` from ≈ 3.4× to ≈ 2×), but this residual is inherent to returning owned group spans.

So on span throughput the crate is competitive; on full capture extraction it pays a bounded, understood
allocation cost. Either way the pitch is not raw speed but the linear-time / ReDoS-safe guarantee and the
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

**Stamp.** REAL `98876d2`, Apple M1 Pro (arm64), Apple clang 16.0.0 (clang-1600.0.26.6), `-O2`,
2026-07-11T01:00Z. The `(?i)<literal>` cells (`(?i)café` in both tables, and its own subsection below) are
re-stamped at `4e98b75` (post P0-fix `b6c2a0e` + seed-tune `4e98b75`), same host/build; every other cell is
unchanged from `98876d2` and was not re-measured. Engine Unicode Character Database versions:

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
| `\w+` (mixed-script) | 6.08 | 79.65 (0.41×) | 2.47 (**0.41×**) | 4.26 (0.70×) | 16218/5406/16218/5406 ⚠ |
| `\p{L}+` (CJK) | 8.96 | unsupported | 1.77 (**5.06×**) | 17.76 (0.50×) | 12904/—/12904/12904 |
| `\p{N}+` (arabic digits) | 2.61 | unsupported | 2.15 (**1.21×**) | 7.05 (0.37×) | 6250/—/6250/6250 |
| `\p{sc=Han}` (CJK) | 8.01 | unsupported | 2.86 (**2.80×**) | unsupported | 25808/—/25808/— |
| `\p{scx=Cyrl}` (mixed-script) | 6.51 | unsupported | 3.53 (**1.84×**) | unsupported | 32436/—/32436/— |
| `(?i)café` (accented) | 1.06 | unsupported | 0.35 (3.03×) | 1.31 (**0.81×**) | 3509/—/3509/3509 |
| `[à-ÿ]+` (accented) | 5.57 | 114.31 (**0.05×**) | 2.72 (0.49×) | 16.60 (0.34×) | 38599/38599/38599/38599 |
| literal `你好` (CJK) | 1.54 | 38.79 (**0.04×**) | 0.78 (0.51×) | 3.48 (0.44×) | 6452/6452/6452/6452 |
| `.` (emoji, one codepoint) | 5.40 | 78.78 (0.07×) | 5.03 (**0.93×**) | 24.96 (0.22×) | 68306/200039/68306/68306 ⚠ |
| ascii witness `[a-z]+` | 3.08 | 122.08 (**0.03×**) | 2.96 (0.96×) | 18.53 (0.17×) | 42108/42108/42108/42108 |

*(Ratios are read as REAL_time / engine_time — i.e. `std::regex` at 0.41× means REAL is roughly 1/0.41 ≈
2.4× faster; **bold** marks REAL's closest competitor per row. This differs from §A's `engine_time /
REAL_time` convention because most rows above are "REAL loses" — see next paragraph — and that framing reads
more honestly than inverting every number to look like a REAL win.)*

⚠ **Two rows have divergent counts — flagged, not glossed over:**

- **`\w+` (mixed-script): 16218 (REAL/PCRE2) vs 5406 (std/RE2).** *Not* a UCD-vintage gap — RE2's `\w` is
  ASCII-only by construction (`[0-9A-Za-z_]`) regardless of Unicode data version, and `std::regex` here runs
  plain ECMAScript grammar. REAL and PCRE2-JIT (`PCRE2_UCP`) both treat `\w` as Unicode-aware. The 0.41×/0.70×
  ratios above compare *different definitions of "word character"* — informative about each engine's
  default, not a clean speed comparison.
- **`.` (emoji corpus): 68306 (REAL/PCRE2/RE2) vs 200039 (std::regex).** `std::regex` operates byte-level:
  `.` matches one *byte*, not one *code point*, so on 4-byte-UTF-8 emoji it counts ~2.9× too many "matches."
  The 0.07× ratio is comparing REAL's per-codepoint scan to `std::regex` doing roughly 4× less semantic work
  per byte — not a fair speed comparison at all.

**Honest read of the rest.** REAL is **behind PCRE2-JIT on every `\p{}`/script row except `\p{L}+`**
(0.36×–1.21× — often 2–3× slower), and **badly behind on the two literal/class rows** (`[à-ÿ]+` 0.49×,
CJK literal 0.51×) where PCRE2's JIT and RE2's compiled DFA both have a real edge over REAL's byte-class
scan on multi-byte input. REAL is comfortably ahead of `std::regex` everywhere `\p{}` is unsupported for it
(as expected — `std::regex` doing zero real work is not a REAL win). The one clear win against a *capable*
competitor is `\p{L}+` vs PCRE2 (**5.06×**) — REAL's General_Category route wins there. **`(?i)café` was a
117×-behind outlier — a P0 correctness-adjacent bug (see below), not a throughput gap. Fixed, it now trails
PCRE2 by 3.0× (in the same range as the other `\p{}` rows) and is slightly ahead of RE2 (0.81×).**

### `duel` — REAL vs rust `regex` (`make bench-duel`, `N=20000` repetitions)

The cleanest Unicode comparison: rust's crate is Unicode-codepoint-aware by default for both `\w` and `.`,
so — unlike the three-way table above — **every row's match count agrees** (`match✓ yes`, all nine rows; no
divergence to flag here). REAL `find_iter` vs rust `find_iter`, min-of-15.

| case | REAL ns/B | rust ns/B | winner |
| --- | ---: | ---: | :--- |
| `\w+` (mixed-script) | 5.98 | 7.09 | REAL 1.2× |
| `\p{L}+` (CJK) | 8.18 | 7.68 | rust 1.1× |
| `\p{N}+` (arabic digits) | 3.20 | 4.98 | REAL 1.6× |
| `\p{sc=Han}` (CJK) | 7.09 | 9.13 | REAL 1.3× |
| `\p{scx=Cyrl}` (mixed-script) | 6.37 | 10.21 | REAL 1.6× |
| `(?i)` accented literal | 0.96 | 1.44 | **REAL 1.5×** |
| `[a-y]` accented class | 5.30 | 14.10 | REAL 2.7× |
| CJK literal | 1.94 | 1.12 | rust 1.7× |
| `.` (emoji, one codepoint) | 5.34 | 21.25 | REAL 4.0× |

A genuine, roughly-even split — REAL ahead on scripts/classes/`.`/accented-literal, rust ahead on `\p{L}+`
and the CJK literal (both by a modest ~1.1–1.7×) — not the lopsided picture the three-way table paints,
because rust's Unicode-aware defaults remove the semantics confound entirely. The `(?i)` row was the one
outlier (rust 166×) until the P0 fix below closed it; it is no longer carved out from the split above.

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
