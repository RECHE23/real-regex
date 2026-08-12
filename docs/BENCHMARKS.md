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

> ### Read this before reading any delta below
>
> Every **absolute** number in this document (the `ns/B` columns, the ratios against `std::regex`,
> PCRE2-JIT and RE2) is a straightforward measurement and stands as published.
>
> Every **before/after delta** in the version notes below — "−26.8 %", "+15.3 %", "+5.1 %" — was
> produced by compiling twice and comparing. That method has since been calibrated **on this very
> harness** and found to report **double-digit deltas on rows that provably cannot be affected**:
> deleting 21 lines of compile-time-only code moved `digits [0-9]+` by +16.7 %. The cause is that
> `real` is header-only, so a single build is one sample from a distribution of code layouts that
> belongs to the consumer, not to us.
>
> **`docs/MEASUREMENT.md` is now the authority on what a timing claim here is allowed to say.** It
> gives each row's measured noise floor — they range from **0.1 % to 7.3 %** — and lists, claim by
> claim, which figures in the history below survive contact with those floors and which do not. Two
> decision rules this project used are retired there, including "same direction on both ISAs means
> real work".
>
> Deltas in the stamps below are kept rather than deleted: they are the record of what was tried, and
> the refutations among them are the most useful part. Treat each as *one draw*, not as the change's
> property, and re-judge with `make bench-layout` before relying on one.

## Conditions of this baseline

