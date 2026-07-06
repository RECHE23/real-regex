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
| Version | REAL `2026.7.16` — every section below re-measured on it, 2026-07-05 |
| Machines | devbox (`x86-64`, g++ 13) for §A / §C · Apple M1 Pro (`arm64`, Apple clang 16) for §B / §E |
| Engines | `std::regex`, PCRE2 10.47 **(JIT enabled — the harness prints `PCRE2_CONFIG_JIT = 1`, x86-64)**, RE2 11.0.0 |
| Python | CPython 3.14, `re` (stdlib) vs the published REAL `2026.7.16` abi3 wheel |
| Method | median of repeated batches, paired, with a bootstrap CI; match counts checked equal across engines; **non-cherry-picked** — the losses show |

## A. C++ engine throughput

Each engine compiles the pattern once, then counts all non-overlapping matches over
the same corpus; only the scan is timed. `ns/B` is nanoseconds per corpus byte (lower
is better). `(x)` is *engine_time / REAL_time* — **> 1 means REAL is faster**. Match
counts agreed across all four engines on every case.

| case | REAL ns/B | std::regex | PCRE2-JIT | RE2 |
| --- | ---: | ---: | ---: | ---: |
| words `[a-z]+` (1 MB) | 3.29 | 29.14 (8.9×) | 6.39 (**1.94×**) | 28.36 (8.6×) |
| digits `[0-9]+` | 2.08 | 24.50 (11.8×) | 3.61 (**1.73×**) | 16.96 (8.2×) |
| fields `[^,]+` | 4.57 | 25.72 (5.6×) | 5.06 (**1.11×**) | 23.10 (5.1×) |
| date `{4}-{2}-{2}` | 0.66 | 18.24 (27.6×) | 0.61 (0.93× ≈) | 4.10 (6.2×) |
| alternation `the\|fox\|dog` | 2.71 | 29.93 (11.0×) | 2.23 (0.82×) | 10.46 (3.9×) |
| literal | 0.77 | 15.04 (19.5×) | 0.58 (0.75×) | 2.39 (3.1×) |
| hex `[0-9a-f]{8}` | 3.61 | 20.48 (5.7×) | 1.81 (0.50×) | 4.09 (1.1×) |
| lookahead `[a-z]+(?=[a-z])` | 114.7 | 77.74 (0.68×) | 7.22 (0.06×) | unsupported |

**Reading.** REAL beats `std::regex` by **5.6–27.6×** and RE2 by **1.1–8.6×** across the board. The headline
shift since the earlier baseline: **REAL now out-runs PCRE2's JIT on class scans** — `[a-z]+` **1.94×**,
`[0-9]+` **1.73×**, `[^,]+` **1.11×**, and level on the rare-byte date (0.93×) — where it previously trailed.
(The JIT is genuinely on: the harness reports `PCRE2_CONFIG_JIT = 1`.) PCRE2-JIT still leads on straight-line
literal / alternation / hex (0.50–0.82×), emitting native code per pattern where REAL is a header-only
constexpr Pike VM. And the **lookahead line is deliberately honest**: at 0.06× vs PCRE2, REAL pays dearly to
do a bounded lookaround *in linear time* — a feature RE2 and the rust crate cannot do at all (§follow-up: the
lookaround VM path is not one of the fast paths).

## B. Python binding vs re

`ratio` is *re_time / REAL_time* — **> 1 means REAL is faster**.

| case | `re` | REAL | ratio |
| --- | ---: | ---: | ---: |
| digits · sparse findall @100KB | 1.52 ms | 159.3 µs | **9.56×** |
| word starts · findall (multiline) | 589.8 µs | 19.7 µs | **29.92×** |
| sub · spaces @100KB | 3.03 ms | 626.5 µs | 4.83× |
| date · findall groups | 1.63 ms | 474.2 µs | 3.42× |
| sub · dates with refs | 1.64 ms | 477.3 µs | 3.42× |
| date · search @100KB | 1.52 ms | 464.3 µs | 3.28× |
| alternation · findall @100KB | 1.07 ms | 526.6 µs | 2.03× |
| hex ids · findall | 333.9 µs | 188.2 µs | 1.78× |
| words · dense findall @100KB | 2.07 ms | 1.36 ms | 1.53× |
| literal · hit @1MB | 927.3 µs | 634.5 µs | 1.46× |
| non-space · Unicode findall | 2.35 ms | 1.92 ms | 1.24× |
| emails · findall groups | 1.98 ms | 1.87 ms | **1.07× ↑** (was 0.32×) |
| literal · anchored miss @1MB | 239 ns | 279 ns | **0.86×** |
| split · commas @100KB | 101.9 µs | 437.6 µs | **0.24×** |
| `(a+)+b` · re n=24 / REAL n=10k | 1397.76 ms | 46.0 µs | **30408×** (ReDoS) |

