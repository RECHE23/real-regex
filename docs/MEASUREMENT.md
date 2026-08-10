# How this project measures performance, and what it is therefore allowed to claim

This document exists because the previous answer to "did that change help?" was *compile it twice and
compare* — and that method was measured, here, on this repository's own benchmark, to report
**double-digit deltas on rows that cannot possibly have been affected**. Several conclusions in this
project's history were drawn with it. This page sets out the instrument that replaced it, the
calibration that licenses it, and — in §5 — which earlier claims it retires.

---

## 1. What we control, and what we will never control

`real` is **header-only**. Every route body is inlined into the consumer's translation unit, and its
address, its alignment and its I-cache colour are then decided by code we do not write and will never
see. There is no build step of ours in which those decisions are made.

So "getting a grip on code placement" is the **wrong goal for the C++ surface**. The reachable goal is
a measurement that is *insensitive* to placement, because placement is the consumer's, not ours.

One exception, and it is a real opportunity rather than a caveat: the **bindings ship compiled
artifacts** (Python wheels, the Rust `cdylib`, the Go archive). There the layout is ours, and
profile-guided optimisation and post-link layout tools (BOLT, Propeller) would be a *shipped* gain,
not merely a measurement aid. That work is not done; §6 keeps it on the list.

## 2. What "the performance of a change" means here

If a route's speed depends on where the linker happened to put it, then a single build measures **one
sample from a distribution of layouts**, and the honest estimate of what a downstream user will see is
the **expectation over that distribution** — not the number one build produced.

That is the whole doctrine. Everything below is machinery for estimating it and for refusing to claim
more than it supports.

The argument is not original. It is the one made by Curtsinger & Berger in *STABILIZER: Statistically
Sound Performance Evaluation* (ASPLOS 2013): randomise layout repeatedly, and ordinary statistics
become valid again. The failure mode it fixes is the one catalogued by Mytkowicz, Diwan, Hauswirth &
Sweeney in *Producing Wrong Data Without Doing Anything Obviously Wrong!* (ASPLOS 2009) — where even
changing the size of an environment variable shifts the stack and moves results by tens of percent.

## 3. The instrument: `benchmarks/bench_layout.py`

    # calibrate this machine (mandatory, once per machine, before judging anything)
    python3 benchmarks/bench_layout.py --base . --null --save-floors floors.json

    # judge a change
    python3 benchmarks/bench_layout.py --base <tree-A> --cand <tree-B> --floors floors.json

It compiles the **same source** into K different layouts, runs them, and compares *distributions*.
Four design choices, each of which the calibration forced rather than suggested:

**Layout draws.** Each draw varies `-falign-functions` and a count of dead `used` functions that shift
everything emitted after them (`BENCH_LAYOUT_PAD` in `bench_engines.cpp`). Both are semantics-free:
neither can change what the program computes. The draw list is fixed, not seeded — a reproducible
sweep can be re-run against a later tree and compared draw for draw.

**Pairing.** Draw *i* is built with the same perturbation on both sides, so the comparison is paired.
This is what gives usable power at small K.

**Interleaving, which is not a nicety.** The first version ran side A's whole sweep and then side B's.
Its null calibration came back with a *systematic positive bias* — most rows' medians at +1 to +4 % on
a comparison whose true delta is exactly zero — because the second sweep ran on a warmer machine.
Alternating A, B, B, A within each draw and taking the minimum of each side's two readings collapsed
that bias from **+3.9 % to +0.6 %** at the worst row.

This is a rule for **every paired timing instrument in this repository, not just this one** — and the
repository had a second one that broke it. `benchmarks/matrix4d` (the `matrix-gate` step) times a route
against the same pattern with that route forced off, and it measured the route side to completion and
then the core side. Its verdict is a *ratio*, so a burst during one side moves the ratio with the code
unmoved: `date dense`, at a true delta of zero, read 1.095 against a 1.05 tolerance and stopped a gate
on a regression that did not exist, while six clean readings of the same two trees spanned 1.007–1.017.
Its slots now alternate `route,core` / `core,route` across reps — six sweeps, the same count as before —
and the worst cell's ratio went from a 4.5-point spread (worst 1.0455, half a point from red) to 0.7
(worst 1.0217). The demonstration to keep: a run where **both** sides inflated 11 % together held the
ratio at 1.018. The 5 % tolerance was deliberately **not** loosened — that would have weakened the gate
instead of the noise.

