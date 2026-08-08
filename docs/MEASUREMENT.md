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

**Null calibration, which is the part that makes any of it honest.** `--null` runs the tree against
*itself*. The true delta is exactly zero by construction, so whatever comes out is the instrument
talking, measured **per row** — rows do not share a floor, and it turns out they differ by a factor of
seventy.

## 4. The decision rule, and the measured floors

A row's movement is reported as **REAL** only when both hold:

1. the median paired delta exceeds that row's null floor, **and**
2. every draw agrees in sign — the spread does not straddle zero.

Anything else is **indistinguishable**, which is not the same statement as "no change" and is not
written as one.

Condition 2 is not taste. Condition 1 alone is insufficient at this K because the null hands some rows
an implausibly *tight* floor by luck: `hex [0-9a-f]{8}` calibrated at ±0.1 %, and the first judged
comparison duly flagged it REAL on a +0.2 % median whose spread was [−0.8, +1.8]. On the null, **not
one of the twenty rows produced a single-signed spread** — so requiring one costs nothing in false
negatives and removes that failure.

Floors measured on **arm64 (Apple M1 Pro, Apple clang 16), 8 draws, interleaved**:

| row | null median | null spread | floor |
| --- | ---: | ---: | ---: |
| `hex [0-9a-f]{8}` | +0.0 % | [−0.1, +0.1] | **0.1 %** |
| `\p{N}+` (arabic digits) | −0.0 % | [−0.1, +0.5] | **0.5 %** |
| `\p{sc=Han}` (CJK) | +0.1 % | [−0.4, +0.5] | **0.5 %** |
| `date {4}-{2}-{2}` | −0.0 % | [−0.4, +0.5] | **0.5 %** |
| `lookahead [a-z]+(?=[a-z])` | +0.1 % | [−0.8, +0.5] | **0.8 %** |
| `digits [0-9]+` | −0.0 % | [−0.6, +1.0] | **1.0 %** |
| `alt the\|fox\|dog` | −0.0 % | [−0.7, +1.1] | **1.1 %** |
| `anchored ^[a-z]+$` | +0.0 % | [−1.4, +0.1] | **1.4 %** |
| ascii witness `[a-z]+` | −0.0 % | [−1.7, +0.1] | **1.7 %** |
| `[à-ÿ]+` (accented) | +0.0 % | [−2.0, +1.8] | **2.0 %** |
| `\w+` (mixed-script) | +0.5 % | [−2.4, +1.3] | **2.4 %** |
| `.` (emoji) | −0.3 % | [−2.5, +2.1] | **2.5 %** |
| literal `你好` (CJK) | +0.0 % | [−2.5, +0.5] | **2.5 %** |
| `words [a-z]+` | +0.0 % | [−0.4, +3.0] | **3.0 %** |
| `fields [^,]+` | +0.3 % | [−4.1, +1.2] | **4.1 %** |
| `\p{L}+` (CJK) | +0.6 % | [−1.5, +4.5] | **4.5 %** |
| `literal` | −0.0 % | [−4.7, +3.9] | **4.7 %** |
| `(?i)café` (accented) | +0.1 % | [−0.4, +5.7] | **5.7 %** |
| `single [a-z]` | −0.4 % | [−5.8, +2.2] | **5.8 %** |
| `\p{scx=Cyrl}` (mixed-script) | +0.1 % | [−0.6, +7.3] | **7.3 %** |

**Read that table before reading any delta in `BENCHMARKS.md`.** A 4 % move on `hex` is forty times
its floor and worth discussing. The same 4 % on `\p{scx=Cyrl}` is *below* its floor and means nothing
whatsoever. The rows this harness can actually resolve finely are the ones with tight floors; the
Unicode property rows and `single [a-z]` are, on this instrument, coarse.

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
| "`noinline`/`cold` placement matters" | **stands, and independently** — removing those attributes moved four Unicode rows and restoring them returned all four to ≈0, a mechanism already documented at `verify_class_row`, and the effect is reproducible rather than a single reading |
| "fewer instructions is not faster" (−21 % Ir, +20 % time) | **stands** — instruction counts are deterministic, so that half needs no distribution, and a +20 % time change is above every floor in the table |
| the `exhaustive-compat` / correctness results | **untouched** — this document is about timing only; correctness is decided by differentials and oracles, which layout cannot perturb |

### 5.3 The refactor this tool was built to judge

A three-part simplification (unifying two byte-identical `\b`-peel helpers, factoring two row-fill
twins, and naming the batch-eligibility preamble written out four times) was **rejected on
single-build evidence** — up to +17.8 % on x86-64 — and then **exonerated by this instrument**: every
row's median within ±0.9 %, every row below its floor. It was the measurement that was wrong, not the
change.

## 6. What is still missing

Named so the gaps are visible rather than implied:

- **Front-end performance counters.** `perf stat` on `instructions` plus front-end stall and iTLB
  counters would tell placement from real work *directly* rather than by distribution. x86-only in
  practice (macOS does not expose them through `perf`). This is the next thing to build.
- **One binary per engine.** The harness links REAL, `std::regex`, PCRE2 and RE2 into a single
  executable, so REAL's layout depends on how much competitor code sits beside it. `BENCHMARKS.md`
  already suspected this for the `fields` row. Splitting removes a whole confound class cheaply.
- **More null reps.** Eight draws give some rows a floor estimated from too little data (§4's `hex`).
- **Coz** (Curtsinger & Berger, SOSP 2015) for *where to optimise*: causal profiling measures the
  speed-up a line would buy, differentially inside one binary, so layout cancels.
- **PGO, then BOLT/Propeller, on the bindings**, where the layout is ours to ship (§1).
- **The floors are arm64's.** An x86-64 calibration must be run on the devbox before any x86 verdict.
