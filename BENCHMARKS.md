# REAL — performance baseline

A reproducible snapshot of REAL's throughput against other regex engines, and of
its Python binding against the standard library's `re`. It serves two purposes:

1. an **honest competitive picture** — where REAL wins, where it loses, and why;
2. a **regression tripwire** — re-measure on the *same machine* before a grouped
   push and investigate any clear, repeatable slowdown.

It is informational only. Neither benchmark is invoked by `full-local-gate`, and
neither can fail a build — wall-time is noisy and hardware-dependent, so absolute
figures and cross-machine comparison are explicitly **not** the goal.

Reproduce with **`make bench-engines`** (C++, in-process) and **`make bench-python`**
(binding vs `re`). Both check result/match-count equality before timing — a fast wrong
answer is not a benchmark win.

## Conditions of this baseline

| | |
| --- | --- |
| Machine | Apple M1 Pro (`arm64`), Darwin 23.6.0 |
| C++ compiler | Apple clang 16.0.0, flags `-O2 -std=c++20` |
| Engines | REAL 2026.6.6, `std::regex` (libc++), PCRE2 10.47 (JIT), RE2 11.0.0 |
| Python | CPython 3.14.3, `re` (stdlib) vs REAL 2026.6.6 (abi3 binding) |
| Method | median of repeated batches; match counts checked equal across engines |
| As of | 2026-06-17 — the code published as `2026.6.6` (2026-06-18) |

## A. C++ engine throughput

Each engine compiles the pattern once, then counts all non-overlapping matches over
the same corpus; only the scan is timed. `ns/B` is nanoseconds per corpus byte (lower
is better). `(x)` is *engine_time / REAL_time* — **> 1 means REAL is faster**. Match
counts agreed across all four engines on every case (ASCII-class semantic parity).

| case | REAL ns/B | std::regex | PCRE2-JIT | RE2 |
| --- | ---: | ---: | ---: | ---: |
| words `[a-z]+` | 6.012 | 92.583 (15.40×) | 2.389 (0.40×) | 14.233 (2.37×) |
| alternation `the\|fox\|dog` | 2.765 | 117.280 (42.42×) | 1.584 (0.57×) | 6.266 (2.27×) |
| hex `[0-9a-f]{8}` | 2.368 | 84.057 (35.50×) | 1.254 (0.53×) | 3.425 (1.45×) |
| digits `[0-9]+` | 3.012 | 83.135 (27.60×) | 1.438 (0.48×) | 8.410 (2.79×) |
| date `{4}-{2}-{2}` | 1.812 | 73.057 (40.31×) | 0.391 (0.22×) | 3.485 (1.92×) |
| fields `[^,]+` | 6.535 | 75.696 (11.58×) | 1.922 (0.29×) | 11.025 (1.69×) |
| literal | 0.909 | 30.914 (34.01×) | 0.486 (0.53×) | 1.364 (1.50×) |

**Reading.** REAL beats `std::regex` by **11–42×** and RE2 by **~1.5–2.8×** across the
board. It is **slower than PCRE2 with its JIT** (0.22–0.57×) — expected and honest:
PCRE2-JIT emits native machine code per pattern, whereas REAL is a header-only
*constexpr* Pike VM with no runtime code generation. REAL trades that last constant
factor for being compile-time-evaluable, dependency-free, and linear-time guaranteed.

## B. Python binding vs `re`

`ratio` is *re_time / REAL_time* — **> 1 means REAL is faster**.

| case | `re` | REAL | ratio |
| --- | ---: | ---: | ---: |
| literal · hit @1MB | 693.6 µs | 473.6 µs | 1.46× |
| literal · miss @1MB | 686.9 µs | 467.0 µs | 1.47× |
| literal · anchored miss @1MB | 180 ns | 135 ns | 1.33× |
| date · search @100KB | 1.16 ms | 89.8 µs | **12.96×** |
| date · findall @100KB | 1.24 ms | 594.4 µs | 2.09× |
| hex ids · findall | 276.4 µs | 181.4 µs | 1.52× |
| emails · findall groups | 1.60 ms | 4.99 ms | **0.32×** |
| words · findall @100KB | 1.51 ms | 842.4 µs | 1.80× |
| digits · findall @100KB | 1.14 ms | 106.5 µs | **10.70×** |
| alternation · findall @100KB | 805.2 µs | 386.1 µs | 2.09× |
| sub spaces @100KB | 2.22 ms | 605.3 µs | 3.67× |
| sub dates with refs | 1.23 ms | 559.6 µs | 2.20× |
| split commas @100KB | 78.8 µs | 88.6 µs | **0.89×** |
| line starts · findall (multiline) | 447.6 µs | 15.5 µs | **28.87×** |
| compile (no cache) | 23.1 µs | 1.5 µs | **15.83×** |
| `(a+)+b` · re n=24 / REAL n=10k | 1118.52 ms | 500.4 µs | **2235×** (ReDoS) |