**Null calibration, which is the part that makes any of it honest.** `--null` runs the tree against
*itself*. The true delta is exactly zero by construction, so whatever comes out is the instrument
talking, measured **per row** — rows do not share a floor. §4 gives the measured ones and, just as
importantly, how much to trust them.

### 3.1 The cheaper instrument, and it must run FIRST: compare the machine code

A layout campaign costs half an hour and answers "is this row faster?". A far cheaper instrument answers
a different question in seconds — **"did the code that row executes even change?"** — and asking it first
retires most hypotheses before a campaign is spent on them. Compile the consumer unit before and after,
disassemble both, strip addresses and symbol offsets, and diff **function bodies keyed by mangled name**
so an inserted function does not make everything downstream look different:

```sh
objdump -d --no-show-raw-insn before.o   # then: normalise 0x… and <sym+0x…>, split on `<name>:`,
objdump -d --no-show-raw-insn after.o    # compare bodies for names present in both
```

Three readings, and each means something different:

- **No body differs** → the change cannot affect runtime at all. This is a *proof*, not a measurement, and
  it is stronger than any campaign: `real.hpp`'s route-billing ticks and the benchmark's three added rows
  were both established this way, on both gate compilers.
- **Only the bodies you edited differ** → nothing else can move, so a campaign has one hypothesis to test
  instead of eighteen.
- **Bodies you did not touch differ at IDENTICAL instruction counts** → a struct reflow changing field
  offsets, not new work. Treat it as a separate change with its own cost.

**In one session this retired five hypotheses that were each about to cost a campaign**, three of them the
author's own: that a diffuse "per-unit inline budget" explained a cost (no — five bodies changed, none of
them a scan loop); that a `bool` added to the hot iterator cost layout (no — it landed in padding,
`sizeof` unchanged at 8664); that outlining the constructor would make a filler free (no — the toll is
`refill_batch`'s and nothing shields it); that a memory cliff was unbounded (no — the lazy-DFA cache
flushes at `state_budget`); and that a `switch` on a dense enum would give a jump table (no — clang emits
a branch tree).

Two rules came out of it that are worth more than the measurements they came from:

- **A test seam in a gate that is crossed per MATCH costs about as much as the route it guards.**
  `fixed_shape_route_disabled()` in `run()`'s gate added four instructions and cost `date` **+8.4 %**
  [+2.9, +23.9] at 24/24. The seven other route seams sit in gates too and cost nothing measurable — their
  routes are BATCHED, so the check amortises over `batch_cap` matches. Consult such a seam once per regex
  (see `dynamic_storage::compile`) or once per walk, never per match.
- **Removing bytes from a hot struct costs more than the dead code it accompanies.** Deleting two retired
  `pattern_hints` fields reflowed twelve function bodies at identical instruction counts and left `date`
  +5.5 % and `literal` +6.1 % — medians above their own floors at 23 and 22 of 24 draws. Holding two
  reserved bytes at the same offset collapsed the diff to the bodies actually edited and turned the same
  stack into a **REAL −5.2 % on `words [a-z]{4,}`**. Dead code and dead layout are separate decisions.

### 3.2 A sign test across rows sees what the per-row rule cannot — and it closed three targets

§4's rule judges each row on its own, which is right for a claim about that row and blind to a small effect
spread over all of them. The batched walk's dispatch is where that mattered.

`basic_match_iterator::refill_batch` is entered once per `batch_cap` matches by EVERY batched route, and it
is reached from `count_matches`, which is what every throughput measurement runs. Adding ONE branch to it --
calling a filler that already exists, so no new function body; the flag computed in the already-outlined
cold `decide_batching`, so the constructor is unmoved; the struct reflow held out -- leaves `advance` and all
six span fillers byte-identical, and moves only `refill_batch` (+10 instructions) and `count_matches` (−2).