| | |
| --- | --- |
| Version | REAL `2026.8.13+` — **A veto this repository wrote for exactly one case was crossed by v2026.8.12, and this train puts it back.** `matrix-gate`'s `date dense` cell fails when the routed path is slower than the core it replaces; v2026.8.12's inner-literal batch filler made it so — **route 2.883 against core 2.563**, where v2026.8.11 read 2.617 / 2.584 — and the release shipped because a hand-picked subset of gates was run instead of the canonical twenty-four. The route's sticky abandon was working; the WALK was not listening, so a route that had given up on the haystack was retried on every match (**3637 attempts against 7**) and each attempt was a wasted memmem before the per-match path did the real work. The filler now disarms for the rest of the walk when the route abandons: **`date dense` 2.883 → 2.609**, its pre-train value, with `email dense` unchanged at 2.044 against the core's 8.730 and `\w+@\w+` still batched at 0.251 engine entries per match. **MATRIX CLEAN.** **THE TABLES BELOW ARE UNCHANGED AND THAT IS A MEASURED CLAIM, not an omission:** of the five commits since v2026.8.12 only one touches a hot path, and its judgement against calibrated floors read **22 of 26 rows indistinguishable on BOTH ISAs**, every REAL row being a per-call one — a regime whose fixed cost is 0.0004 ns/byte over the 100 KB corpora §A measures. **NOT IN THESE TABLES, for want of a row rather than for want of a number:** the slot storage is now taken by rvalue reference instead of by value, which removes one of two bulk copies per call (gcc lowered them to two `rep movsq`, 12.15 % and 10.20 % of the inlined per-call path). Judged on both instruments — **x86-64 −13.2 % / −10.0 % / −9.6 %** and **arm64 −11.8 % / −8.2 % / −7.9 % / −7.2 %** on the per-call rows, everything else indistinguishable, no cross-row toll — and this is the first change this project has judged on x86-64 at all: that host's floors fell from 27–59 % to **1.8–8.3 %** once its governor was set to `performance`, so the same change was unjudgeable there a day earlier. **§B, RE-MEASURED AND UNMOVED** over three passes: five headline rows inside 2 % (`words · findall @100KB` 2.16–2.24× against 2.13×, `digits · sparse` 12.22–12.33× against 12.33×, `literal · hit` 11.79–11.82× against 11.89×, `anchored miss` 0.91–0.92× against 0.90×, `date · search` 428–436× against 432.86×), the sixth being that section's known-unstable `sub · dates with refs` at 57.35–79.68× across the three, straddling the 56.84–76.08× spread already documented for it. **AND ONE CONFIDENCE WITHDRAWN WITHOUT ANY NUMBER MOVING:** the Aho-Corasick gate arbitrates on candidate density, and that quantity provably cannot arbitrate — holding it FIXED and varying only the fraction of candidates that complete flips the verdict (arm64 0.87× → 2.19×, x86-64 0.56× → 1.90×), because a false start punishes the cascade while a match rewards it. No constant was retuned: the fix needs a second quantity, and retuning against those tables would move the error rather than remove it. See the gate's own note. |
| Machines | §A on **two ISAs**: devbox (`x86-64`, g++ 13.3.0) *and* Apple M1 Pro (`arm64`, Apple clang 16, **on AC power** — see `docs/MEASUREMENT.md` §3.5 for why the state is declared and why its cost must not be assumed). §B / §E on M1 Pro (§E's x86-64 leg noted inline where it diverges — see §E). §multi-pattern measured on **x86-64 devbox** (g++ 13.3, RE2 + Hyperscan 5.4) |
| Engines | `std::regex`; **PCRE2 10.47, JIT on, both ISAs** (built from source on x86-64 to pin the exact version — and the pin only applies when `PKG_CONFIG_PATH` points at that build, since the recipe resolves the library through `pkg-config` and the system package otherwise wins silently; `make bench-engines` now prints the version it actually LINKED, because this document named 10.47 for a leg that had measured 10.42 and nothing in the output could contradict it); RE2 (10.0 on x86-64, 11.0 on arm64 — version-differs-by-leg, uncontested given the margins). Multi-pattern: RE2::Set, Hyperscan (optional). §E: rust `regex` 1.12.4 |
| Python | CPython 3.14.6, `re` (stdlib) vs the in-place REAL `2026.8.13` extension (§B re-measured at this stamp over three passes and unmoved; five of six headline rows inside 2 %, the sixth being its known-unstable `sub · dates with refs` — see §B) |
| Method | §A: median of N = 30 paired batches, bootstrap CI, **three full runs per ISA with the minimum taken per cell** (both ISAs, this re-stamp — one run does not survive the x86-64 container's episodic interference); match counts equal on every case, both ISAs. §E: best-of-15, REAL `count_matches` vs rust `find_iter`/`captures_iter`, match counts equal. §multi-pattern: best-of-7, `make bench-multipattern`. **Every ratio below is computed from the raw ns/B pair, and `benchmarks/verify_bench_ratios.py` re-derives all of them plus §A's reading bullets — `make check-bench-ratios`, step 7b of the local gate, part of `gate-doc` whenever this file is touched, and a step of the **Docs-site** workflow — which is the one CI net with no `paths-ignore`, so it fires on the doc-only pushes that edit this file.** That wiring is new, and the sentence it replaces was not true when it was written: the script was called from nothing at all, and running it for the first time failed on two cells — a third once its rounding rule was made exact — while every range, per-row pair and count in §A's bullets had been stale for three stamps. A checker nothing runs is a claim, not a check |

## A. C++ engine throughput

Each engine compiles the pattern once, then counts all non-overlapping matches over the same corpus; only
the scan is timed. `ns/B` is nanoseconds per corpus byte (lower is better). `(x)` is *engine_time /
REAL_time* — **> 1 means REAL is faster**. Match counts agreed across all four engines on every case, on
both ISAs, on the same `47dccc2` tree for this re-stamp.

**The two weakest rows in this document are no longer the two weakest rows.** `fields [^,]+` and `.`
were 0.58× and 0.81× against PCRE2-JIT on arm64, and profiling said why: neither was a scan
measurement. `[^,]+` cost ~19 ns of fixed per-match overhead plus ~0.086 ns/byte — flat at
18.6 / 19.3 / 18.8 ns per match for fields of 4, 8 and 16 characters — so on this harness's 4–7
character fields the fixed cost WAS the measurement. The cause sat in the hint table rather than in
any loop: batching requires `greedy_class_loop` or `greedy_cp_class`, this shape sets neither
(`codepoint_class_ascii`), and it therefore crossed a full route entry per match where the other two
class routes cross one per sixteen.

| | arm64 | x86-64 |
| --- | ---: | ---: |
| `.` (emoji) | **−52.6 %**, 0.81× → **1.75×** | **−50.3 %**, 1.25× → **2.67×** |
| `fields [^,]+` | **−26.8 %**, 0.58× → **0.82×** | **−52.8 %**, 0.80× → **1.70×** |

`fields` now leads PCRE2-JIT on x86-64, and `.` leads it on both.

**What moved the other way, and what is NOT claimed about it.** Six rows regress on arm64 by 3–15 %
(`\w+` +15.3 %, `digits` +15.3 %, `\p{sc=Han}` +10.3 %, `words` +8.9 %, its ASCII witness +8.8 %,
`\p{L}+` +6.7 %) and the same rows on x86-64 read +4.3 %, +4.3 %, +0.4 %, +4.3 %, +2.9 % and
**−0.8 %**. `\w+` is +15.3 % on one ISA and −1.4 % on the other. Rows that disagree in DIRECTION
across ISAs are the placement signature this section documents; rows that agree in direction but not
in magnitude are partly that and partly the translation unit carrying one more function. This stamp
does not separate the two, and says so rather than picking whichever attribution reads better. The
delta also spans THREE commits, not one — two alternation fusions landed alongside the filler — so no
row's movement here belongs to a single change.

**Protocol for this re-stamp:** three full harness runs per ISA, each already a median of N = 30
paired batches, then the **minimum per cell across the three**. One run is not enough on either
machine, and this set says so again: arm64 `alt` spreads 9.1 % across its three runs and `anchored`
5.4 %, while `lookahead` and `fields` hold inside 2.5 %; on x86-64 it is `literal` at 7.7 % and
`digits` at 5.5 % against `hex` at 0.1 %. Note that the noisy rows are not the same ones per ISA — that
is the episode shape described under Methodology: a burst lands on whatever is scanning when it
arrives, not on a property of the row. A minimum across runs is what survives it; a mean would not.
**38 of the published REAL cells came from a run other than the first** — 22 on arm64, 16 on x86-64 —
which is the protocol earning its cost rather than performing it.

**The harness itself was the biggest measured effect in this train, and it is why this table has no
new row.** An accented-literal case was added to the Unicode list -- one brace-initialized struct,
no new code -- and on gcc/x86-64 that alone moved `words` **+27.7 %**, its ASCII witness +26.9 %,
`digits` +23.7 % and `[à-ÿ]+` +16.5 %, with arm64/clang flat throughout (same engine tree, both
binaries built and interleaved five times). The row was reverted. `bench_engines.cpp` already carried
"DO NOT INSTRUMENT THIS FILE" on the evidence of a runtime switch worth 12 %; it now also says that a
*data row* spends the same budget, because the unit's cost is what it CONTAINS and not only what it
runs. The consequence for this document is concrete: a shape can be too expensive to measure here,
and the accented literal is one — its figure is given in Methodology from an isolated probe, labelled
as such, rather than bought at the price of every other row's comparability.

**Two earlier sets were also discarded rather than published.** An arm64 set ran while the machine
was building and reads slower on exactly the longest-scanning rows, and contamination only inflates,
so it cannot be mixed into a minimum. And an x86-64 set linked **PCRE2 10.42**, the container's
system copy, instead of the pinned 10.47: that alone moved `anchored`'s PCRE2 cell from 0.52 to 0.77
— a 1.01× that would have been published as **1.50× in REAL's favour**. The harness prints the
version it LINKED for this reason, and that one line has now caught the same trap **three times** —
most recently on the x86-64 leg of this very stamp, where `anchored`'s PCRE2 cell read 0.77 (a 1.50×
in REAL's favour) before the line was read, and 0.52 (1.01×, parity) after the fix.

**The cause is now known, and it is not `PKG_CONFIG_PATH`.** That variable is enough to COMPILE against
the pinned 10.47, which is why the trap survives a check that only inspects `pkg-config --modversion`:
the link carries no rpath, so the dynamic loader resolves `libpcre2-8.so.0` to the container's system
10.42 at RUN time. `LD_LIBRARY_PATH=<pinned>/lib` alongside `PKG_CONFIG_PATH` is what makes the printed
version read 10.47. Do not trust a compile-time probe here; trust the line the harness prints.

**x86-64** — devbox, g++ 13.3.0, N = 30 × 3 runs, PCRE2 10.47-JIT, RE2 10.0:

| case | REAL ns/B | std::regex | PCRE2-JIT | RE2 |
| --- | ---: | ---: | ---: | ---: |
| words `[a-z]+` | 1.92 | 28.71 (**14.94×**) | 6.44 (**3.35×**) | 28.39 (**14.78×**) |
| digits `[0-9]+` | 1.41 | 24.68 (**17.55×**) | 3.88 (**2.76×**) | 16.78 (**11.93×**) |
| fields `[^,]+` | 3.05 | 25.04 (**8.20×**) | 4.95 (**1.62×**) | 23.80 (**7.79×**) |
| single `[a-z]` | 5.57 | 66.43 (**11.94×**) | 24.02 (**4.32×**) | 90.93 (**16.34×**) |
| words `[a-z]{4,}` | 1.42 | 23.80 (**16.75×**) | 4.11 (**2.89×**) | 17.39 (**12.24×**) |
| words `[a-z]++` | 1.87 | 31.24 (**16.70×**) | 6.60 (**3.53×**) | unsupported |
| alternation `the\|fox\|dog` | 1.67 | 31.36 (**18.81×**) | 2.31 (**1.38×**) | 10.59 (**6.35×**) |
| date `{4}-{2}-{2}` | 0.87 | 18.36 (**21.20×**) | 0.61 (0.71×) | 4.09 (**4.72×**) |
| hex `[0-9a-f]{8}` | 1.41 | 20.64 (**14.64×**) | 1.90 (**1.35×**) | 4.08 (**2.90×**) |
| literal | 0.37 | 15.43 (**41.97×**) | 0.61 (**1.65×**) | 2.45 (**6.66×**) |
| anchored `^[a-z]+$` | 0.51 | unsupported | 0.52 (**1.02×**) | 1.78 (**3.46×**) |
| lookahead `[a-z]+(?=[a-z])` | 7.47 | 76.91 (**10.30×**) | 6.88 (0.92×) | unsupported |

**arm64** — Apple M1 Pro, Apple clang 16, N = 30 × 3 runs, PCRE2 10.47-JIT, RE2 11.0:

| case | REAL ns/B | std::regex | PCRE2-JIT | RE2 |
| --- | ---: | ---: | ---: | ---: |
| words `[a-z]+` | 1.21 | 94.10 (**77.70×**) | 2.32 (**1.92×**) | 13.97 (**11.53×**) |
| digits `[0-9]+` | 0.89 | 83.70 (**94.50×**) | 1.45 (**1.64×**) | 8.48 (**9.58×**) |
| fields `[^,]+` | 2.29 | 75.73 (**33.07×**) | 1.89 (0.82×) | 11.10 (**4.85×**) |
| single `[a-z]` | 4.37 | 66.94 (**15.31×**) | 8.13 (**1.86×**) | 42.13 (**9.64×**) |
| words `[a-z]{4,}` | 0.91 | 73.35 (**80.19×**) | 2.04 (**2.23×**) | 8.34 (**9.12×**) |
| words `[a-z]++` | 1.20 | unsupported | 2.36 (**1.97×**) | unsupported |
| alternation `the\|fox\|dog` | 1.77 | 117.90 (**66.55×**) | 1.58 (0.89×) | 6.40 (**3.61×**) |
| date `{4}-{2}-{2}` | 0.55 | 72.59 (**131.47×**) | 0.39 (0.71×) | 3.42 (**6.19×**) |
| hex `[0-9a-f]{8}` | 1.42 | 81.26 (**57.14×**) | 1.26 (0.88×) | 3.42 (**2.41×**) |
| literal | 0.20 | 31.12 (**151.97×**) | 0.50 (**2.42×**) | 1.43 (**6.99×**) |
| anchored `^[a-z]+$` | 0.32 | unsupported | 0.42 (**1.31×**) | 2.22 (**6.95×**) |
| lookahead `[a-z]+(?=[a-z])` | 4.50 | 160.22 (**35.63×**) | 3.67 (0.82×) | unsupported |

**The arm64 leg was re-measured on AC power, and the premise for re-measuring it was wrong.** This
column was taken on 2026-08-07 (tree `47dccc2`) with the machine's power state unrecorded, and after an
M1 was caught throttling ~29 % on battery elsewhere in this project, these figures were declared
uniformly too slow pending a fresh run. **They are not.** Three passes on AC — same recipe, same
protocol, minimum per cell, `linked: pcre2=10.47 2025-10-21`, match counts equal on every row and every
engine — reproduce the published column within **−2.4 % to +3.6 % on REAL** and **−6.3 % to +0.9 % on
the three competitors**, every row inside the 1.00×–1.08× arm64 inter-run amplitude this document
already declares. The largest single deviation is RE2 on `alternation` (−6.3 %), a third-party column,
which is drift and not a change.

**The published cells therefore STAND and are not replaced by the confirming run**: swapping in a
second set that differs by less than the declared noise would advertise a change that did not happen.
The conditions of that run are on the record rather than assumed — AC power, Apple clang 16,
samples = 30 × 3 passes, one-minute load 3.9 / 4.8 / 5.1 at the start of each pass. **That is not an
idle machine, and it is the useful direction to be imperfect in**: contamination only inflates, so a
run taken under load that comes out no slower cannot be flattering the column it confirms.

What the fresh passes do settle is `literal`, whose cell read 0.21 while its own two precise ratios pin
the raw value at **0.2046–0.2048 ns/B** (31.12 ÷ 151.97 and 1.43 ÷ 6.99 — the PCRE2 ratio gives 0.2066,
which agrees within the ±0.005 a two-digit `0.50` cell allows and cannot narrow it). Displayed as 0.21
the largest `std::regex` ratio arithmetic permits is 151.83, below the 151.97 printed beside it, so the
row could not be re-derived; corrected to **0.20**, the honest two-decimal rounding, consistent with all
three of its ratios under the interval rule `check-bench-ratios` applies. The AC passes read 0.197
independently. v2026.8.12's stamp rounded the same row to 0.21 and that line stays as published: it
records what that train reported, not what this cell must be.

**Reading — verdict brut, no dressing up. Every ratio in these bullets is checked against the cells
above by `make check-bench-ratios`** (local gate step 7b), because the bullets below were wrong for
three consecutive stamps while the tables were right — see each bullet's own note.

<!-- [std-regex-reading] — drop-in/std-regex-tour.md slices from here to the RE2 bullet. Placed
     BEFORE the list, not between two items: an HTML comment inside a list splits it in two when
     rendered. Same reason as the duel-reading marker further down — the wording this used to anchor
     on was the heading above, and extending that heading by one sentence broke the site build. A
     marker's own name is never spelled in brackets outside its marker: two occurrences and the
     extractor silently takes the first, which check-site-anchors refuses. -->
- **REAL ≫ `std::regex`**, always: **8.20–41.97×** on x86-64, **15.31–151.97×** on arm64 (libc++'s
  `std::regex` falls even further behind on arm64). Never below 8.20×. **These bounds have now drifted
  from the table twice in a row.** An earlier stamp read "4.1–37.6× / 23.5–139.3×"; the correction that
  replaced it read "8.4–35.1× / 15.8–147.6×" and claimed to be "regenerated with the tables rather than
  carried by hand", which is precisely what did not happen — three re-measures later, neither bound on
  either ISA matched a cell. The intention was never the mechanism; the checker is.
- **REAL > RE2**, always where RE2 supports the pattern: **2.90–16.34×** on x86-64, **2.41–11.53×** on
  arm64. (Same drift: this bullet read "3.0–18.1× / 2.4–12.1×", inventing a high bound on each ISA that
  no row of the table printed.)
- **REAL vs PCRE2-JIT: seven of twelve rows are REAL's on BOTH ISAs** — `words [a-z]+` (**3.35×**
  x86-64 / **1.92×** arm64), `digits` (**2.76×** / **1.64×**), `single` (**4.32×** / **1.86×**),
  `words [a-z]{4,}` (**2.89×** / **2.23×**), `words [a-z]++` (**3.53×** / **1.97×**), `literal`
  (**1.65×** / **2.42×**) and `anchored` (**1.02×** / **1.31×**) — that last x86-64 cell being a hair
  over parity, not a win worth leaning on. Three more are REAL's on **x86-64 only**: `fields`
  (**1.62×** / 0.82×), `alternation` (**1.38×** / 0.89×) and `hex` (**1.35×** / 0.88×). PCRE2 keeps
  exactly two, on both ISAs: `date` (0.71× / 0.71×) and `lookahead` (0.92× / 0.82×), the shape it wins
  by backtracking. **This bullet said "five rows" and listed pre-v2026.8.11 ratios for every one of
  them**: it filed `alternation` as a PCRE2 win at 0.94× x86-64, where the table has read a REAL win
  since v2026.8.11 announced that crossing, and `literal` on arm64 at 1.90× against the table's
  **2.42×** — which v2026.8.12's own stamp published as a gain. The three rows that same train ADDED
  to the table (`single`, `words [a-z]{4,}`, `words [a-z]++`) were never counted here at all.
- **What moved these rows is one accessor, and it moved every route that shares it.** `class_table`
  runs once per MATCH on a walk, and it was asking two storage-mode questions — both invariant for
  the whole walk — before the row-key question that actually varies. Bisected on executed conditional
  branches (deterministic; the x86-64 container's 3 % floor cannot resolve a 6 % step) to the commit
  that introduced the lazy per-class row fill, where the accessor went from 1 branch per call to
  exactly 3.00. Asking the key first: **`lookahead` 4.24 → 3.75 arm64 and 8.08 → 7.50 x86-64**,
  **`words` 2.35 → 2.22 and 3.39 → 2.87**, `digits` 1.44 → 1.38 and 1.89 → 1.79, `date` 0.53 → 0.51
  and 0.71 → 0.68. The same reordering then applied to the two code-point accessors, which carried
  the identical shape, and moved every §Unicode row on arm64 (−1.4 % to −6.7 %) and seven of nine on
  x86-64 (to −14.2 % on `\p{N}+`). **How the cold half is packaged decided both, and in opposite
  directions**: `class_table` wanted it outlined behind a `noinline`, the code-point pair is wrecked
  by exactly that (+39 % on `[à-ÿ]+`) and is also wrecked by merely putting it in an `always_inline`
  function of its own (+10–17 % on gcc/x86-64 §A rows that never touch it, from the translation
  unit's inline budget). Neither is predictable from any counter this repository collects — all four
  of them said the `noinline` was an improvement.
- **`fields` did not move, and that is now the finding rather than the mystery.** It was published as
  an open regression at the last stamp (+9.8 % x86-64 since v2026.7.55). Isolated in a probe that
  scans the same corpus with the same pattern on the same machine, that regression **does not
  reproduce at all** — HEAD measured 4.8 % FASTER than v2026.7.55 there — and the accessor fix that
  moves every other row measured **−25 % on x86-64 in the probe and +1.0 % in this table**. Two
  independent disagreements, both on this row alone. Whatever this row is measuring in the
  multi-engine harness binary, it is not what the isolated scan does, so **do not read it as scan
  throughput and do not chase it from the engine side**. The next move on it is a harness question —
  median-vs-minimum, or layout in a binary that links four engines — not a routing one.
- **The lookahead line is PCRE2's on both ISAs, and the arm64 gap is a trade this project chose rather
  than a loss it suffered.** The cells read `lookahead` (**0.92×** x86-64 / **0.82×** arm64) — REAL
  7.47 / 4.50 ns/B against PCRE2's 6.88 / 3.67 — against the pre-P3c general-VM order (~92 / ~48 ns/B), and against
  8.08 / 4.24 at the previous stamp of this table. **This bullet claimed "within 3 % of PCRE2-JIT on
  arm64 (0.97×)", and that stopped being true at v2026.8.12** — whose stamp published the move itself:
  **+18 % on this row in exchange for −91.8 % on `find_iter`**, because the trailing-lookaround rework
  pays on `count_matches` to return on iteration. Reading 0.97× here while the release notes said 0.82×
  was the drift showing its cost: it hid a trade the project had already disclosed. REAL does a
  **bounded lookaround in linear time**;
  PCRE2 is faster here but by **backtracking** (itself ReDoS-able on a crafted lookaround), and
  **RE2 and the rust crate cannot compile the pattern at all** (`unsupported`). `find_iter` / Python
  `finditer` do not get the P3c fast path by construction (return type fixed at compile time so pure
  `[a-z]+` does not regress) — this row is `count_matches` only, stated plainly so it is not read as
  a `find_iter` number.
- **The gauge that makes the above readable, and it is looser than this bullet used to claim.**
  `std::regex` and RE2 are third-party constants here, so their columns are the drift witness. This
  bullet read "RE2 lands within 1 % on every x86-64 row" and listed `28.24, 16.89, 22.74, 10.39, 4.08,
  4.08, 2.38` as the current values — **a set the table above does not contain**, since it was re-measured
  after the bullet was written. Against the table's actual RE2 x86-64 cells the same seven rows read
  **28.39, 16.78, 23.80, 10.59, 4.09, 4.08, 2.45** against that older `28.26, 16.81, 22.95, 10.39, 4.09,
  4.09, 2.38`: five rows inside 0.5 % (`words` +0.5 %, `digits` −0.2 %, `date` 0.0 %, `hex` −0.2 %, and
  `alternation` the outlier of those at +1.9 %), with **`fields` +3.7 % and `literal` +2.9 %** outside any
  1 % claim. So the license holds for a −16.8 % REAL row but not for a 3 % one, and the two rows that
  spend it are `fields` — the row the bullet three above says is not measuring scan throughput in this
  binary at all — and `literal`, the row whose ratios needed a third decimal. Both point at the harness,
  not at RE2.

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
| date · search @100KB | 1.16 ms | 2.7 µs | **434.97×** |
| date · findall groups | 1.24 ms | 3.2 µs | **383.30×** |
| sub · dates with refs | 1.25 ms | 21.4 µs | **59.68×** ↑ (was 45.58×) ⚠ |
| word starts ASCII · findall (multiline) | 442.6 µs | 17.3 µs | **25.80×** ↓ (was 28.57×) |
| digits · sparse findall @100KB | 1.16 ms | 93.0 µs | **12.55×** ↑ (was 4.66×) |
| literal · hit @1MB | 695.5 µs | 58.7 µs | **11.89×** |
| literal · miss @1MB | 696.4 µs | 58.6 µs | **11.85×** |
| sub · spaces @100KB | 2.28 ms | 458.8 µs | **4.96×** ↑ (was 3.34×) |
| words · findall @1KB | 16.1 µs | 4.9 µs | **3.26×** ↑ (was 1.50×) |
| alternation · findall @100KB | 801.9 µs | 308.0 µs | **2.61×** |
| words · findall @10KB | 157.7 µs | 61.8 µs | **2.51×** ↑ (was 1.32×) |
| words · findall @100KB | 1.54 ms | 702.4 µs | **2.19×** ↑ (was 1.26×) |
| words · dense findall @100KB | 1.51 ms | 705.3 µs | **2.15×** ↑ (was 1.25×) |
| words · findall @1MB | 16.11 ms | 8.39 ms | **1.92×** ↑ (was 1.20×) |
| emails · findall groups | 1.49 ms | 834.2 µs | **1.79×** |
| split · commas @100KB | 77.0 µs | 46.3 µs | **1.66×** |
| hex ids · findall | 248.4 µs | 157.6 µs | **1.58×** |
| non-space · Unicode findall | 1.73 ms | 1.20 ms | **1.44×** ↑ (was 1.09×) |
| literal · anchored miss @1MB | 177 ns | 198 ns | **0.90×** ↑ (was 0.82×) |
| `(a+)+b` · re n=24 / REAL n=10k (prefilter) | 1064.35 ms | **687 ns** | **~1.5×10⁶×** (ReDoS) |

**RE-MEASURED AGAIN AT `2026.8.12+`, AND THIS TRAIN DID NOT MOVE IT EITHER.** Three passes: `words ·
findall @100KB` reads 2.15 / 2.14 / 2.16× against 2.13× here, `digits · sparse` 12.22 / 12.28 / 12.37×
against 12.33×, `literal · hit` 11.60 / 11.86 / 11.77× against 11.89×, `anchored miss` 0.90× on all
three, `date · search` 437.18 / 432.03 / 432.72× against 432.86×. Five rows within 2 % on every pass,
one of which was taken under a background load of 14 — which is itself the useful reading: this
section's ratios are PAIRED and adjacent, so a shared load moves both engines and the ratio holds.
**The sixth row is `sub · dates with refs`, and it is this section's known-unstable one:** 46.89×
(loaded), then **79.06×** and **84.62×** on two quiet passes, against 65.71× here and the 56.84–76.08×
spread this section already documents for it. That is ABOVE the documented spread, and it is NOT
claimed as a movement: nothing in this train touches the `sub`-with-group-references path in a
direction that would explain +29 %, and the row's own history is why the spread is documented at all.
Its REAL column moved 38.9 → 16.2 → 15.0 µs across the three passes, which is the load and not the
engine. **The v2026.8.11 stamp's own re-measurement, which withdrew a claim the v2026.8.11 release
notes made, is unchanged below.** Those notes said §B was one train stale and that "this train's batching changes
exactly the `findall`/`sub` regime §B measures, so its numbers understate the binding". They do not. Every
row reads within 2 % of the figures below: `date · search` 434.97× → 432.86×, `digits · sparse` 12.55× →
12.33×, `words · findall @100KB` 2.19× → 2.13×, `literal · hit` 11.89× unchanged. `sub · dates with refs`
reads 65.71×, which falls inside the 56.84–76.08× spread this section already documents for it, so it is
that row's known instability rather than a movement.

The reason is worth keeping, because it bounds what §B can ever show: this table's regime is dominated by
the **per-call** cost of crossing into Python, not by the scan. v2026.8.11's engine gain was −5.2 % on one
C++ row of eighteen; at this boundary that is invisible. So an engine train that touches only scan cost
should be expected NOT to move §B, and "§B is stale" is a weaker debt than four consecutive release-note
sets implied. What WOULD move it is per-call work — which is what v2026.8.6 (20–25 % off the fixed per-call
cost) did, and it shows below.

The instrument was null-calibrated before this reading rather than trusted: `collect_pair` alternates
subject and reference within each of 40 samples and takes the median of per-sample ratios, but always in
the same order, so a position bias would not cancel. Pointing both sides at the SAME operation — true ratio
exactly 1.0 — five times gave medians of 0.9958, 1.0006, 0.9994, 0.9981 and 1.0010: no consistent sign, and
under 0.4 % at the median. The paired design also absorbs machine state that makes `bench_layout.py`
unusable on a loaded host, which is why this table is measurable where §A's floors are not.

**This table was thirty trains stale, and it understated REAL by roughly a factor of two on the
`findall` family.** It carried a `2026.7.55` stamp while the engine ran at `2026.8.10`, which
`make version-check` had been warning about, and four consecutive release-note sets said "§B is NOT
re-measured for this train". The `words · findall` rows go 1.20–1.50× → **1.92–3.26×** and
`digits · sparse` 4.66× → **12.55×**, which is the shape of the two trains in between: v2026.8.6 took
20–25 % off the fixed per-call cost, and v2026.8.10 gave the `.`/negated-class family a batch filler —
and `findall` over a large corpus is exactly the regime both of those serve.

One row went the other way (`word starts ASCII` 28.57× → 25.80×) and is published as it reads. One row
is still below parity, and it is the same one as before: `literal · anchored miss` at 0.90×, an
anchored no-match at 1 MB where the whole measurement is ~180 ns of call overhead.

**⚠ `sub · dates with refs` is not stable at this precision.** Three runs read 56.84× / 76.08× / 59.68×
— a 33.8 % spread, and run 1's reported CI of [56.80–60.59] does not contain run 2's value. The median
is published, and the disagreement is stated rather than hidden behind a bootstrap interval that this
row's own re-runs contradict. Every other cell holds inside 2 %.

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
linear-time-guarantee axis. This duel is honest about where REAL loses: the same patterns run through both
engines over the same corpora (~1 MB), best of 15 batches, match counts cross-checked equal.
**Stamp (re-stamp):** REAL **`1ed00bf`** (v2026.8.3+, both sides re-run this pass; the previous stamp was `3cd9c81`/v2026.7.55 — the perf train: one-search exact-literal route,
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
| literal `dog` | 0.318 | 0.628 | **REAL 2.0×** |
| alternation `fox\|dog\|cat` | 1.053 | 1.297 | **REAL 1.2×** |
| class `[a-z]+` | 1.648 | 12.000 | **REAL 7.3×** |
| digits `[0-9]+` | 2.123 | 17.660 | **REAL 8.3×** |
| fields `[^,]+` | 2.948 | 9.195 | **REAL 3.1×** |
| word-boundary `\b\w+\b` | 2.722 | 10.985 | **REAL 4.0×** |
| email `(\w+)@(\w+)` | 1.829 | 5.268 | **REAL 2.9×** |
| ident `(\w+)_(\w+)` | 6.424 | 30.742 | **REAL 4.8×** |
| date no-match `\d{4}-\d{2}-\d{2}` | 0.023 | 0.012 | rust 1.9× |
| date sparse `\d{4}-\d{2}-\d{2}` | 0.059 | 0.077 | **REAL 1.3×** |
| email sparse `(\w+)@(\w+)` | 0.061 | 0.121 | **REAL 2.0×** |
| key= `key=(\w+)` | 1.072 | 1.440 | **REAL 1.3×** |

**x86-64** — devbox, g++ 13.3.0, `-O3 -flto`:

| case | REAL ns/B | rust ns/B | winner |
| --- | ---: | ---: | :--- |
| literal `dog` | 0.464 | 0.820 | **REAL 1.8×** |
| alternation `fox\|dog\|cat` | 1.471 | 2.215 | **REAL 1.5×** |
| class `[a-z]+` | 2.273 | 18.913 | **REAL 8.3×** |
| digits `[0-9]+` | 2.509 | 23.576 | **REAL 9.4×** |
| fields `[^,]+` | 4.204 | 16.100 | **REAL 3.8×** |
| word-boundary `\b\w+\b` | 4.330 | 16.751 | **REAL 3.9×** |
| email `(\w+)@(\w+)` | 3.628 | 6.325 | **REAL 1.7×** |
| ident `(\w+)_(\w+)` | 12.508 | 46.429 | **REAL 3.7×** |
| date no-match `\d{4}-\d{2}-\d{2}` | 0.017 | 0.015 | rust 1.1× |
| date sparse `\d{4}-\d{2}-\d{2}` | 0.097 | 0.102 | **REAL 1.1×** |
| email sparse `(\w+)@(\w+)` | 0.097 | 0.159 | **REAL 1.6×** |
| key= `key=(\w+)` | 1.252 | 2.161 | **REAL 1.7×** |

<!-- Keep this marker: docs/site/drop-in/regex.md slices from it, and anchoring that include on
     wording instead broke the site build once when a re-stamp rewrote the sentence below. The
     marker must stay the LAST line of this comment -- the extractor resumes at the next newline. -->
<!-- [duel-reading] -->

**Reading — verdict brut. REAL leads 11 of 12 rows on BOTH ISAs, and the row this table used to
publish as its worst deficit is now a 3.7–4.8× win.**

- **`ident (\w+)_(\w+)` was the largest published deficit anywhere in this document and it no longer
  exists.** It stood at 53.542 ns/B on arm64 and **113.178 on x86-64** against rust's 30.786 / 46.355
  — rust 1.7× / 2.4× ahead. Re-measured with the same harness, same corpus, same flags: **6.424 and
  12.508**, an 8.3× and 9.0× improvement, turning rust 2.4× ahead into REAL 3.7× ahead. Nothing in
  this train touched it; it was carried by the capture and prefilter work of the trains between, and
  nobody re-ran the row. Three other rows crossed the same way — `date sparse` (rust 1.7× → REAL
  1.3×), `email sparse` (tie → REAL 2.0×), and `email` (1.1× → 2.9×) — and `word-boundary` doubled
  its margin, 2.0× → 4.0×.
- **The one row rust still wins is `date no-match`**, 0.023 against 0.012 ns/B on arm64. Both engines
  cross a 1 MB corpus in under 25 microseconds without a match; the ratio is on work that has already
  collapsed to nothing, and it is reported rather than dropped for exactly that reason.
- **Read this against the previous stamp as a caution about stamps, not a victory lap.** A table
  stamped at v2026.7.55 was still being read as current a dozen trains later, and it understated the
  engine by up to 9×. §A had the same problem this month. A benchmark table that is not re-run is not
  a measurement, it is a claim about the past.

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
  on the same arm64 M1 Pro, one group at a time; NOT under the interleaved A/B protocol the table above
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

**Stamp.** REAL `2026.8.9+` (tree `47dccc2`), both ISAs re-measured for this stamp, on the same
three-runs-per-ISA / minimum-per-cell protocol as §A. **arm64** table below: Apple M1 Pro, Apple
clang 16, `-O2`, N = 30 (`make bench-engines`). **x86-64**, same harness and N, g++ 13.3 with PCRE2
10.47 (from source — the version the binary LINKED, printed by the harness) and RE2 10.0: `\w+` mixed
**3.399** (pcre2 **1.02×**, re2 **1.47×**), `\p{L}+` CJK **3.375** (0.84× / re2 **5.70×**), `\p{N}+`
**4.291** (0.60× / **1.74×**), `sc=Han` **5.704** (0.81×), `scx=Cyrl` **6.615** (0.97×), `(?i)café`
**0.668** (0.90× / **4.19×**), `[à-ÿ]+` **3.561** (**1.65×** / **7.07×**), literal `你好` **0.842**
(pcre2 **1.22×** — the same crossing as arm64), `.` emoji **3.870** (pcre2 **2.67×** — this stamp's
largest move, see §A), ascii witness **1.730** (**3.71×**). Oracle: exhaustive `\p{L}` over
U+0000..10FFFF (surrogates skipped) — **0 mismatch**.

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
| `\w+` (mixed-script) | 2.278 | 60.77 (**26.68×**) | 1.86 (0.82×) | 3.21 (**1.41×**) | 16218/5406/16218/5406 ⚠ |
| `\b\w+\b` (mixed-script) | 2.085 | 57.41 (**27.53×**) | 3.04 (**1.46×**) | 3.96 (**1.90×**) | 16218/5406/16218/5406 ⚠ |
| `\w++` (mixed-script) | 2.233 | unsupported | 1.87 (0.84×) | unsupported | 16218/—/16218/— |
| `\w{2,}` (mixed-script) | 2.756 | 59.94 (**21.75×**) | 1.77 (0.64×) | 3.92 (**1.42×**) | 16218/5406/16218/5406 ⚠ |
| `\p{L}+` (CJK) | 2.045 | unsupported | 1.33 (0.65×) | 13.14 (**6.43×**) | 12904/—/12904/12904 |
| `\p{N}+` (arabic digits) | 2.036 | unsupported | 1.61 (0.79×) | 5.23 (**2.57×**) | 6250/—/6250/6250 |
| `\p{sc=Han}` (CJK) | 3.156 | unsupported | 2.14 (0.68×) | unsupported | 25808/—/25808/— |
| `\p{scx=Cyrl}` (mixed-script) | 3.581 | unsupported | 2.68 (0.75×) | unsupported | 32436/—/32436/— |
| `(?i)café` (accented) | 0.405 | unsupported | 0.34 (0.84×) | 1.31 (**3.23×**) | 3509/—/3509/3509 |
| `[à-ÿ]+` (accented) | 1.975 | 88.17 (**44.64×**) | 2.06 (**1.04×**) | 12.60 (**6.38×**) | 38599/38599/38599/38599 |
| literal `你好` (CJK) | 0.457 | 29.08 (**63.63×**) | 0.58 (**1.27×**) | 2.62 (**5.73×**) | 6452/6452/6452/6452 |
| `.` (emoji, one codepoint) | 2.267 | 58.81 (**25.94×**) | 3.83 (**1.69×**) | 18.91 (**8.34×**) | 68306/200039/68306/68306 ⚠ |
| ascii witness `[a-z]+` | 1.262 | 91.56 (**72.55×**) | 2.26 (**1.79×**) | 13.95 (**11.05×**) | 42108/42108/42108/42108 |
*(Same convention as §A: `(x) = engine_time / REAL_time`, **> 1 means REAL is faster**; **bold** marks the
PCRE2-JIT column, REAL's main competitor throughout this document. Every ratio here is computed
programmatically from the ns/B pair — `benchmarks/verify_unicode_ratios.py` re-derives and checks all of
them against this table.)*

*(This table carried the `\w+` row TWICE until this stamp — 1.946 and 2.239 ns/B, one left behind by
an earlier row replacement. `verify_unicode_ratios.py` could not catch it: each duplicate was
internally consistent with its own ns/B pair, so the check it performs was satisfied by both.)*

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
- **Measure x86-64 on the devbox, not in Docker on the laptop.** Same six cores and the same
  g++ 13.3 that builds the manylinux wheels, but the worst within-arm spread across every
  row is **1.045×** against the container's **1.98×** — the difference between an
  instrument and a lottery. Use Docker only when the devbox is unavailable, and then only
  with the workaround below. The two disagree on more than precision: on the `cp_class_hi_width`
  attribute A/B the container reported `(?i)cafe` **improving 25 %** where the devbox
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
  **+220 %**, and one row read −25 % in a container against +217 % on the devbox. **Largely cured** —
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