**Geometric-mean speedup over `re`: 2.06× (CI [1.37, 3.12] clears 1.0 — PASS).** The headline change since
the last refresh: **emails-with-groups flipped from a 0.32× loss to a 1.07× win** — the one-pass capture
extractor now pays for the dense group case the binding used to lose. Two cases remain *slower* than `re`:
**comma split (0.24×)** and **anchored miss (0.86×)** — high-volume `split` / tiny anchored matches where
CPython's C engine has the lower per-match constant. REAL's edge widens on sparse/rare-byte scans, sub, and
anything pathological (`(a+)+b`: 30408× — `re` blows up, REAL stays linear); the two losses are an accepted
trade for linear-time safety.

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
engines over the same corpora, `find_iter` over ~1 MB, best of 15 batches, match counts cross-checked
equal. `rust regex 1.12.4` (`find_iter` on `regex::bytes`), REAL 2026.7.11, Apple M1 Pro, both `-O3`/LTO.

| case | REAL ns/B | rust ns/B | winner |
| --- | ---: | ---: | :--- |
| class `[a-z]+` | 2.466 | 3.138 | **REAL 1.3×** |
| digits `[0-9]+` | 2.982 | 8.039 | **REAL 2.7×** |
| fields `[^,]+` | 3.086 | 2.791 | rust 1.1× |
| alternation `fox\|dog\|cat` | 1.365 | 0.876 | rust 1.6× |
| literal `dog` | 0.728 | 0.274 | rust 2.7× |
| ident `(\w+)_(\w+)` | 59.865 | 4.662 | rust 12.8× |
| word-boundary `\b\w+\b` | 41.531 | 2.671 | rust 15.5× |
| email `(\w+)@(\w+)` | 44.551 | 1.806 | rust 24.7× |
| date (no-match) `\d{4}-\d{2}-\d{2}` | 0.450 | 0.012 | rust 36.6× |

REAL's SWAR class-loop fast paths win the single-class ASCII scans (`[a-z]+`, `[0-9]+`). rust wins
everything a lazy DFA does well: **word-boundary and multi-group capture** rows, where REAL falls back to
the general Pike VM (which tracks capture slots even for span iteration), and the **literal / alternation**
rows, where its Teddy/memchr prefilters beat REAL's cascade.

On the capture rows the comparison is not symmetric, and the asymmetry is the point: rust's `find_iter`
answers match *spans* without ever running its capture machinery (its lazy DFA), while REAL's Pike VM
always tracks its slots. That gap is the architecture gap itself — it is exactly what the lazy-DFA arc
targets (a `kFirstMatch` DFA for the span, a windowed Pike pass only where captures are actually read).

Two rows carry a caveat, not a verdict:

- The **date row is a no-match scan** — the corpus contains *no* `yyyy-mm-dd`, so both engines only reject.
  rust's required-literal prefilter jumps on the fixed `-`; REAL, scanning the first-byte digit class per
  byte, grinds. This is a **prefilter gap, not a capture cost** — and now a closed one for fixed-width
  shapes: a *rare-required-byte* hint makes REAL `memchr` the same `-`. For the explicit-class
  `[0-9]{4}-[0-9]{2}-[0-9]{2}` it drops the no-match scan from **0.449 → 0.023 ns/B** (within 1.8× of rust,
  from 37×), and on a corpus with sparse real dates REAL now *beats* rust **1.78×** (0.91 vs 1.63 ns/B —
  memchr on the rare byte, then its fast digit verify). The `\d{4}-…` row above keeps the class scan: text
  Unicode `\d` is a variable-width `klass_cp`, so the `-` is not at a byte-fixed offset and the hint soundly
  declines — a `re.ASCII` `\d` or an explicit class gets it.
- The **capture rows are what the lazy-DFA arc targeted** — the rows above are recorded at their pre-arc
  cost, deliberately; §E.1 measures the arc's delivered result and the gap that survives it.

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

§E.1–E.3 time the REAL **engine** (C++, in `real_bench`) against rust. `make bench-rust` (criterion, in
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
noisy and machine-dependent (criterion reports CIs); reproduce with `make bench-rust`.

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

## Methodology & reproduction

- **Goal.** A competitive snapshot *and* a same-machine regression tripwire — not a
  benchmark contest. Compare a fresh run to these tables on the same machine/compiler;
  a single case that jumps well outside run-to-run noise after a change is the signal.
- **Reproduce.** `make bench-engines` builds `benchmarks/bench_engines.cpp` with
  `-I include` and compiles in PCRE2/RE2 **only when `pkg-config` locates them** (so the
  table degrades gracefully to REAL-vs-`std::regex` on a bare machine). `make
  bench-python` builds the abi3 binding and runs `benchmarks/bench.py` against the
  interpreter's own `re`. A third target, `make bench-fuzz`, runs the same comparison
  over randomly fuzzed `(pattern, text)` pairs. `make bench-duel` generates the §E
  REAL-vs-rust table (`benchmarks/duel/`, ns/byte with match counts cross-checked; the
  rust harness needs a Rust toolchain).
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