On the 18-row consumer instrument that reads **0 rows REAL**. Every spread straddles zero, so by §4 nothing
is proven. But **17 of the 18 medians are positive**, +0.2 % to +9.7 %, at 21 of 24 draws on most of them --
where the same base without the branch had read 13 of 18 NEGATIVE. Under the null, 17-or-more of 18 sharing
a sign has probability about 1e-4. The per-row rule cannot see a ~4 % effect against these floors; the sign
across rows can, and the two independent perturbations that produced it (a real filler, and this deliberately
minimal probe) agree.

**So the batched dispatch is closed to new routes, and this is the conclusion to keep rather than re-derive.**
Three routes still bill one route entry per match -- `exact_literal` (1.0005), `inner_literal` (1.0003),
`fixed_shape` (1.0003, and it serves `date`, §A's weakest row at 0.83x / 0.79x) -- and batching any of them
through `refill_batch` taxes the other seventeen rows by about the same amount it might win on one. Nor is
there a free door around it: a branch in `advance` is worse (it is inlined into `count_matches`), a
function-pointer dispatch has to convert `refill_batch` first and pays the toll once, and a separate walk in
the `TrailingLA` style adds its selector to `count_matches`. Every entrance touches a hot shared function.

What this does NOT say is that those routes are unimprovable -- only that *batching them through the shared
dispatch* is not the way. And the cost of finding out is now seconds: if §3.1's comparison shows
`count_matches` and `refill_batch` byte-identical, a mechanism is viable; if it does not, it is not, and no
campaign is needed to know.

### 3.3 A second compiler is a second instrument, and the per-call rows needed a batched clock

`make bench-compilers` builds `bench_minimal.cpp` with every compiler on the PATH, runs each, and
prints the per-row ratio against the first. It exists because this project measured itself with one
compiler for its whole life: local development and `bench_layout.py` both use the platform clang, and
the CI matrix's GCC leg builds and tests but never times anything. Whatever GCC does differently was
therefore invisible by construction, and what it does differently is not small:

| row | clang++ 16 | g++-14 | ratio |
| --- | ---: | ---: | ---: |
| `short stamp search exact` | 54.9 ns | 103.5 ns | **1.89x** |
| `short stamp match reject` | 34.6 ns | 83.0 ns | **2.40x** |
| `short trim replace` | 681.3 ns | 765.6 ns | 1.12x |
| the nineteen throughput rows | -- | -- | 0.90x to 1.22x |

The per-call rows carry the whole difference; the throughput rows are within ordinary compiler
variance. The cause is a per-call fixed cost that one compiler removes and the other does not, which is
why it hides in throughput rows: over 100 KB it is divided by the corpus.

**The rule this establishes.** When an optimisation's own comment claims a saving, that claim has to
hold on both legs, or say which leg it holds on. A number measured under one compiler is a statement
about that compiler.

**And the rule that made the leg possible at all.** A per-call row measures tens of nanoseconds. Read
the clock around a single such call and the reading is dominated by the clock: the five per-call rows
came back at **exactly 0.0 ns** under g++-14 and 42 ns under clang, from nothing but where each landed
relative to the tick — and a minimum-of-samples estimator then reports the zero. So the timed region is
a batch, sized per case until it spans 50 us, and the total is divided. Two consequences worth stating
because both are easy to get wrong:

* A row reading 0.0 ns is never a fast row. It is an unmeasured row, and the harness must be fixed
  before any comparison drawn from it is used for anything.
* Batching moves the absolute numbers of the affected rows (42 ns became 55 ns here) for two
  independent reasons -- the granularity artefact is gone, and an inner loop changes the enclosing
  function's inlining context. Per-call numbers taken before the batch are not comparable to numbers
  taken after it, and neither is a floor calibrated under the old loop.

**What the batch bought, beyond measurability.** The calibrated floors say the per-call rows are now
the *most* sensitive rows in the minimal unit, not the least: `short stamp match hit` and `short stamp
match reject` both floor at **0.1 %**, `short stamp search exact` at 0.6 %, against 2.7-3.1 % for the
throughput rows over 100 KB corpora. Each sample averages thousands of calls, so layout noise is
averaged inside the sample rather than between samples. A one-percent movement in the per-call regime is
therefore a judgeable claim here -- which is what makes the fixed per-call cost a tractable target at
all, rather than something only a profiler can see.

### 3.4 On Linux, ramp the clock and pin the core, or measure the governor instead

Discovered while trying to compare two ISAs and finding the comparison had no ground to stand on. A host
under the `powersave` governor idles at its minimum multiplier -- 1.2 GHz on a part whose base is 3.7 --
and ramps only under sustained load. A per-call row is a few hundred microseconds of work, so it can
complete entirely below the ramp, and which side of the ramp it lands on is chance. Five runs of a
SINGLE unchanged binary on the same row:

    163.9   230.2   300.1   301.8   315.5 ns        (and 83.2 seen earlier the same hour)

3.8x of spread, none of it attributable to any code. Every x86-64 per-call number taken before this was
found had to be discarded, including one already written into a commit message.

**It takes two things, and neither is optional.** The ramp is per core, so ramping without pinning lets
the process migrate onto a core that has not ramped:

    ramp only, unpinned    82.7   83.3   83.9   231.2          <- one run in four is worthless
    ramp + pinned          77.1   83.5   83.6   83.8   85.1   85.2

`benchmarks/bench_warmup.hpp` does both, called once from `main` before any row is timed: pin to the
core we are already on (so an outer `taskset` stays in charge), then spin 800 ms. `bench_minimal` and
`bench_percall` both call it.

**What it does not fix.** A longer timed region substitutes for none of this -- growing the batch from
50 us to 5 ms changed nothing once ramped and pinned, because the batch exists to clear the clock's
granularity (§3.3), which is a different problem. And even ramped and pinned, the spread is 1.1x rather
than 1.0x, so on such a host **take the minimum across runs**, never a single run's median. The
arm64 development machine needs none of this and shows none of the instability; that asymmetry is
exactly why it went unnoticed.

## 4. The decision rule, and the measured floors

A row's movement is reported as **REAL** only when both hold:

1. the median paired delta exceeds that row's null floor, **and**
2. every draw agrees in sign — the spread does not straddle zero.

Anything else is **indistinguishable**, which is not the same statement as "no change" and is not
written as one.

Condition 2 entered as a patch and stays on its own merits. It entered because the first floor
estimator (a maximum over 8 readings) handed `hex [0-9a-f]{8}` an implausibly tight ±0.1 %, and the
first judged comparison duly flagged that row REAL on a +0.2 % median whose spread was [−0.8, +1.8].
The converging estimator removes that particular failure — `hex` now calibrates at 1.1 % — but the
condition is kept, because a spread straddling zero is not evidence of a shift whatever its median
does, and because it is checked rather than assumed: on the 24-reading null, **0 of 20 rows produced a
single-signed spread**. It therefore costs no sensitivity on a true-zero change.

### The floor's estimator, and a design fault this page shipped with

The floor is the **95th percentile of |paired delta|** over the sweep. It was a *maximum* for one
afternoon, and that was wrong in a way worth recording: the widest excursion **grows with the sample
size**, so a max-based floor is not a consistent estimator and cannot be stabilised by collecting more
data. Measured on the same configuration: `digits [0-9]+` calibrated at **1.0 % from 8 readings and
6.0 % from 24**. This page simultaneously advised raising `--reps` "until it settles" — advice that
could never be satisfied. A high quantile converges; that is the whole requirement for a threshold.

**A claim this page made and now withdraws:** it said the floors "span a factor of seventy". That was
an artefact of the max estimator on 8 readings. With a converging estimator on 24, the span is a
factor of **seven** — and the row it named as worst, `\p{scx=Cyrl}` at 7.3 %, is in fact the **best**
at 0.6 %. The lesson generalises: the instrument's own statistics need the same scepticism as the
engine's.

Floors on **arm64** (Apple M1 Pro, Apple clang 16), 8 layout draws × 3 interleaved reps = 24 paired
readings:

| row | floor (q95) | max | half 1 | half 2 |
| --- | ---: | ---: | ---: | ---: |
| `literal` | **4.5 %** | 5.8 % | 4.1 % | 2.5 % |
| `[à-ÿ]+` (accented) | **3.1 %** | 3.7 % | 3.4 % | 3.0 % |
| `digits [0-9]+` | **3.1 %** | 6.0 % | 4.4 % | 1.5 % |
| literal `你好` (CJK) | **2.8 %** | 4.8 % | 2.7 % | 1.5 % |
| `fields [^,]+` | **2.7 %** | 5.9 % | 4.2 % | 1.6 % |
| `\w+` (mixed-script) | **2.7 %** | 5.9 % | 4.2 % | 1.6 % |
| `date {4}-{2}-{2}` | **2.6 %** | 4.4 % | 3.6 % | 0.8 % |
| `words [a-z]+` | **2.5 %** | 3.8 % | 3.1 % | 0.5 % |
| `\p{N}+` (arabic digits) | **2.4 %** | 3.6 % | 2.6 % | 1.3 % |
| `single [a-z]` | **2.2 %** | 2.8 % | 2.5 % | 1.6 % |
| `anchored ^[a-z]+$` | **2.1 %** | 2.6 % | 1.2 % | 1.5 % |
| `lookahead [a-z]+(?=[a-z])` | **1.9 %** | 2.7 % | 2.3 % | 0.7 % |
| `alt the\|fox\|dog` | **1.8 %** | 2.4 % | 2.1 % | 0.5 % |
| `.` (emoji) | **1.8 %** | 1.9 % | 1.6 % | 1.7 % |
| `\p{L}+` (CJK) | **1.5 %** | 4.3 % | 2.5 % | 1.4 % |
| `hex [0-9a-f]{8}` | **1.1 %** | 2.9 % | 1.9 % | 0.8 % |
| `\p{sc=Han}` (CJK) | **0.9 %** | 1.7 % | 1.3 % | 0.4 % |
| `(?i)café` (accented) | **0.7 %** | 2.0 % | 1.3 % | 0.3 % |
| ascii witness `[a-z]+` | **0.7 %** | 1.5 % | 1.0 % | 0.4 % |
| `\p{scx=Cyrl}` (mixed-script) | **0.6 %** | 0.9 % | 0.7 % | 0.4 % |

**Read the two half-sweep columns before trusting any single floor.** They are the same statistic on
each half of the same data, and they disagree by up to **6.2×** (`words`: 3.1 % against 0.5 %). So:

* a floor here is good enough to be a **conservative threshold** — its job is to stop a claim, and it
  is on the safe side for that;
* it is **not** precise enough to compare configurations. Three attempts to do so — arm64 against
  x86-64, four engines against REAL-only — each produced floors moving in *both* directions, which is
  what an unstable estimate looks like, not a property of the build. `bench_layout.py` now prints this
  split-half ratio and says so.

Floors on **x86-64** (devbox container, g++ 13.3.0), same 24 paired readings:

| row | floor (q95) | arm64 for comparison | half 1 | half 2 |
| --- | ---: | ---: | ---: | ---: |
| literal `你好` (CJK) | **4.2 %** | 2.8 % | 2.2 % | 5.4 % |
| `literal` | **3.0 %** | 4.5 % | 3.5 % | 2.1 % |
| `.` (emoji) | **2.2 %** | 1.8 % | 1.2 % | 2.5 % |
| `fields [^,]+` | **2.1 %** | 2.7 % | 1.4 % | 2.2 % |
| `words [a-z]+` | **2.0 %** | 2.5 % | 2.5 % | 1.8 % |
| `lookahead [a-z]+(?=[a-z])` | **1.9 %** | 1.9 % | 1.0 % | 2.8 % |
| `[à-ÿ]+` (accented) | **1.7 %** | 3.1 % | 1.8 % | 1.3 % |
| `single [a-z]` | **1.6 %** | 2.2 % | 1.7 % | 1.3 % |
| `alt the\|fox\|dog` | **1.5 %** | 1.8 % | 1.7 % | 1.2 % |
| `\w+` (mixed-script) | **1.4 %** | 2.7 % | 0.6 % | 1.8 % |
| `\p{scx=Cyrl}` (mixed-script) | **1.3 %** | 0.6 % | 1.0 % | 1.9 % |
| `\p{sc=Han}` (CJK) | **0.8 %** | 0.9 % | 0.5 % | 1.3 % |
| `date {4}-{2}-{2}` | **0.7 %** | 2.6 % | 0.7 % | 0.7 % |
| ascii witness `[a-z]+` | **0.7 %** | 0.7 % | 0.7 % | 0.6 % |
| `\p{N}+` (arabic digits) | **0.7 %** | 2.4 % | 1.5 % | 0.5 % |
| `(?i)café` (accented) | **0.7 %** | 0.7 % | 1.3 % | 0.6 % |
| `\p{L}+` (CJK) | **0.6 %** | 1.5 % | 0.3 % | 0.7 % |
| `digits [0-9]+` | **0.5 %** | 3.1 % | 1.4 % | 0.4 % |
| `hex [0-9a-f]{8}` | **0.4 %** | 1.1 % | 0.4 % | 0.3 % |
| `anchored ^[a-z]+$` | **0.2 %** | 2.1 % | 0.2 % | 0.4 % |

**A prediction of this project's, falsified, and the correction matters more than the prediction.**
The expectation was that x86-64 floors would be systematically *wider* — that being the leg where the
impossible deltas showed up, in a container, under gcc's punishing per-unit inline budget. The
opposite holds: x86-64 is **tighter on 12 of 20 rows**, several by a factor of 3 to 6 (`digits` 0.5 %
against 3.1 %, `anchored` 0.2 % against 2.1 %, `date` 0.7 % against 2.6 %), and its split-half
stability is better as well (worst ratio **3.3× against arm64's 6.2×**).

So **the devbox is the more sensitive instrument and the laptop is the noisy leg** — which inverts this
project's habit of treating the local arm64 machine as primary and the devbox as the awkward
confirmation. It is also the expected direction on reflection: a dedicated container against a laptop
with thermal management, background load and performance/efficiency core migration.

Two consequences for practice. **Judge on the devbox** when the two disagree about whether something
cleared its floor. And **do not read the historical "x86-64 regressions that never reproduced" as x86
noise** — that leg is the quiet one, so whatever those readings were, this is not the explanation.

## 5. What this retires

### 5.1 Two decision rules, both falsified by the null

**"Rows that move the same way on both ISAs are real work; rows that disagree are placement."** This
was the discriminator this project used, and it is recorded in `design.dox` §10.1. It has **no power**.
It was falsified twice over: a change that deleted only compile-time-only code — a helper reached once
per `regex` construction, never on any scan path — moved `digits [0-9]+` by **+16.7 % on x86-64 and
+3.0 % on arm64**, i.e. it *agreed in direction on both ISAs* while being incapable of costing
anything. Direction agreement measures how systematic an artefact is, not whether it is one.

**"Agreement on K−1 of K layout draws."** This tool's own first decision rule, retired within an hour
of existing: on the null, `(?i)café` agreed **8 draws out of 8** at +3.4 %.

### 5.2 Claims that do not survive, and claims that do

The instrument does not invalidate everything measured before it. It invalidates claims **at or below
the floor** of the row they were measured on. Applying it:

| earlier claim | status |
| --- | --- |
| The cross-ISA direction discriminator (`design.dox` §10.1) | **retired** — see §5.1 |
| "one added line cost +27.7 % on `words`" | **unsupported as stated** — single build, no distribution; `words`'s floor is 3.0 %, so a 27.7 % reading is well clear of noise and something real probably happened, but the *magnitude* is one draw and is not a measurement of the change |
| "removing 36.8 KB of code changed nothing" | **unsupported** — a null result from a single draw is exactly what the null sweep produces by accident |
| v2026.8.10's "six rows regress 3–15 % on arm64" | **mostly unsupported** — of those rows, `\p{L}+` (floor 4.5 %), `words` (3.0 %) and `digits` (1.0 %) were quoted at 3–15 %; only the largest survive contact with their floors, and none was measured as a distribution |
| v2026.8.11's "`alternation` costs +5.1 % x86 / +1.0 % arm64" | **withdrawn** — `alt`'s arm64 floor is 1.1 %, so the arm64 figure was noise, and the pair was the whole basis for calling it a price |
| the batching train's "+16.8 % → +10.2 % when `batch_cap` went 16 → 4" | **plausible, not established** — the deltas are far above the relevant floors, but they are single draws and the *difference between them* (6.6 points) is not |
| this page's own "the floors span a factor of seventy" | **withdrawn by this page** — an artefact of a max-based estimator on 8 readings; see §4 |
| this page's own "x86-64 floors are probably wider" | **falsified by this page** — x86-64 is tighter on 12 of 20 rows; see §4 |
| "`noinline`/`cold` placement matters" | **stands, and independently** — removing those attributes moved four Unicode rows and restoring them returned all four to ≈0, a mechanism already documented at `verify_class_row`, and the effect is reproducible rather than a single reading |
| "fewer instructions is not faster" (−21 % Ir, +20 % time) | **stands** — instruction counts are deterministic, so that half needs no distribution, and a +20 % time change is above every floor in the table |
| the `exhaustive-compat` / correctness results | **untouched** — this document is about timing only; correctness is decided by differentials and oracles, which layout cannot perturb |

### 5.3 The refactor this tool was built to judge

A three-part simplification (unifying two byte-identical `\b`-peel helpers, factoring two row-fill
twins, and naming the batch-eligibility preamble written out four times) was **rejected on
single-build evidence** — up to +17.8 % on x86-64 — and then **exonerated by this instrument**: every
row's median within ±0.9 %, every row below its floor. It was the measurement that was wrong, not the
change.

### 5.4 What the instrument is actually for: three spellings of one guard

The clearest case so far, because none of these calls could have been made by reading the code. Teaching
the batch fillers a one-position word-boundary guard (so `\b\w+\b` stops paying 2.8× its own bare
form) was written three ways. The *feature* was identical each time; only its spelling differed:

| spelling | target row | rows judged REAL against their floors |
| --- | ---: | --- |
| condition inline, evaluated per iteration | −25.6 % | **five regressions**: `digits` +17.3 %, `words` +13.6 %, witness +13.6 %, `\p{L}+` +13.4 % |
| hoisted to a runtime local, false after the first span | −26.1 % | **five regressions**: `words` +7.7 %, witness +7.4 %, `\p{L}+` +7.3 %, `\w+` +6.8 %, `digits` +6.2 % |
| `if constexpr` on a template parameter | **−28.6 %** | **none** |

Three things this shows that a single-build A/B could not have.

**The regressions were real, not noise.** Sixteen of sixteen draws agreed in sign, and the medians sat
three to twenty times above the affected rows' floors. The old method would have produced *a* number
for each spelling with no way to tell which of them meant anything.

**The mechanism was findable from the measurement.** The inline spelling re-read the hint struct on
every span emitted, in loops that emit a span every few bytes. That is why hoisting halved the cost —
and why halving was not enough.

**"Indistinguishable" is the useful verdict, not a shrug.** The accepted spelling's other twenty rows
came back indistinguishable, several with negative medians. That is precisely the claim the change
needed, and it is a claim, not an absence of one.

### 5.5 TWO instruments, because the harness is not shaped like a consumer

The single most consequential finding of the day, and it invalidated three of this project's own
refusals within an hour of being measured.

Every layout verdict above was taken against `benchmarks/bench_engines.cpp`, which includes `<regex>`,
PCRE2 and RE2 alongside `real.hpp`. Three changes were refused during one session because they charged
the code-point class rows 5 to 9 % **there**: a counted-minimum check for `X{k,}` (twice, once
templated), and a class-index parameter so the bare possessive loop could reuse a filler. Each refusal
was correctly reasoned *from that instrument*. The instrument was the problem.

A consumer includes only `real.hpp`. `benchmarks/bench_minimal.cpp` is that translation unit — nothing
else in it — and `bench_layout.py --source bench_minimal.cpp` judges against it. Re-judging the
counted-minimum change there, 8 draws × 3 reps = 24 paired readings on x86-64:

| row | four-engine harness | minimal unit |
| --- | ---: | ---: |
| `\w{2,}` (the target) | not measured — no row existed | **−38.1 %**, 24/24, REAL |
| `\p{L}{3,}` (the target) | not measured | **−36.2 %**, 24/24, REAL |
| `\p{L}+` (the row that caused the refusal) | **+4.9 %, then +7.6 %** | **−0.3 %, indistinguishable** |

The cost that drove the decision does not exist in a unit shaped like the one users compile.

**Note what this does NOT say.** The harness is not "wrong": it is a four-engine comparison binary, and
the absolute numbers and competitor ratios in `BENCHMARKS.md` are what they are precisely because that
is the binary. Nor is the minimal unit a truer instrument in general — its own split-half stability is
worse (13.9× on `\w+` against the harness's 3.3×), because with fewer rows and less code there is less
to average over.

So: **two instruments, each authoritative for its own question.**

* *Does this change help the people who use the library?* → `bench_minimal.cpp`. This governs whether a
  change lands.
* *What number does this project publish?* → `bench_engines.cpp`. This governs `BENCHMARKS.md`.

When they disagree, both get reported. A change that gains in the minimal unit and costs a published
row is still worth landing — and the published row's movement must then be labelled as what it is, a
property of the comparison binary, not of the engine.

**A caution against over-correcting.** One measured disagreement does not license re-opening every
refusal in this repository. `docs/BENCHMARKS.md` and `design.dox` record refutations taken with the
single-build method, which §5.1 and §5.2 already downgrade on their own grounds; this section adds a
second reason to re-check the ones that turned on small costs in the harness, not a licence to assume
they were all artefacts. Re-judge them, one at a time, with both instruments.

## 6. What is still missing

Named so the gaps are visible rather than implied:

- **A minimal-unit floor set per machine.** §5.5's instrument needs its own `--null` run wherever it is
  used, and its split-half stability (13.9×) is worse than the harness's -- more reps, or more rows,
  before leaning on a single floor from it.
- ~~**Front-end performance counters.**~~ **Wanted, and verified UNAVAILABLE here — do not plan around
  it.** `perf stat` on `instructions` plus front-end stall and iTLB counters would tell placement from real
  work directly rather than by distribution, and it is x86-only in practice (macOS does not expose them
  through `perf`). The x86 devbox is an LXC container whose `perf` binary does not match the Proxmox
  kernel, so every counter read fails asking for `linux-tools-<kernel>`; installing that is a change to
  the hypervisor host, not to this project. What DOES work there, and is the substitute actually reachable
  today: `valgrind` (so callgrind/cachegrind, deterministic and load-independent) and §3.1's machine-code
  comparison, which answers the placement-versus-work question structurally rather than statistically.
- ~~**One binary per engine.**~~ **Measured and closed.** The suspicion was that linking four engines
  into one executable widens REAL's floor. Two findings retire it: dropping PCRE2 and RE2 removes only
  **4.3 %** of the binary (`std::regex` is header-only and compiled either way; the other two are
  dynamically linked), and a `--null --real-only` sweep moved floors in **both directions** rather than
  tightening them — `\p{scx=Cyrl}` 7.3 → 2.0 % but `date` 0.5 → 3.3 %, on the unstable max estimator
  that §4 has since replaced. There is no evidence of a systematic gain, and the change would break
  comparability with every published stamp. Not worth doing.
- **A floor estimator with a known standard error.** The q95 quantile converges where the old maximum
  did not, but §4's split-half columns still disagree by up to 6.2× on arm64. Bootstrapping the floor
  from the saved per-draw deltas (`--save-deltas` writes them) would give each threshold an interval
  instead of a point, which is what "is this above the floor?" actually needs.
- **Coz** (Curtsinger & Berger, SOSP 2015) for *where to optimise*: causal profiling measures the
  speed-up a line would buy, differentially inside one binary, so layout cancels.
- **PGO, then BOLT/Propeller, on the bindings**, where the layout is ours to ship (§1).
- **The floors are arm64's.** An x86-64 calibration must be run on the devbox before any x86 verdict.