**Geometric-mean speedup over `re`: 2.47×** (PASS). Two of the fourteen key cases are
*slower* than `re`: **emails with groups (0.32×)** and **comma split (0.89×)** —
high-volume `findall`/`split` on "easy" patterns where CPython's C engine has a lower
per-match constant. REAL's edge widens on anchored search, compilation, and anything
pathological; the two losses are an accepted trade for linear-time safety.

### `finditer` memory — lazy iteration

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

### Threaded `findall` / `split` — GIL release (two-phase)

`findall` and `split` also release the GIL, but in two phases: a first pass walks the
matches with the GIL **released** and records each match's byte spans into a flat buffer
(reentrant — the match iterator owns its VM scratch and only reads the immutable
program); a second pass builds the Python objects with the GIL **held**. That second
pass is the catch — it allocates **O(matches)** Python objects under the GIL, a *serial*
tail that both caps scaling (~2× for fast-scanning patterns) and, on small match-dense
subjects, lets the frequent per-call GIL toggling *regress* throughput. So the release
threshold here is **4 KB**, not the 512 B of single-shot matching (measured: the
no-regression point is ~2 KB for the fastest-scanning patterns; 4 KB keeps a margin).
Below it the interleaved scan runs under the held GIL, byte-identical to before.

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
the GIL-held build is the serial portion (Amdahl). Patterns that scan more per match,
e.g. `.` (one codepoint per match), scale further (~4× at 8 threads on ≥ 32 KB) because
their parallel scan is a larger share of the call. Sub-4 KB subjects stay flat **by
design**: the threshold keeps them on the serial path rather than paying toggle
contention for no gain.

Transient memory: the collect phase holds **O(matches × (groups + 1))** byte offsets
(8 B each) for the call's duration — small, but not zero. `findall`/`split` already
return O(matches) Python objects, so the order is unchanged; `finditer` stays lazy
(O(1) memory) on the interleaved path and is deliberately left untouched.

## C. ReDoS safety — the headline property

The catastrophic backtracking case `(a+)+b` over `"a"×N` (no `b`, so no match):

| engine | input | time |
| --- | --- | ---: |
| REAL | N = 100 000 | 5.949 ms (**linear**) |
| RE2 | N = 100 000 | 0.217 ms (**linear**) |
| `std::regex` | N = 22 | *refused* — "complexity … exceeded a pre-set level" |
| Python `re` | n = 24 | 1118.52 ms (and climbing exponentially) |

REAL and RE2 stay linear; the backtracking engines (`std::regex`, `re`) either refuse
or blow up at trivially small inputs. This is the property REAL is built to guarantee.

## Methodology & reproduction

- **Goal.** A competitive snapshot *and* a same-machine regression tripwire — not a
  benchmark contest. Compare a fresh run to these tables on the same machine/compiler;
  a single case that jumps well outside run-to-run noise after a change is the signal.
- **Reproduce.** `make bench-engines` builds `benchmarks/bench_engines.cpp` with
  `-I include` and compiles in PCRE2/RE2 **only when `pkg-config` locates them** (so the
  table degrades gracefully to REAL-vs-`std::regex` on a bare machine). `make
  bench-python` builds the abi3 binding and runs `benchmarks/bench.py` against the
  interpreter's own `re`. A third target, `make bench-fuzz`, runs the same comparison
  over randomly fuzzed `(pattern, text)` pairs.
- **Equality first.** Both harnesses verify identical results (and per-engine match
  counts) before timing, so a divergence shows up as a correctness failure, not a
  misleading speed number.
- **Not gated.** These targets are excluded from `full-local-gate` on purpose: a noisy
  wall-time measurement must never turn a clean build red.
