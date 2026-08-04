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
| Version | REAL `2026.8.5+` — **Both §A and §Unicode are re-stamped on both ISAs, and §Unicode was nine trains stale.** Two findings carry this stamp. **The prefilter ranked every UTF-8 byte as the rarest one there is**, so `café` picked as its `memchr` target the 0xC3 that leads every accented Latin letter — one byte in six of French prose, chosen in the belief it was rare. Against an ASCII literal of the same match density in the same corpus that was **6.3× slower** (1.761 vs 0.279 ns/B); ranked by selectivity, **0.258**, at parity. **And the harness cannot hold a row for it**: adding one brace-initialized case to `bench_engines.cpp` — no new code — moved gcc/x86-64 `words` **+27.7 %**, its ASCII witness +26.9 %, `digits` +23.7 %, with arm64/clang flat, so the row was reverted and the figure is published from a labelled isolated probe instead. That file already said 'do not instrument me' on the strength of a 12 % runtime switch; it now also says a *data row* spends the same budget. **`(?i)café`, this document's worst ratio, is decomposed rather than fixed**: pure literal 0.257, explicit class `caf[éÉ]` 0.700, `(?i)café` 1.226 — the dominant step is emitting the fold as a code-point class, and it is NOT the case-insensitivity, since plain `café` used to be the slower of the two. §A itself moves by under 1 % on either ISA this train. Under the previous stamp, `2026.8.5+` — **§A is re-stamped on both ISAs because one change moved its flagship rows on gcc/x86-64 by 14 %.** The thread list's last heap container, its `mark` table, became SBO, and `basic_thread_list::reset` was outlined; the pairing is the point, since each measured ALONE is a regression (`small_vec` alone +10 % on x86-64 class-scan, `noinline` alone +5.8 % on `words` and nothing cold). **x86-64: `words` 1.72 → 1.46 ns/B and `digits` 1.21 → 1.03 — against PCRE2-JIT that is 3.76× → 4.40× and 3.00× → 3.52×; `alternation` 2.45 → 2.30 reaches 0.98×, near parity. arm64 is flat** (every row within ±1 %, `fields` 3.30 → 3.19 the one real move), which is the expected shape: the cost removed was gcc's inline budget on routes that never build a thread list. What the change was actually FOR does not appear in this table at all — **per-call latency on a short subject, −20 to −25 %** (`[a-z]+` 59.4 → 44.5 ns on a 16-byte subject, `hello` 65.6 → 50.8) — because §A measures steady-state ns/byte over a warmed regex, and the saving is one state construction per `search()`. **The saving is not the avoided allocation**, which is why the inline capacity is 8 and not 64: it appears on patterns that never allocate at all, being `std::vector`'s destructor leaving a state that every standalone search builds and tears down. **Unblocking it required `small_vec` to spill during CONSTANT EVALUATION**, which it could not: `reserve` threw `bad_alloc`, so a constexpr `real::regex` outgrowing the inline capacity broke the BUILD — a ceiling nothing announced, now lifted for every user of that container through `std::allocator`. **Two measured sets were discarded rather than published** (see §A's protocol): a contaminated arm64 set, and an x86-64 set that linked the container's system PCRE2 10.42 instead of the pinned 10.47, which alone would have published `anchored`'s 1.01× as 1.50× in REAL's favour. Under the previous stamp, `2026.8.4+` — **The anchoring work is complete on both class routes, and both tables are re-measured on it.** `\A`/`^` is a MODE and `\Z`/`$` a LIMIT; the recognizers peel them and the routes honour them, so a pattern pinned to a position no longer falls to the general VM: **`^[a-z]+` 2.585 → 0.032 ns/B (81×), `[a-z]+$` 3.100 → 0.032 (94×), `^[a-z]+$` 0.573 → 0.043 (13×), `^\w+$` 1.450 → 0.066 (22×)** over 100 KB. The published `anchored ^[a-z]+$` row reads **REAL ahead of PCRE2-JIT on both ISAs** (1.35× arm64, 1.01× x86-64). **Collateral of the last step, measured:** the code-point half lands within +2.9 % on arm64 and mostly BETTER on x86-64 (ASCII witness −7.0 %), with `(?i)café` +5.9 % the one row that pays. **Three guards came from three different oracles**, none of them foreseen: the seam differential on `[a-z]+\b$`, the Python binding's fuzz against `re` on `\b(?>\w)$` (a wrong answer that 11 178 span comparisons had missed), and MSVC's `/WX` on a shadowed local that no local step asked about — `gcc-check` now passes `-Wshadow`, which is in neither `-Wall` nor `-Wextra`. A `\b`/`\B` wrap and an end anchor together are refused rather than combined: once the assertion is peeled, nothing downstream can re-derive it. Under the previous stamp, `2026.8.3` — **`\A`/`^` and `\Z`/`$` are a MODE and a LIMIT, not shapes, and this table now has an anchored row to say so.** An explicit anchor used to disqualify every shape route, so a pattern pinned to position 0 ran on the general VM while the identical question asked through the call ran on the class loop: **`^[a-z]+` 2.585 → 0.031 ns/B (81×), `[a-z]+$` 3.100 → 0.032 (94×), `^[a-z]+$` 0.573 → 0.032 (17×)** on 100 KB. The recognizers peel the anchors and the routes honour them; multiline `^`/`$` are different assertions and still disqualify. **The new `anchored ^[a-z]+$` row exists because none of the eight rows above could see any of that** — every one is free-floating, so a 94× change moved nothing here, and an unmeasured row is one that regresses in silence. It reads **REAL ahead of PCRE2-JIT on both ISAs** (1.32× arm64, 1.01× x86-64) and 3.5–6.9× ahead of RE2. **`std::regex` is `unsupported` there and that is not a capability gap but a CRASH**: libstdc++ recurses once per input character and SIGSEGVs on a 200 KB anchored subject — ~98 000 frames before the stack goes — while libc++ survives at 63.7 ns/B, so it is an implementation property, and the harness skips rather than dying with it. **Collateral, measured and reduced rather than hidden:** the first shape of this change cost §Unicode 6–17 % on arm64; outlining everything an anchor implies behind ONE branch took that to +1.1…+4.5 %, with the ASCII witness at +0.1 %. Under the previous stamp, `2026.8.3` — **The walk now hands out BUFFERED spans, and it is the largest single move these tables have recorded.** Holding a class and its bytes fixed while varying only how often a match must be emitted showed the inner scan at ~2.2 ns/B against 7.6 for the same bytes emitted one code point at a time: **71 % of those rows was the per-match return** through `run()`'s dispatch, `fill_span_slots` and the iterator's re-entry. The walk fills a four-span buffer inside the route instead. **`words` 2.225 → 1.163 ns/B arm64 and 2.877 → 1.708 x86-64; `digits` 1.381 → 0.845 and 1.758 → 1.089; `[à-ÿ]+` 3.390 → 1.572 and 8.183 → 3.336; `\p{sc=Han}` 5.556 → 3.326 and 9.246 → 5.974; `\p{L}+` 4.075 → 2.880 and 6.144 → 4.269; `\w+` 3.010 → 2.295 and 5.236 → 3.686.** Against PCRE2-JIT that takes `words` from 1.05× to **2.00×** on arm64 and 1.89× to **3.76×** on x86-64, `digits` to **1.70×** / **3.33×**, and `[à-ÿ]+` and `\p{scx=Cyrl}` cross ahead for the first time (**1.31×**, **1.01×**). **What it costs, unhidden:** `fields` +3.4 % arm64 / +7.4 % x86-64 (`[^,]+` averages 6.3 bytes per match, so there is nothing to amortise), `date` +5.6 % x86-64 and `.` +7 % — routes the batch does not serve. **A fourth filler for `.` was written, verified and refused**: −58.8 % on its own row on arm64 and near-nothing on x86-64, where it took back most of the byte filler's win (`words` 1.708 → 3.155). Two fillers is what this translation unit absorbs. Under the previous stamp, `2026.8.3` — **These tables still describe this tree: a lead-byte table for the UTF-8 decoder landed after them and was REVERTED, so the only engine difference against the measured revision is a comment.** It is worth the sentence because of how it failed. The table is exactly equivalent (differential over every 1- and 2-byte sequence on `cp`, `length` and `valid`, zero divergence) and an isolated probe read it as a clear win on BOTH platforms — `\p{sc=Han}` −13.3 % arm64, `\p{L}+` −12.6 %, `\p{N}+` −10.6 % x86-64. In this harness it costs §A 4–13 % on x86-64 and 3–6 % on arm64, on ASCII rows that never call the decoder; folded from 256 entries to 32 it recovers §A on x86-64 and breaks §Unicode on arm64 instead (`[à-ÿ]+` +15.8 %, ASCII witness +3.2 %). Attributed, not assumed: measuring the three trees in sequence puts the Han threshold at 0.0…−1.6 % per §A row and the decoder at +1.0…+13.5 %. **The lesson is about the instrument, not the decoder**: `utf8.hpp` is included everywhere, this translation unit sits at a codegen cliff (docs/design.dox §10.1), and a probe that isolates one pattern in one small binary reported gains on both platforms for a change that loses on both. Measure anything in that file with `make bench-engines`. Under the previous stamp, `2026.8.3` — **`\p{sc=Han}`, §Unicode's worst row, moves off a binary search and onto the sparse two-stage table: 10.713 → 9.147 ns/B on x86-64 (−14.6 %) and 5.757 → 5.621 on arm64.** `cp_hi_range_threshold` stood at 32 on the strength of a measured quasi-tie; re-measured, the two classes that straddle it pull opposite ways — `sc=Han` (22 ranges) wants the table, `scx=Cyrl` (18) wants the search and loses 2.7 % on the table — so the crossover lies between them and the constant is now that gap rather than either measurement. **The control is what makes the figure readable**, and it caught a bad measurement first: `\p{L}+` (675 ranges), `\w+` (767) and `\p{N}+` (143) are unreachable by any value this constant can take, so a first x86-64 A/B showing them at +23.4 % and +11.1 % was impossible by construction — the baseline binary had come from a different tree. Rebuilt correctly they read +0.1 / +0.1 / +0.2 %, the ASCII witness −0.5 %, and on arm64 this re-stamp moves ONLY the Han row (every other §Unicode row within 0.5 %, §A within 0.5 %). **Refuted and recorded rather than retried:** skipping UTF-8 continuation bytes in the leftmost scan. A continuation byte can never start a code point, so a 3-byte subject probes every non-member code point three times with two answers fixed in advance — but the strict decoder already rejects a continuation in about four mask tests, so the skip removes cheap work and adds a branch: arm64 `[à-ÿ]+` +14 % unconditional, +34 % gated on the lead byte, and `\p{sc=Han}` itself unmoved either way. Under the previous stamp, `2026.8.2` — **§A and §Unicode both re-stamped at `4efdd46`; the same one-line reordering was applied to three accessors, and how its cold half is PACKAGED decided each one differently.** All three asked two storage-mode questions — invariant for a whole walk — ahead of the row-key question that varies. `class_table` (once per match, top of a `run_*`) wants the cold half OUTLINED: bisected on executed conditional branches to the lazy per-class row fill, 3.00 branches per call over 1 270 040 calls, and the fix gives **`lookahead` 4.24 → 3.75 arm64 / 8.08 → 7.50 x86-64**, **`words` 2.35 → 2.22 / 3.39 → 2.87**, `digits` 1.44 → 1.38 / 1.89 → 1.79. The two code-point accessors (inside the per-code-point loop) are WRECKED by that same outlining — arm64 `[à-ÿ]+` **+39 %**, `\p{sc=Han}` +27 % — **while every deterministic counter called it an improvement** (Ir −10.2 %, Dr −4.1 %, Dw −11.2 %, Bc −9.6 %); and they are wrecked a second way by merely giving the cold half its own `always_inline` function, which costs gcc/x86-64 **10–17 % on §A rows that never touch it** (`digits` 1.813 → 2.114, 0.05 % spread) through the translation unit's inline budget. Folded in place, they move **every §Unicode row on arm64 (−1.4 % to −6.7 %) and seven of nine on x86-64 (to −14.2 % on `\p{N}+`, −8.2 % on `\w+` and `\p{L}+`)**, with the pure-ASCII witness in the same harness at −1.4 % / +2.0 %. Four §A rows are REAL's against PCRE2-JIT on both ISAs (`words` 1.05× arm64, `digits` 1.04×, `literal`, `hex` on x86-64) and `lookahead` reaches **0.97× on arm64**. **`fields` stays reclassified rather than chased**: its published regression does not reproduce in an isolated probe (HEAD 4.8 % FASTER than v2026.7.55 there) and the accessor work read −25 % in that probe against +1.0 % here — the row is not measuring the isolated scan, and the next move on it is a harness question. Under the previous stamp, `2026.8.2` — **§A is untouched again, by construction:** every change in this train is on the FIRST search a pattern performs, and §A's rows are steady-state ns/byte over a warmed regex. What moves is the first-use axis, which §E.4 publishes rows for and which this train does NOT re-run against the rust crate — the figures here are REAL's own, measured with one live regex per sample (200 kept alive, one first search each; see this file's methodology on why a construct-and-destroy loop cannot measure a first search at all). **The inner-literal route** decided its small-haystack guard AFTER building the byte program and abandoned on the very next line, so a short subject paid the whole expansion to learn it was too short to want it: `\w+@\w+` on 18 bytes **459.51 → 0.71 µs (647×)**, `(\w+)X(\w+)` on 13 bytes **466.91 → 1.23 (380×)**, `(\w+)@(\w+)` on 80 bytes 458.60 → 2.97 (154×), the ASCII gauge `[a-z]+@[a-z]+` 3.65 → 2.16, and an **8 KB subject 1804.59 → 1720.72 (1.05×)** — that last row the control showing the guard is size-gated rather than always-on, since 8 KB is past the 4 KB floor. Confirmed without a clock, which is what settled it when two timing protocols disagreed: a build counter reads **400 UTF-8 trie builds across those 200 searches before the change and 0 after**. **The UTF-8 trie** is built once per CLASS rather than once per occurrence, since it is a pure function of its code-point class (**637.78 → 476.48 µs, −25 %**); sharing one cache across the program's two expansions was written, wired and refused — it helps `(\d+)X(\d+)` by 8.2 % and hurts `(\w+)X(\w+)` by 2.0 %, and the large classes are the entire reason to care. **The compat `regex_iterator`** holds its walker instead of rebuilding a VM state per advance: `++it` **2.2×** on `[a-z]+` and **4.5×** on a 24-branch alternation, `it++` 1.8× and 3.8×, neither form regressing. **Still blind to the subject**, and disclosed rather than chased: for `\w`, over 99 % of what the expansion builds recognises code points a pure-ASCII subject can never contain. §A, §Unicode, §B, §E and §multi-pattern carry their earlier figures unchanged. Under the previous stamp, `2026.8.1` — **§A again cannot have moved, by construction.** Its table carries no capture-carrying pattern, and its one alternation row `the|fox|dog` has three branches — below `ac_branch_floor`, which this train lowered to 4 and no further — so the routing change cannot reach it. The two throughput changes land on shapes this document does not publish, and were measured on their own terms, on both platforms, as the minimum across alternating rounds against an untouched gauge. **Aho-Corasick below twelve branches**, where the route was previously never taken at all: on 4000-byte subjects at 600 candidates per 1000 bytes, **3.48×/4.34× at 4 branches (arm64/x86-64)**, 4.36/6.29 at 6, 5.28/8.90 at 8, **8.03/10.23 at 11**; at 200 candidates 1.74/2.24; and at 50–100 candidates **0.99–1.01× arm64, 0.98–1.02× x86-64** — that last row being the one that matters, since the sample window is now sized by the verdict rather than fixed and a fixed 256 had cost 2–5 % on exactly those sparse short alternations. The threshold in that region is the measured MAXIMUM rather than the minimum, because below twelve the automaton was taken never, so an early switch regresses where above twelve it could not; a `static_assert` holds the two constants in the right order. Reproduce with `make ac-regime`. **`regex_set` construction** stops building one full munch DFA per pattern to discover a set too small to fuse: **214.4 → 16.9 µs at 20 patterns (12.7×)** and **446.9 → 34.0 at 40 (13.1×)**, 11.2 µs per pattern of pure waste on every set under 56. Not carried further, and measured rather than guessed: replacing the probe for larger sets is worth 9–17 %, because the fused subset construction dominates and grows superlinearly (49 µs per pattern at 56, 100 at 120). **The compat `regex_iterator` is disclosed at 1.8–5.5× behind `find_iter`** and NOT fixed — a fresh VM state per advance, where the walker embeds 8232 bytes against that iterator's 320 and `std::regex_iterator` requires independent copies; against the `std::regex` it replaces, the same cases run 9.9×, 88.7× and 10.8× faster, so the gap is against REAL's own best path and not the promise. §A, §Unicode, §B, §E and §multi-pattern carry their earlier figures unchanged. Under the previous stamp, `2026.8.0` — **no published row was re-measured this train, and §A cannot have moved: by construction, not by inspection.** Its table carries no capture-carrying pattern, so the capture-pool reservation cannot reach it, and its one alternation row `the|fox|dog` has three branches — below the Aho-Corasick routing floor of twelve — so the new density gate cannot reach it either. The two changes that do carry throughput land on shapes this document does not publish, and were measured on their own terms, on both platforms, as the minimum across five alternating rounds against an untouched gauge. **Aho-Corasick routing** now selects on candidate density rather than branch count: a 24-branch alternation over a 4000-byte subject with no match goes **12.92 → 2.21 µs on arm64 (5.85×)** and **13.57 → 3.63 on x86-64 (3.74×)**, while match-dense and false-start subjects keep the automaton and their existing figures (0.97× arm64, 1.00× x86-64). The rule is a PRODUCT — `(candidates per 1000 bytes) × branch_count` — because the crossover moves 3× between 12 and 24 branches; the constant is the measured minimum across both platforms, since the route was taken unconditionally before, so switching early cannot regress what it replaces. Reproduce with `make ac-regime`, which prints the four regimes and a crossover sweep and compares match counts across arms before printing any ratio. **The capture pool** stops growing by doubling mid-search: 13 heap allocations become 5, and `make alloc-probe` reads the general route at **5/5/5 where it read 4/7/17** — allocation count is now a function of the ROUTE and not of the pattern, which is the property worth the stamp. Timing on capture-carrying shapes absent from §A: `((\w+))` **−20.0 % arm64 / −15.0 % x86-64**, `(\w+)@(\w+)` −7.0 / −6.2, `\w+@\w+` −9.4 / −6.1, against `[a-z]+` as the untouched gauge at 0.0 % / +2.3 %. **§E's `captures/*` rows are where the pool change would show in a published table, and they were NOT re-run** — that is a criterion campaign against the rust crate on its own protocol, and this train does not claim it. §A, §Unicode, §B and §multi-pattern carry their earlier figures unchanged. Under the previous stamp, `2026.7.63` — **re-measured after the `state_type` lift (`991480d`, `be9e801`): `bench_static`, both platforms, min of five interleaved runs.** The lift keys a compile-time storage's scratch on dimensions instead of the pattern's value; it is meant to change codegen sharing, not throughput, and that is what the rows show. **arm64 is the trustworthy leg** — no inlining cliff there, and the spread is tight: median **+0.7 % stat / +0.0 % dyn**, every row inside −3.0…+3.8 %. The `dyn` column doubles as the control, since that path never held the table pointers and now pays a null test where it had an `if constexpr`; it reads +0.0 %, so the substitution is free. **x86-64 cannot discriminate here and is reported as such**: median +1.5 % stat / −1.6 % dyn but a −16.5…+17.7 % spread, and read against the gauge nearly every large move is a draw (`[0-9]+` +17.7 % stat against −26.8 % dyn; `(\w+)@(\w+)` −16.5 % against −22.5 %). The one clean-gauge outlier, `(?i)cafe` +9.8 %, is the row that read +217 %, +9.9 % and +0.1 % across this train's variants — the canary, not a signal. What the lift *did* move is not in these tables: `.text` per added pattern **~45 KB → 7.7 (x86) / 7.2 (arm64)**, budget refusals in a 32-pattern TU **19 195 → 1457**, and an unrelated pattern's timing **33.5/33.7/20.4 µs → 11.8/10.7/10.8** at 11/12/14 patterns (docs/design.dox §10.1). Under the previous stamp — **re-measured after `9a341ca` (`extend_run` captures by value): the x86-64 cp-class rows only, and only those that survived the noise.** The change is gcc-only (`cpclass_gcc_loop.hpp` sits behind `__GNUC__ && !__clang__`), so **arm64/clang is unaffected by construction** — `c++ -E` finds zero `cp_class_hi_width` in the clang preprocessor output — and by measurement. On x86-64 (g++-14, interleaved A/B, both binaries fixed and alternated, `[a-z]+` as the drift witness at 2.82–2.87 ns/B throughout): `\d+` on the standard 64 KiB corpus **116.1/120.0/116.5 → 102.4/101.5/101.6 ns/B (−13 % wall, −7.4 % Ir**, three rounds); §Unicode `[à-ÿ]+` **7.94/8.00/8.63/8.19 → 6.97/6.98/7.12/7.27 (−13 %**, B ahead in all four rounds); §Unicode `\w+` mixed-script **5.14/5.21/5.89/5.19 → 5.05/5.05/5.11/5.12 (−2.5 %**, B ahead in all four, Ir −1.1 %); `(?i)café` a tie; `[a-z]+` byte-identical in Ir. **§A is unaffected by inspection, not by measurement** — its x86 table contains no code-point class at all, the `date` row being `[0-9]{4}-[0-9]{2}-[0-9]{2}`, three byte classes. **The `\p{}`/script band, re-measured under min-across-runs (5 runs per arm) after that protocol was established** — the first attempt read `\p{L}+` at 5.882 and 10.089 ns/B on two runs of the *same* binary and was abandoned as unmeasurable; the cause was episodic container interference contaminating whole contiguous rows, diagnosed and worked around under §Methodology. Against a **±3 % floor set by the unreachable rows in the same comparison** (`the|fox|dog` +3.0 %, `[0-9a-f]{8}` −2.1 %, `[a-z]+(?=[a-z])` −2.4 %, none containing a code-point class): `[à-ÿ]+` **7.994 → 6.950 (−13.1 %**, agreeing with the four-round interleaved figure above to a tenth of a point), `\p{N}+` **6.265 → 5.945 (−5.1 %**, one protocol only). **`\p{scx=Cyrl}` (−3.4 %), `\p{L}+` (−2.5 %), `\p{sc=Han}` (−1.8 %) and `\w+` (−1.4 %) sit at or under that floor and are therefore bounded, not measured** — they keep their earlier figures, and the honest claim is that the change does not move them by more than 3 %, not that it moves them by the number printed here. **re-measured for the `2026.7.62` stamp: five §E.4 rows** (`first_use/{email,word_bound,no_match}` at `d9deec7`, then `find/word_bound` and `captures/word_bound` after the cp-class scan change, arm64, one criterion group at a time with both engines in the same run), plus the `email` scan rows re-checked to confirm the cost went rather than moved (`find/email` 46.52 µs against 46.20 recorded, `captures/email` 51.98 against 52.10). `first_use/email` reaches **parity** (604.53 µs against the crate's 601.81, CIs overlapping) from 2.64× behind, because the one-pass extractor is no longer built for routes that cannot consult it — the capture-free twin `\w+@\w+` cost the same 1487 µs and settled it. NOT re-measured under the interleaved A/B protocol, which is why those rows sit in their own table in §E.4 rather than as a column in the one above them; §A, §Unicode, §B and §multi-pattern carry their earlier figures unchanged. **The 7.61 stamp below is retained verbatim, since §E.4's main table is still its measurement.** Under `2026.7.61` — **re-measured for that stamp: §E.4 only** (the crate's own criterion rows, arm64, at `3cb085a`), by an interleaved A/B against `v2026.7.60` with the same bench file on both sides, one group at a time, machine otherwise idle: two rounds per group and **four rounds of 5 s for `captures`**, which was load-bearing — `captures/word_bound` read +3.1 % on two rounds and **−0.3 % on four**. No row regresses; every row other than the four `icase_class` gains lands within ±1.6 %. **The row this train was about:** 7.60 added an `icase_class` family because the suite had no case-insensitive pattern at all, a gap two compile-cost defects had already come through, and it read 3.37× behind the `regex` crate immediately. The profile refuted the obvious explanation — `(?i)[a-z]+` was not on the code-point scan but on the lazy DFA (`lazy_dfa::anchored_end`, 23.5 %). `find/icase_class` **631.8 → 252.1 µs (−60.1 %)**, and **3.37× behind becomes 1.34×**; `captures/icase_class` **680.0 → 295.8 (−56.5 %)**, 3.66× → 1.57×; `compile/icase_class` −30.8 % (9.66× ahead → 14.01×); `first_use/icase_class` −39.6 %. Under icase `[a-z]` gains the long s and the Kelvin sign, both MULTI-BYTE, so the class was expanded to a byte-level alternation — 8 byte classes and 18 instructions against 1 and 5 for `[a-z]+` — which stopped being a class loop and matched no route at all: two rare fold partners were costing the route. An ASCII bitmap with a few non-ASCII members IS a code-point class and is now emitted as one. arm64 walk `(?i)[a-z]+` −57.7 % and `(?i)[a-zA-Z0-9_]+` −58.4 %; x86-64 instructions −58.2 % and −58.7 %; `[a-z]+`/`\w+`/`.`/`[^,]+`/`dog`/`(?i)dog`/`\b\w+\b` at 0.0 % on arm64 and within 0.7 % on x86-64. **`real::dfa` widened, not narrowed:** it refused `klass_cp` outright, so this change would have made those patterns unconstructible there — a capability regression, not an acceptable price — and the refusal was a limit of the entry point, so `dfa_flatten` now expands `klass_cp` through the same `build_byte_program` the lazy DFA has always used. Text-mode `\d`/`\s`, Unicode properties, non-ASCII classes and folded ASCII classes all build there now, where that API had never accepted one. §A, §Unicode, §B and §multi-pattern carry their earlier figures unchanged. Per-train benchmark-impact log: CHANGELOG.md (full release notes: docs/release-notes/ + GitHub Releases). **A second-order cost the fuzzer found**, within the hour and through a path the change never touched: `regex_set` builds one of these DFAs internally, so widening what the DFA ACCEPTS widened what it ATTEMPTS — on `^\w` it spent 27.9 s where it had errored immediately. `max_dfa_states` bounds the RESULT of subset construction and nothing bounded the WORK, which is superlinear: a folded ASCII class expands to 26 instructions and builds in 0.04 ms, `\d+` to 261 and 1.88 ms, text-mode `\w+` to **3434 and 418 ms**. `max_dfa_byte_program` = 512 sits between them, and everything it refuses was already refused before; a repeat multiplies the expansion, so `\d{2}` is already 517. Reproducer **27.91 s → 0.33 s**. **Disclosed, not chased:** `no_match` 2.09×/2.05× (a ratio on 1.6 µs against 778 ns), `captures/word_bound` 1.82×, `find/literal` 1.57×. (`(\w+)@(\w+)` first use was listed here at **2.64× behind**; it reached parity after this stamp — see §E.4.) |
| Machines | §A on **two ISAs**: devbox (`x86-64`, g++ 13.3.0) *and* Apple M1 Pro (`arm64`, Apple clang 16). §B / §E on M1 Pro (§E's x86-64 leg noted inline where it diverges — see §E). §multi-pattern measured on **x86-64 devbox** (g++ 13.3, RE2 + Hyperscan 5.4) |
| Engines | `std::regex`; **PCRE2 10.47, JIT on, both ISAs** (built from source on x86-64 to pin the exact version — and the pin only applies when `PKG_CONFIG_PATH` points at that build, since the recipe resolves the library through `pkg-config` and the system package otherwise wins silently; `make bench-engines` now prints the version it actually LINKED, because this document named 10.47 for a leg that had measured 10.42 and nothing in the output could contradict it); RE2 (10.0 on x86-64, 11.0 on arm64 — version-differs-by-leg, uncontested given the margins). Multi-pattern: RE2::Set, Hyperscan (optional). §E: rust `regex` 1.12.4 |
| Python | CPython 3.14.6, `re` (stdlib) vs the in-place REAL `2026.7.55` extension (§B re-measured for this stamp, arm64 M1 Pro, N = 40 paired samples, bootstrap CI) |
| Method | §A: median of N = 30 paired batches, bootstrap CI, **three full runs per ISA with the minimum taken per cell** (both ISAs, this re-stamp — one run does not survive the x86-64 container's episodic interference); match counts equal on every case, both ISAs. §E: best-of-15, REAL `count_matches` vs rust `find_iter`/`captures_iter`, match counts equal. §multi-pattern: best-of-7, `make bench-multipattern`. **Every ratio below is computed programmatically from the raw ns/B pair — `benchmarks/verify_bench_ratios.py` re-derives and checks all of them** |

## A. C++ engine throughput

Each engine compiles the pattern once, then counts all non-overlapping matches over the same corpus; only
the scan is timed. `ns/B` is nanoseconds per corpus byte (lower is better). `(x)` is *engine_time /
REAL_time* — **> 1 means REAL is faster**. Match counts agreed across all four engines on every case, on
both ISAs, on the same `c0f75ec` tree for this re-stamp.

**Protocol for this re-stamp:** three full harness runs per ISA, each already a median of N = 30
paired batches, then the **minimum per cell across the three**. One run is not enough on either
machine, and this set shows why in the cleanest form yet: on **each** ISA exactly one run steps out
on exactly one row, and it is a different row per ISA — x86-64 `words` reads 1.470 / 1.582 / 1.471
(7.6 %) while four of its other rows hold inside 1 %, and arm64 `lookahead` reads 3.816 / 3.802 /
4.129 (8.6 %) while six of its rows hold inside 1 %, `words` and `digits` at 0.1 % and 0.0 %. That is
the episode shape described under Methodology: a burst that lands on whatever is scanning when it
arrives, not a property of the row. A minimum across runs is what survives it; a mean would not.

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
version it LINKED for this reason, and that one line has now caught the same trap twice.

**x86-64** — devbox, g++ 13.3.0, N = 30 × 3 runs, PCRE2 10.47-JIT, RE2 10.0:

| case | REAL ns/B | std::regex | PCRE2-JIT | RE2 |
| --- | ---: | ---: | ---: | ---: |
| words `[a-z]+` | 1.47 | 29.19 (**19.86×**) | 6.42 (**4.37×**) | 28.25 (**19.22×**) |
| digits `[0-9]+` | 1.03 | 24.64 (**23.81×**) | 3.64 (**3.52×**) | 16.87 (**16.30×**) |
| fields `[^,]+` | 6.00 | 25.00 (**4.17×**) | 4.94 (0.82×) | 23.02 (**3.84×**) |
| alternation `the\|fox\|dog` | 2.29 | 30.60 (**13.36×**) | 2.25 (0.98×) | 10.40 (**4.54×**) |
| date `{4}-{2}-{2}` | 0.68 | 18.50 (**27.09×**) | 0.62 (0.91×) | 4.09 (**5.99×**) |
| hex `[0-9a-f]{8}` | 1.36 | 20.82 (**15.26×**) | 1.98 (**1.45×**) | 4.08 (**2.99×**) |
| literal | 0.44 | 15.09 (**34.61×**) | 0.59 (**1.35×**) | 2.38 (**5.46×**) |
| anchored `^[a-z]+$` | 0.51 | unsupported | 0.52 (**1.01×**) | 1.78 (**3.46×**) |
| lookahead `[a-z]+(?=[a-z])` | 7.83 | 76.09 (**9.72×**) | 6.81 (0.87×) | unsupported |

**arm64** — Apple M1 Pro, Apple clang 16, N = 30 × 3 runs, PCRE2 10.47-JIT, RE2 11.0:

| case | REAL ns/B | std::regex | PCRE2-JIT | RE2 |
| --- | ---: | ---: | ---: | ---: |
| words `[a-z]+` | 1.16 | 91.93 (**79.05×**) | 2.32 (**1.99×**) | 13.90 (**11.95×**) |
| digits `[0-9]+` | 0.85 | 82.62 (**97.66×**) | 1.44 (**1.70×**) | 8.51 (**10.06×**) |
| fields `[^,]+` | 3.20 | 74.48 (**23.28×**) | 1.86 (0.58×) | 11.03 (**3.45×**) |
| alternation `the\|fox\|dog` | 1.98 | 111.02 (**56.10×**) | 1.56 (0.79×) | 6.13 (**3.10×**) |
| date `{4}-{2}-{2}` | 0.49 | 72.43 (**148.73×**) | 0.39 (0.80×) | 3.41 (**7.00×**) |
| hex `[0-9a-f]{8}` | 1.42 | 80.75 (**57.03×**) | 1.24 (0.88×) | 3.40 (**2.40×**) |
| literal | 0.25 | 30.82 (**124.78×**) | 0.48 (**1.94×**) | 1.36 (**5.51×**) |
| anchored `^[a-z]+$` | 0.32 | unsupported | 0.42 (**1.32×**) | 2.18 (**6.83×**) |
| lookahead `[a-z]+(?=[a-z])` | 3.80 | 156.81 (**41.24×**) | 3.65 (0.96×) | unsupported |

**Reading — verdict brut, no dressing up.**

- **REAL ≫ `std::regex`**, always: 4.1–37.6× on x86-64, 23.5–139.3× on arm64 (libc++'s
  `std::regex` falls even further behind on arm64). Never below 4.1×.
- **REAL > RE2**, always where RE2 supports the pattern: 3.1–9.9× on x86-64, 2.4–6.6× on arm64.
- **REAL vs PCRE2-JIT: four rows are REAL's on both ISAs now**, where the previous stamp had one.
  `words` (**2.23×** x86-64 / **1.05×** arm64), `digits` (**2.03×** / **1.04×**), `literal`
  (**1.41×** / **1.94×**), and `hex` on x86-64 (**1.43×**, 0.87× on arm64). PCRE2 keeps `alternation`
  (0.97× / 0.82×), `fields` (0.83× / 0.59×), `date` (0.90× / 0.76×) and `lookahead` (0.90× /
  **0.97×**) — that last row now within 3 % on arm64, and it is the one PCRE2 wins by backtracking.
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
- **The lookahead line is now within 3 % of PCRE2-JIT on arm64** (0.97×) and 0.90× on x86-64, against
  the pre-P3c general-VM order (~92 / ~48 ns/B), and against 8.08 / 4.24 at the previous stamp of this table. REAL does a **bounded lookaround in linear time**;
  PCRE2 is faster here but by **backtracking** (itself ReDoS-able on a crafted lookaround), and
  **RE2 and the rust crate cannot compile the pattern at all** (`unsupported`). `find_iter` / Python
  `finditer` do not get the P3c fast path by construction (return type fixed at compile time so pure
  `[a-z]+` does not regress) — this row is `count_matches` only, stated plainly so it is not read as
  a `find_iter` number.
- **The gauge that makes the above readable:** `std::regex` and RE2 are third-party constants here,
  so their columns are the drift witness. Across this re-stamp and the one before it, RE2 lands
  within 1 % on every x86-64 row (28.24, 16.89, 22.74, 10.39, 4.08, 4.08, 2.38 against 28.26, 16.81,
  22.95, 10.39, 4.09, 4.09, 2.38), which is what licenses reading a −16.8 % REAL row as REAL's.

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

**Stamp.** REAL `2026.8.5+` (tree `c0f75ec`), both ISAs re-measured for this stamp, on the same
three-runs-per-ISA / minimum-per-cell protocol as §A. **arm64** table below: Apple M1 Pro, Apple
clang 16, `-O2`, N = 30 (`make bench-engines`). **x86-64**, same harness and N, g++ 13.3 with PCRE2
10.47 (from source — the version the binary LINKED, printed by the harness) and RE2 10.0: `\w+` mixed
**3.678** (pcre2 0.95×, re2 **1.35×**), `\p{L}+` CJK **4.231** (0.67× / re2 **4.55×**), `\p{N}+`
**4.278** (0.60× / **1.74×**), `sc=Han` **5.909** (0.78×), `scx=Cyrl` **6.697** (0.96×), `(?i)café`
**2.057** (0.29× / **1.37×**), `[à-ÿ]+` **2.906** (**2.02×** / **8.70×**), literal `你好` **0.818**
(pcre2 **1.26×** — the same crossing as arm64), `.` emoji **7.627** (pcre2 **1.35×**), ascii witness
**1.470** (**4.36×**). Oracle: exhaustive `\p{L}` over U+0000..10FFFF (surrogates skipped) —
**0 mismatch**.

**`(?i)café` is this document's worst ratio, and it is now diagnosed rather than merely reported.**
Its cost is NOT the case-insensitivity, which the obvious reading blames: plain `café` used to be
*slower* than `(?i)café` (1.761 vs 1.221 ns/B on arm64), because the fold turns `é` into a code-point
class and that stops the fixed-offset walk before it reaches the high bytes. Decomposed on one corpus
at equal match density: a pure literal `café` is **0.257**, the same thing written as an explicit
two-member class `caf[éÉ]` is **0.700**, and `(?i)café` is **1.226** — so the dominant step is
emitting the fold as a code-point class, and the icase prefix costs again on top. `é` and `É` are
both two bytes sharing one lead, which is a fixed-width shape a byte class could express; the
alternation form the 7.61 note refuses (`caf(é|É)`, **3.405**) is a different and worse thing. Not
fixed here — measured, decomposed, and left where the next attempt will find it.

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
| `\w+` (mixed-script) | 2.295 | 59.76 (**26.04×**) | 1.86 (0.81×) | 3.19 (**1.39×**) | 16218/5406/16218/5406 ⚠ |
| `\p{L}+` (CJK) | 2.908 | unsupported | 1.33 (0.46×) | 13.18 (**4.53×**) | 12904/—/12904/12904 |
| `\p{N}+` (arabic digits) | 1.872 | unsupported | 1.60 (0.85×) | 5.20 (**2.78×**) | 6250/—/6250/6250 |
| `\p{sc=Han}` (CJK) | 3.339 | unsupported | 2.14 (0.64×) | unsupported | 25808/—/25808/— |
| `\p{scx=Cyrl}` (mixed-script) | 2.642 | unsupported | 2.68 (**1.01×**) | unsupported | 32436/—/32436/— |
| `(?i)café` (accented) | 1.286 | unsupported | 0.34 (0.26×) | 1.31 (**1.02×**) | 3509/—/3509/3509 |
| `[à-ÿ]+` (accented) | 1.572 | 85.88 (**54.63×**) | 2.05 (**1.30×**) | 12.52 (**7.96×**) | 38599/38599/38599/38599 |
| literal `你好` (CJK) | 0.457 | 29.06 (**63.59×**) | 0.59 (**1.29×**) | 2.62 (**5.73×**) | 6452/6452/6452/6452 |
| `.` (emoji, one codepoint) | 4.665 | 58.55 (**12.55×**) | 3.75 (0.80×) | 18.95 (**4.06×**) | 68306/200039/68306/68306 ⚠ |
| ascii witness `[a-z]+` | 1.164 | 92.43 (**79.41×**) | 2.25 (**1.93×**) | 13.93 (**11.97×**) | 42108/42108/42108/42108 |

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
