# Changelog

Per-train benchmark-impact log: what each release train measurably touched (or explicitly did not touch) in `docs/BENCHMARKS.md`'s §A/§E/§B/§Unicode/§multi-pattern sections, carried verbatim from that file's Version row. This is not the release notes — for the complete per-release description of features, fixes, and breaking changes, see `docs/release-notes/` and the GitHub Releases page.

## v2026.8.14

8.14 (**a per-call copy, a routing quantity, three allocations — and three instruments that were reporting silence as success**): **The instruments were lying, and the last one to be fixed found the engine's largest single cost.** **THE TABLES BELOW DO NOT MOVE IN THIS TRAIN, AND THAT IS MEASURED:** the two engine changes are a per-call copy and one routing decision, neither of which has a row here, and the third — an allocation removed from the general VM — was judged on the 26-row consumer instrument at **0 rows REAL** on an idle x86-64 host with the governor pinned. **WHAT MOVED, WITH NO ROW HERE TO SHOW IT:** the second bulk slot copy per call is gone (the engine fills the result in place), and its OWED x86-64 leg is paid — **−27.8 % / −16.0 % / −15.6 % / −14.1 %** on the four per-call rows, 24 of 24 draws agreeing, against arm64's −9.4 / −8.4 / −7.4 / −6.4 %; the x86 gain is two to three times the arm64 one because gcc lowers that move to a `rep movsq` worth 12.02 % of the inlined per-call path and Apple clang emits no such instruction, so **the two legs differ by COMPILER as much as by ISA**. The Aho-Corasick gate gained the second quantity v2026.8.13 recorded and deliberately withheld: **−30.0 % arm64 / −18.4 % x86-64** on the mis-routed regime, against a verification cost of +3.4 % / +0.12 %, with verdicts moving automaton→cascade ONLY across ten density/completion combinations. And the COW capture pool stopped allocating three times per general-VM call — **five allocations become two, 368 bytes become 176** — found by counting, resolved by size AND by symbol, fixed in `storage.hpp` because `pike.hpp` sits below it and has no `small_vec` within reach. **ONE REFUSAL, PUBLISHED WITH THE TABLE THAT REFUSED IT:** raising the `mark` inline capacity to 16 would close the last two allocations and charges the four per-call rows **+4.1 % to +5.6 %** (19–22 of 24 draws) for −3.1 % on its target. No row is REAL either way, and with both sides under the bar the tie goes to not growing a per-call object; the tier that would cover the measured 11–73 instruction band is 64 or 128, i.e. +896 or +1920 bytes on a state already near 4944. **§A's PROSE WAS WRONG FOR THREE STAMPS WHILE THESE TABLES WERE RIGHT**, and two bullets said the OPPOSITE of the cells above them (`alternation` filed as a PCRE2 win at 0.94× where the table has read a REAL win since v2026.8.11; `lookahead` claiming 0.97× where it reads 0.82×, a trade the notes had already published). The checker that would have caught it was **wired to nothing** while this section claimed in print that it re-derives every ratio; it failed on two cells the first time it ran, a third once its rounding rule was made exact, and it now reads the PROSE too — 65 cell ratios and 18 bullet claims, plus row-label uniqueness — from `make check-bench-ratios`, local gate step 7b, `gate-doc`, and the Docs-site workflow. **AND THIS COLUMN'S arm64 LEG WAS RE-MEASURED AND CONFIRMED, NOT CORRECTED:** declared uniformly too slow on the unrecorded-power-state inference, three passes on AC reproduce every published cell within **−2.4 % to +3.6 % on REAL** and −6.3 % to +0.9 % on the competitors, inside the 1.00×–1.08× inter-run amplitude already declared — so the cells stand, and the premise for doubting them does not. **NOT FIXED, AND NAMED AS THE NEXT TARGET:** the general VM costs **12 to 25 ns per byte stepped** against ~1 for a routed class loop and takes 4911 of 7406 composed patterns; the trim shape carries ONE live thread on 100 % of its 2050 steps and performs 4833 refcount/COW operations to report a single match, with no capture groups. Nothing measured exceeds 15 live threads and no program exceeds 73 instructions.

## v2026.8.13

8.13 (**a veto restored, and a per-call copy removed**): **A veto this repository wrote for exactly one case was crossed by v2026.8.12, and this train puts it back.** `matrix-gate`'s `date dense` cell fails when the routed path is slower than the core it replaces; v2026.8.12's inner-literal batch filler made it so — **route 2.883 against core 2.563**, where v2026.8.11 read 2.617 / 2.584 — and the release shipped because a hand-picked subset of gates was run instead of the canonical twenty-four. The route's sticky abandon was working; the WALK was not listening, so a route that had given up on the haystack was retried on every match (**3637 attempts against 7**) and each attempt was a wasted memmem before the per-match path did the real work. The filler now disarms for the rest of the walk when the route abandons: **`date dense` 2.883 → 2.609**, its pre-train value, with `email dense` unchanged at 2.044 against the core's 8.730 and `\w+@\w+` still batched at 0.251 engine entries per match. **MATRIX CLEAN.** **THE TABLES BELOW ARE UNCHANGED AND THAT IS A MEASURED CLAIM, not an omission:** of the five commits since v2026.8.12 only one touches a hot path, and its judgement against calibrated floors read **22 of 26 rows indistinguishable on BOTH ISAs**, every REAL row being a per-call one — a regime whose fixed cost is 0.0004 ns/byte over the 100 KB corpora §A measures. **NOT IN THESE TABLES, for want of a row rather than for want of a number:** the slot storage is now taken by rvalue reference instead of by value, which removes one of two bulk copies per call (gcc lowered them to two `rep movsq`, 12.15 % and 10.20 % of the inlined per-call path). Judged on both instruments — **x86-64 −13.2 % / −10.0 % / −9.6 %** and **arm64 −11.8 % / −8.2 % / −7.9 % / −7.2 %** on the per-call rows, everything else indistinguishable, no cross-row toll — and this is the first change this project has judged on x86-64 at all: that host's floors fell from 27–59 % to **1.8–8.3 %** once its governor was set to `performance`, so the same change was unjudgeable there a day earlier. **§B, RE-MEASURED AND UNMOVED** over three passes: five headline rows inside 2 % (`words · findall @100KB` 2.16–2.24× against 2.13×, `digits · sparse` 12.22–12.33× against 12.33×, `literal · hit` 11.79–11.82× against 11.89×, `anchored miss` 0.91–0.92× against 0.90×, `date · search` 428–436× against 432.86×), the sixth being that section's known-unstable `sub · dates with refs` at 57.35–79.68× across the three, straddling the 56.84–76.08× spread already documented for it. **AND ONE CONFIDENCE WITHDRAWN WITHOUT ANY NUMBER MOVING:** the Aho-Corasick gate arbitrates on candidate density, and that quantity provably cannot arbitrate — holding it FIXED and varying only the fraction of candidates that complete flips the verdict (arm64 0.87× → 2.19×, x86-64 0.56× → 1.90×), because a false start punishes the cascade while a match rewards it. No constant was retuned: the fix needs a second quantity, and retuning against those tables would move the error rather than remove it. See the gate's own note.

## v2026.8.12

8.12 (**three more routes, and the costs published as prominently as the gains**): **Three more routes stopped paying a route entry per match, and this stamp publishes what the train COST as prominently as what it gained.** The batching that had reached six route families now reaches the **lazy DFA** (a pattern no shape recognizer claims — `[a-z]+|[0-9]+`, the plain tokenizer idiom), the **exact literal**, and the **inner literal**. Two of those had no row in this table and still do not: it cannot be extended cheaply (a single added data row was measured charging `words` +27.7 % on gcc/x86-64), so their figures below come from the consumer instrument and are labelled as such. **WHAT THIS TABLE SHOWS:** `literal` **0.46 → 0.37 ns/B on x86-64 and 0.25 → 0.21 on arm64**, its ratio against PCRE2-JIT going **1.29× → 1.65×** and **1.94× → 2.42×** — the one gain of the train with a row here. **WHAT IT COSTS, IN FULL:** `date {4}-{2}-{2}` **+17 % / +13 %** (0.83× → 0.71× and 0.79× → 0.71× against PCRE2-JIT) and, on arm64 only, `lookahead [a-z]+(?=[a-z])` **+18 %** (0.97× → 0.82×). The class-scan band moves +1 % to +9.8 % on x86-64 and −0.3 % to −5.1 % on arm64 — **opposite directions on the two ISAs, which this document reads as code PLACEMENT and not as cost**, and `alternation` does the same (−10.8 % / +4.2 %). Inter-run amplitude over three runs per leg: 1.00×–1.12× on x86-64, 1.00×–1.08× on arm64, so the movements above are outside it. PCRE2 10.47 verified LINKED on both legs by the harness's own `linked:` line, not by a compile-time probe (see the note above this table). **AND THE CUMULATIVE READING THE PREVIOUS STAMP OWED AND DID NOT TAKE.** The whole train was judged on `bench_minimal` — the consumer-shaped unit that decides what lands — v2026.8.11's library against this one, the SAME bench source on both sides, 24 paired draws against recalibrated floors. Ten rows are REAL: `lookahead find_iter` **−91.8 %**, `short stamp search exact` **−87.2 %**, `short stamp match hit` **−86.6 %**, `short stamp match reject` **−73.3 %**, `alt class [a-z]+|[0-9]+` **−41.7 %**, `literal charlie` **−20.8 %**, `inner lit \w+@\w+` **−11.1 %** — against `date {4}-{2}-{2}` **+15.0 %**, `lookahead [a-z]+(?=[a-z])` **+12.6 %** and `short trim replace` **+4.6 %**. Sixteen rows indistinguishable. **The two costs are therefore REAL COSTS OF THE ENGINE, not properties of this four-engine binary** — which is the opposite of what the four-engine reading alone would have suggested, and the reason the cumulative run was worth taking. Both are trades, and the trades are legible: the trailing-lookaround rework pays +12.6 % on `count_matches` to return −91.8 % on `find_iter`, and keeping anchored fixed shapes pays +15 % on `date` to return −73 % to −87 % on the three per-call rows. **NOT CLAIMED:** no cost here is attributed to a single commit — the train is 30 commits and the judgement is cumulative. **§B, RE-MEASURED AND UNMOVED:** the Python binding sees almost none of this — five of six headline rows within 2 % over three passes, the sixth being the section's known-unstable `sub · dates with refs`. That is not a disappointment but a location: the binding's dominant cost is the Python round trip, not the scan, so an engine train lands in §A and stops at the boundary. **NOT IN EITHER TABLE:** `https?://` 1.90 → 1.24 ns/B (a scan-strategy exclusion removed from the lazy-DFA arming), and a per-call fixed cost that only gcc paid — 4944 bytes zeroed per state construction, 112.1 → 82.5 ns on x86-64 g++ 15.2 and 103.5 → 50.8 on arm64 g++-14, invisible to every instrument this project had until a second compiler was added to it.

## v2026.8.11

8.11 (**five routes stopped paying a route entry per match, and the two instruments disagree**): **Seven routes crossed one full route entry per match; five of them no longer do, and this stamp reports both instruments because they disagree.** The batching that had reached three class routes now reaches the bare single class, the counted minimum `{k,}` (byte AND code-point), the dropped-`\b` and KEPT-`\b` class runs, the bare possessive (byte and code-point, by recognition-time redirect rather than a filler), and the fixed alternation below the Aho-Corasick branch floor. **Five rows are NEW here** — `single [a-z]`, `words [a-z]{4,}`, `words [a-z]++`, `\b\w+\b`, `\w{2,}` — added because a route this table does not measure regresses in silence, which also means no stamp before this one compares to it. Headlines: **`alternation` crosses ahead of PCRE2-JIT on x86-64 for the first time, 0.94× → 1.22×** (arm64 0.78× → 0.92×); `single [a-z]` enters at **4.70× / 1.82×**, `words [a-z]{4,}` at **3.05× / 2.13×**, `words [a-z]++` at **3.58× / 1.85×**. **THE COSTS, ON THIS INSTRUMENT, IN FULL:** arm64's Unicode property band pays for the train — `\p{sc=Han}` 2.312 → 3.156 (0.93× → 0.68×), `\p{scx=Cyrl}` 2.743 → 3.581 (0.98× → 0.75×), `\w+` 1.970 → 2.278 (0.94× → 0.82×), `[à-ÿ]+` 1.617 → 1.975 — and x86-64's `words` 1.56 → 1.79 with `digits` 1.04 → 1.34. Those are real on this binary and are not dressed up. **AND THIS IS WHERE THE TWO INSTRUMENTS PART.** `docs/MEASUREMENT.md` §5.5: this harness links `<regex>`, PCRE2 and RE2 beside `real.hpp` and sits on the compiler's per-unit inlining budget; a CONSUMER compiles only `real.hpp`. Judged in a unit shaped like that one, the same changes read `\w{2,}` −38.1 % and `\p{L}{3,}` −36.2 % over 24 paired draws **with every other row indistinguishable, `\p{L}+` included at −0.3 %** — where this binary charged it 4.9 % and then 7.6 % for the same code. The consumer instrument decides what lands; this one decides what is published. Both appear, and the Unicode band's movement above is a property of THIS binary, not a claim about the engine. **What is NOT claimed:** the cumulative arm64 cost of the train was never judged as a distribution — the per-change verdicts were taken on x86-64 — so read those four Unicode figures as this stamp's reading, not as attributed to any one commit.

## v2026.8.10

8.10 (**the two weakest rows are no longer the two weakest rows**): **The two weakest rows in this document are no longer the two weakest rows, and the defect was in the HINT TABLE rather than in any loop.** Profiling said `fields [^,]+` (0.58× against PCRE2-JIT) and `.` (0.81×) were not scan measurements at all: `[^,]+` cost ~19 ns of fixed per-MATCH overhead plus ~0.086 ns/byte, flat at 18.6 / 19.3 / 18.8 ns per match for fields of 4, 8 and 16 characters, so on this harness's 4–7 character fields the fixed cost WAS the measurement — which is also why `words [a-z]+`, whose matches are longer, sat at parity. Batching requires `greedy_class_loop` or `greedy_cp_class`; the `.`/negated-class shape sets neither, so it crossed a full route entry per match where the other two class routes cross one per sixteen. Given the missing filler: **`.` −52.6 % / −50.3 %, 0.81× → 1.75× and 1.25× → 2.67×**; **`fields` −26.8 % / −52.8 %, 0.58× → 0.82× and 0.80× → 1.70×** — `fields` now LEADS PCRE2-JIT on x86-64 and `.` leads it on both. Also in the train, with no row of their own: an alternation of single atoms now compiles to the byte or code-point CLASS it is, one program for both spellings — `(?:e|o|u)+` **7.05 → 1.105 ns/B (6.2×)**, `(?:a|b|c)+` 3.7×, `(?:é|à|è)+` **7.47 → 1.904 (3.9×)**, each at parity with its hand-written class. **What moved the other way, and what is NOT claimed about it:** six rows regress 3–15 % on arm64 and 0.4–4.3 % on x86-64, with `\w+` at +15.3 % on one and −1.4 % on the other — rows disagreeing in DIRECTION across ISAs are placement, rows agreeing in direction but not magnitude are partly that and partly one more function in the translation unit, and this stamp does NOT separate the two -- it reports both signatures rather than picking whichever attribution reads better. The delta spans three commits, so no row's movement here belongs to a single change.

## v2026.8.9

8.9 (**a limitation lifted, a route found, three figures withdrawn**): **A limitation lifted, a route found, and three of this document's own performance figures withdrawn.** A possessive quantifier over a non-ASCII LITERAL was rejected outright — `é++`, `あ++`, `𝔘++` all threw "compound body" — while `[é]++` compiled and so did `(?i)é++`. Same character, same language, three answers: Tier-1 eligibility is tested on the node KIND, and a non-ASCII literal is a concat of UTF-8 bytes, so it could never qualify. Promoted to the one-member code-point class it equals, it does; **verified against CPython 3.14.6 on all eight discriminating cases**. The second change was found by COUNTING rather than timing: the route counters showed `é+`, `[é]+` and `[éàèùç]+` dispatching `lazy_dfa_anchored` **once per match** where `[à-ÿ]+` needed none, because v2026.8.6's fixed-width class emission — right for a bare class — turns a quantified one into a repeat of a two-BYTE sequence that no route recognises. `[éa]+` was the witness that settled it: one ASCII member makes the pattern FAST. Both now route as code-point classes: **`é+` 5.62 → 1.765 ns/B (3.19×)**, `[é]+` 3.18×, `[éàèùç]+` 8.44 → 1.915 (4.41×), parity with `[a-z]+`, identical to three decimals on two independent builds. **And three figures are taken back.** v2026.8.8's arm64 **+5.6 % is retracted** — rebuilt it reads −0.01 % / +0.03 %, it was one build's code PLACEMENT, and the evidence given for it (an ASCII witness moving with `words`) proves nothing, since two instances of one pattern share compiled code and so move together under placement exactly as under cost. Its x86-64 cost IS placement, established: every out-of-line engine symbol byte-identical, forced 16/32-byte alignment collapsing the delta, and the delta reappearing on OTHER rows. And v2026.8.7's `cold` figure is requalified — it reproduces on two unaligned builds (`words` −11.95 / −12.14 %) but reads **+5.70 %** under 1 % of code inflation, sign reversed, because `cold` IS a placement directive and the discriminator cannot judge one. The annotations stay; the number describes the shipped build, not the change.

## v2026.8.8

8.8 (**four answers where there should have been errors**): **Four answers where there should have been errors — and each was an error the engine already knew how to raise, somewhere else.** A result from a temporary regex borrowed the pattern text and named-group table it took with it, so a STORED result asking for a group by name read freed memory (**AddressSanitizer: heap-use-after-free** through `~dynamic_program`); `find_iter`/`find_all` were already deleted on an rvalue regex for that exact reason, and the single attempts could not be — the one-expression form is safe, and this repository alone has 868 of them. A lookaround index rode in a `uint16` unchecked, so the 65536th assertion pointed at sub-pattern 0 and **`(?=b)a` matched "a"** with no `b` anywhere. `$18446744073709551616` is 2^64, wrapped a `size_t` to **group 0**, and substituted the whole match where Python raises. And `[\d-z]` matched "-": the range rule was implemented on ONE SIDE only — `[a-\d]` already raised, so the engine had chosen Python's rule and applied half of it. **What that cost, published rather than mentioned:** the lifetime fix redistributes gcc/x86-64 — **`digits` 1.08 → 1.21** and `words` 1.67 → 1.76, against `alternation` 2.38 → 2.29, `date` 0.75 → 0.70 and `literal` 0.44 → 0.43 the other way — while arm64 is flat (`words` and `digits` unmoved to two decimals). Which is exactly why both tables are re-stamped here instead of carried over: shipping the previous table would have published numbers already known to be wrong. The SHAPE of that fix was chosen by measuring three built variants, not by argument — folding the owner into the shared result type cost arm64 `words` **+5.6 %**, marking it `cold` cost **+11.2 %**, separating the two result types costs **+0.19 % worst row**. The mechanism is NOT established and is not guessed at; on arm64 it is not gcc's per-unit inline budget, clang having none. Also fixed: `real::match_result` named a type it could not hold — wrong since the first commit — and now DERIVES from `real::regex::result_type`, which makes that class of drift unrepresentable rather than merely detectable.

## v2026.8.7

8.7 (**two silent wrong answers, and one shape behind both**): **A correctness train, and the two bugs in it share one shape: a cache or a hint validated by a key too weak to identify what it stood for.** One checked a program's identity by an ADDRESS that copy-assigning a vector reuses, so a regex assigned onto a warmed one kept answering as its old pattern — **3230 matches where the new pattern matches none, 0 where it matches 3125**. The other recognised an inner literal by its FIRST BYTE, so a prefix repeating that byte was taken for the literal and the route walked back to the wrong place — `\w\d(?:a){2}ax` and `(?:\d+xy){1}xz` returned NO MATCH on subjects that match. Both are silent: no crash, no exception, no diagnostic. **And the suite was blind to both BY CONSTRUCTION** — it exercised only move-assignment (which changes the address, so the stale key correctly fails) and only short dynamic subjects (where the size guard abandons the faulty route before its hint is used). `static_regex` has no such guard, so it failed at ANY subject size. The perf work in the same train: build-time functions marked `cold` so they stop bidding for the scan routes' inline budget (**x86-64 `words` −11.8 %**, its ASCII witness −11.7 %, `digits` −12.2 %, eight rows gaining, worst +4.8 % on `sc=Han` which pays on both ISAs), and a non-capturing group no longer hides the atom from the quantifier promotion (**`(?:a)+` 1.368 → 0.490 ns/B**, parity with `a+`, with atomic and capturing groups correctly refused). Three §g_perf figures were also retired for describing code that no longer runs — one of them asserting the INVERSE of the truth about `^(a+)+$`.

## v2026.8.6

8.6 (**the non-ASCII paths, and what a call costs before it scans anything**): **This document's worst ratio is fixed, and the fix is priced in two numbers rather than one.** `(?i)café` sat at **0.26×** against PCRE2-JIT; an icase fold whose members share one UTF-8 length and differ in exactly ONE byte (`é`/`É` are `C3 A9`/`C3 89`) is now emitted as fixed-width bytes instead of a code-point class, and it reads **0.84× on arm64** (1.286 → 0.404) and **0.89× on x86-64** (2.057 → 0.671). **The diagnosis that looked obvious was wrong**: case-insensitivity was never the cost — plain `café` used to be SLOWER than `(?i)café` — and the decomposition (literal 0.257 / explicit class 0.700 / icase 1.226 / alternation 3.405) is what identified the code-point-class emission as the step that mattered. Correctness is pinned by `exhaustive-compat` reading identically to the case before and after (3218434 cases, agree=3213886 both sides) and by a test whose NEGATIVE cases refuse the cross product two varying positions would admit. **The price is published twice on purpose:** gcc/x86-64 `words` **+16.8 %** in this harness, **+4.6 %** in a translation unit that includes only `real.hpp` — the shape a caller compiles — for the same −64 %; arm64 pays nothing (worst row +0.2 %). The gap is the harness's own unit, which compiles four regex engines together and was measured this train charging +27.7 % for a single added *data row*.

## v2026.8.5

8.5 (**an anchor is a mode, not a shape**): **The anchoring work is complete on both class routes, and both tables are re-measured on it.** `\A`/`^` is a MODE and `\Z`/`$` a LIMIT; the recognizers peel them and the routes honour them, so a pattern pinned to a position no longer falls to the general VM: **`^[a-z]+` 2.585 → 0.032 ns/B (81×), `[a-z]+$` 3.100 → 0.032 (94×), `^[a-z]+$` 0.573 → 0.043 (13×), `^\w+$` 1.450 → 0.066 (22×)** over 100 KB. The published `anchored ^[a-z]+$` row reads **REAL ahead of PCRE2-JIT on both ISAs** (1.35× arm64, 1.01× x86-64). **Collateral of the last step, measured:** the code-point half lands within +2.9 % on arm64 and mostly BETTER on x86-64 (ASCII witness −7.0 %), with `(?i)café` +5.9 % the one row that pays. **Three guards came from three different oracles**, none of them foreseen: the seam differential on `[a-z]+\b$`, the Python binding's fuzz against `re` on `\b(?>\w)$` (a wrong answer that 11 178 span comparisons had missed), and MSVC's `/WX` on a shadowed local that no local step asked about — `gcc-check` now passes `-Wshadow`, which is in neither `-Wall` nor `-Wextra`. A `\b`/`\B` wrap and an end anchor together are refused rather than combined: once the assertion is peeled, nothing downstream can re-derive it.

## v2026.8.4

8.4 (**the walk stopped returning to the dispatcher for every match**): **The walk now hands out BUFFERED spans, and it is the largest single move these tables have recorded.** Holding a class and its bytes fixed while varying only how often a match must be emitted showed the inner scan at ~2.2 ns/B against 7.6 for the same bytes emitted one code point at a time: **71 % of those rows was the per-match return** through `run()`'s dispatch, `fill_span_slots` and the iterator's re-entry. The walk fills a four-span buffer inside the route instead. **`words` 2.225 → 1.163 ns/B arm64 and 2.877 → 1.708 x86-64; `digits` 1.381 → 0.845 and 1.758 → 1.089; `[à-ÿ]+` 3.390 → 1.572 and 8.183 → 3.336; `\p{sc=Han}` 5.556 → 3.326 and 9.246 → 5.974; `\p{L}+` 4.075 → 2.880 and 6.144 → 4.269; `\w+` 3.010 → 2.295 and 5.236 → 3.686.** Against PCRE2-JIT that takes `words` from 1.05× to **2.00×** on arm64 and 1.89× to **3.76×** on x86-64, `digits` to **1.70×** / **3.33×**, and `[à-ÿ]+` and `\p{scx=Cyrl}` cross ahead for the first time (**1.31×**, **1.01×**). **What it costs, unhidden:** `fields` +3.4 % arm64 / +7.4 % x86-64 (`[^,]+` averages 6.3 bytes per match, so there is nothing to amortise), `date` +5.6 % x86-64 and `.` +7 % — routes the batch does not serve. **A fourth filler for `.` was written, verified and refused**: −58.8 % on its own row on arm64 and near-nothing on x86-64, where it took back most of the byte filler's win (`words` 1.708 → 3.155). Two fillers is what this translation unit absorbs.

## v2026.8.3

8.3 (**three accessors that answered the wrong question first**): **§A and §Unicode both re-stamped at `4efdd46`; the same one-line reordering was applied to three accessors, and how its cold half is PACKAGED decided each one differently.** All three asked two storage-mode questions — invariant for a whole walk — ahead of the row-key question that varies. `class_table` (once per match, top of a `run_*`) wants the cold half OUTLINED: bisected on executed conditional branches to the lazy per-class row fill, 3.00 branches per call over 1 270 040 calls, and the fix gives **`lookahead` 4.24 → 3.75 arm64 / 8.08 → 7.50 x86-64**, **`words` 2.35 → 2.22 / 3.39 → 2.87**, `digits` 1.44 → 1.38 / 1.89 → 1.79. The two code-point accessors (inside the per-code-point loop) are WRECKED by that same outlining — arm64 `[à-ÿ]+` **+39 %**, `\p{sc=Han}` +27 % — **while every deterministic counter called it an improvement** (Ir −10.2 %, Dr −4.1 %, Dw −11.2 %, Bc −9.6 %); and they are wrecked a second way by merely giving the cold half its own `always_inline` function, which costs gcc/x86-64 **10–17 % on §A rows that never touch it** (`digits` 1.813 → 2.114, 0.05 % spread) through the translation unit's inline budget. Folded in place, they move **every §Unicode row on arm64 (−1.4 % to −6.7 %) and seven of nine on x86-64 (to −14.2 % on `\p{N}+`, −8.2 % on `\w+` and `\p{L}+`)**, with the pure-ASCII witness in the same harness at −1.4 % / +2.0 %. Four §A rows are REAL's against PCRE2-JIT on both ISAs (`words` 1.05× arm64, `digits` 1.04×, `literal`, `hex` on x86-64) and `lookahead` reaches **0.97× on arm64**. **`fields` stays reclassified rather than chased**: its published regression does not reproduce in an isolated probe (HEAD 4.8 % FASTER than v2026.7.55 there) and the accessor work read −25 % in that probe against +1.0 % here — the row is not measuring the isolated scan, and the next move on it is a harness question.

## v2026.8.2

8.2 (**a first search that stopped paying for work it was about to throw away**): **§A is untouched
again, by construction** -- every change here is on the FIRST search a pattern performs, and §A's
throughput rows are steady-state ns/byte over a warmed regex. What moves is the first-use axis, which
§E.4 publishes rows for and which this train does not re-run against the rust crate; the figures below
are REAL's own, measured with one live regex per sample (200 kept alive, one first search each --
`docs/BENCHMARKS.md` now carries why a construct-and-destroy loop cannot measure this at all).
**The inner-literal route** decided its small-haystack guard AFTER building the byte program and
abandoned on the next line, so a short subject paid the whole expansion to learn it was too short:
`\w+@\w+` on 18 bytes **459.51 -> 0.71 us (647x)**, `(\w+)X(\w+)` on 13 bytes **466.91 -> 1.23
(380x)**, `(\w+)@(\w+)` on 80 bytes 458.60 -> 2.97 (154x), the ASCII gauge `[a-z]+@[a-z]+` 3.65 ->
2.16, and an **8 KB subject 1804.59 -> 1720.72 (1.05x)** -- that last row the control showing the
guard is size-gated rather than always-on. Confirmed without a clock: a build counter reads **400
UTF-8 trie builds across those 200 searches before and 0 after**. **The UTF-8 trie** is now built once
per CLASS rather than once per occurrence (**637.78 -> 476.48 us, -25 %**); sharing one cache across
the program's two expansions was written, measured and refused, since it helps `\d` by 8.2 % and
hurts `\w` by 2.0 %. **The compat regex_iterator** holds its walker instead of rebuilding a VM state
per advance: `++it` **2.2x** on `[a-z]+` and **4.5x** on a 24-branch alternation, `it++` 1.8x and
3.8x, with copy independence pinned against std::sregex_iterator. §A, §Unicode, §B, §E and
§multi-pattern carry their earlier figures unchanged.

## v2026.8.1

8.1 (**two routing decisions that were never measured, and a tool that finds the rest**): **§A again
cannot have moved, by construction.** Its table carries no capture-carrying pattern and its one
alternation row `the|fox|dog` has three branches -- below `ac_branch_floor`, which this train lowered
to 4 and not below, so the routing change cannot reach it. The two throughput changes land on shapes
this document does not publish and were measured on their own terms, both platforms, minimum across
alternating rounds against an untouched gauge. **Aho-Corasick below twelve branches**, where the
route was previously never taken at all: on 4000-byte subjects at 600 candidates per 1000 bytes,
**3.48x/4.34x at 4 branches (arm64/x86-64)**, 4.36/6.29 at 6, 5.28/8.90 at 8 and **8.03/10.23 at
11**; at 200 candidates 1.74/2.24; and at 50-100 candidates **0.99-1.01x arm64, 0.98-1.02x x86-64**,
which is the row that matters -- the sample window is now sized by the verdict rather than fixed, and
a fixed 256 had cost 2-5 % on exactly those sparse short alternations. The threshold in that region
is the measured MAXIMUM rather than the minimum, because below twelve the automaton was taken never,
so an early switch regresses where above twelve it could not; a static_assert holds the two constants
in the right order. **`regex_set` construction** stops building one full munch DFA per pattern to
discover a set too small to fuse: **214.4 -> 16.9 us at 20 patterns (12.7x)** and **446.9 -> 34.0 at
40 (13.1x)**, 11.2 us per pattern of pure waste on every set under 56. Not carried further: replacing
the probe for larger sets is worth 9-17 %, measured, because the fused subset construction dominates
and grows superlinearly (49 us per pattern at 56, 100 at 120). **The compat regex_iterator is
disclosed at 1.8-5.5x behind find_iter** and NOT fixed -- a fresh VM state per advance, where the
walker embeds 8232 bytes against that iterator's 320 and std::regex_iterator requires independent
copies; against the std::regex it replaces the same cases run 9.9x, 88.7x and 10.8x faster. §A,
§Unicode, §B, §E and §multi-pattern carry their earlier figures unchanged.

## v2026.8.0

8.0 (**routing decided by the subject, and allocation counts that stop depending on the pattern**):
**no published row was re-measured, and §A cannot have moved — by construction rather than by
inspection.** Its table carries no capture-carrying pattern, so the capture-pool reservation cannot
reach it, and its one alternation row `the|fox|dog` has three branches, below the Aho-Corasick
routing floor of twelve, so the density gate cannot reach it either. The two changes that DO carry
throughput land on shapes this document does not publish, and they were measured on their own terms,
on both platforms, as the minimum across five alternating rounds against an untouched gauge.
**Aho-Corasick routing** now selects on candidate density rather than branch count: a 24-branch
alternation over a 4000-byte subject with no match goes **12.92 → 2.21 µs on arm64 (5.85×)** and
**13.57 → 3.63 on x86-64 (3.74×)**, while match-dense and false-start subjects keep the automaton and
their existing figures (0.97× arm64, 1.00× x86-64). The rule is a PRODUCT — `(candidates per 1000
bytes) × branch_count` — because the crossover moves 3× between 12 and 24 branches; the constant is
the measured minimum across both platforms, since the route was taken unconditionally before and so
switching early cannot regress what it replaces. **The capture pool** stops growing by doubling
mid-search: 13 heap allocations become 5, and the allocation probe reads the general route at
**5/5/5 where it read 4/7/17**, which is the property worth the entry — allocation count is now a
function of the ROUTE and not of the pattern. Timing on capture-carrying shapes not in §A:
`((\w+))` **−20.0 % arm64 / −15.0 % x86-64**, `(\w+)@(\w+)` −7.0 / −6.2, `\w+@\w+` −9.4 / −6.1,
against `[a-z]+` as the untouched gauge at 0.0 % / +2.3 %. **§E's `captures/*` rows are where the
pool change would show in a published table and they were NOT re-run** — that is a criterion campaign
against the rust crate, on its own protocol, and this train does not claim it. §A, §Unicode, §B and
§multi-pattern carry their earlier figures unchanged.

## v2026.7.63

7.63 (**the storage lift — a pattern stops multiplying the engine into your translation unit, and the
Unicode gain it had been blocking**): §Unicode re-stamped on the devbox under g++ 13.3.0, min of five
interleaved runs, against untouched-row gauges. `\p{sc=Han}` **13.12 → 10.14 ns/B (−22.7 %)**,
`\p{scx=Cyrl}` 11.23 → 8.92 (−20.6 %), `\p{N}+` 7.32 → 6.08 (−17.0 %), mixed-script `\w+` 6.34 → 5.43
(−14.3 %); `\p{L}+` (−12.2 %) and `[à-ÿ]+` (−12.9 %) sit nearer this TU's floor and are recorded as
bounded, not claimed. arm64/clang unaffected **by construction** — the file is behind
`__GNUC__ && !__clang__` and `c++ -E` finds zero occurrences under clang. §A, §E, §B and §multi-pattern
carry their earlier figures; `bench_static` re-measured before/after the lift and reported as neutral on
the leg that can discriminate (arm64 median +0.7 % stat / +0.0 % dyn, every row within −3.0…+3.8) and as
**unable to discriminate** on x86-64 (−16.5…+17.7 % spread, nearly every large move a draw against its
gauge) rather than averaged into a claim.

**The row this train was about** was not in the tables at all. `static_regex`'s scratch type was nested in
a storage templated on the pattern's *value*, so every distinct pattern carried a private copy of every
Pike VM route — 82.4 % of the ~48 KB each added to `.text`. That exhausted GCC's
`--param inline-unit-growth` (3028 refused inline decisions in one benchmark TU, 2637 in `pike.hpp`), and
past the cap GCC declines in traversal order: a pattern that never executes a changed line moved **+220 %**,
and the degradation was **non-monotone in pattern count** — fine at 9 and 10, 3× off at 11 and 12, fine at
13 — so there was no safe count and no margin to watch. Fixed in three steps (dimension-keyed scratch,
table bases into `program_view`, power-of-two capacity tiering): `.text` per pattern ~45 KB → **7.7**
(x86-64) / 7.2 (arm64) for one shape and 50.9 → **10.8** / 9.3 for a unit of differing shapes, refusals in
a 32-pattern unit 19 735 → **1457**, and the 11/12/14-pattern timings 33.5 / 33.7 / 20.4 µs → **11.8 /
10.7 / 10.8**, flat across N = 0…32 on both platforms. Scratch footprint is the price: `[a-z]+` 1792 →
2328 bytes (×1.30), powers of two chosen over a coarse 32/64 ladder that would have cost ×3.8.

**Why the Unicode gain is in this train and not an earlier one:** dropping the cp-class inlining
attributes had been measured, valid and reproduced across three compilers, and refused — it cost
`(?i)cafe` on an ASCII no-match scan **+217 %**, a pattern with zero code-point classes that never enters
the loop. That was the budget defect, not the change. Re-measured after the lift on the same devbox the
collateral is gone and inverted: `(?i)cafe` −9.0 %, `\b\w+\b` −7.4 %, `\w+` −3.2 %, all with the `dyn`
gauge inside 1 %. **Nine approaches were refuted** on the way and are recorded so none is retried,
including PGO — which reproduces the identical cliff and exhausts the cap faster, because a profile
changes which inlines GCC *wants*, not the cap it runs into.

**Two measurement rules earned their place** and now sit in the methodology: read the rows a change
*cannot* reach before believing any delta (this caught three would-be findings, one a 19 % "gain" that was
the gauge moving with it), and measure x86-64 on the devbox rather than in Docker — worst within-arm
spread 1.045× against 1.98×, and on the deciding A/B the two disagreed about the **sign**.

**Not a benchmark row, but the user-visible fix of the train:** the Python binding's char-offset API was
quadratic. `finditer(...).start()` on a non-ASCII subject at 32 000 matches **633.2 → 10.1 ms**, and the
ratio to a `.group()`-only loop stops growing (1.5–1.6× across a 16× range, where it had gone 7× → 91×).
Pinned by shape, not by a wall-clock budget.

## v2026.7.62

7.62 (**documentation becomes enforceable, and a lazy build stops being paid for nothing**): §E.4 only —
five rows re-measured (`first_use/{email,word_bound,no_match}`, then `find/word_bound` and
`captures/word_bound` after the cp-class scan change), arm64, one criterion group at a time with both
engines in the same run, plus the `email` scan rows re-checked to confirm the cost went rather than moved
(`find/email` 46.52 µs against 46.20 recorded, `captures/email` 51.98 against 52.10). NOT under the
interleaved A/B protocol §E.4's main table uses, which is why those rows sit in their own table there.
§A, §Unicode, §B and §multi-pattern carry their earlier figures unchanged.

**The row this train was about:** `first_use/email` reaches **parity** with the `regex` crate (604.53 µs
against 601.81, CIs overlapping) from 2.64× behind. Five earlier passes had made the one-pass table
cheaper to build, 21.3 ms → 2.91; the sixth found that building it AT ALL was the cost. The capture-free
twin settled it — `\w+@\w+` has two slots and no capture for an extractor to fill, and cost the same
1487 µs — so `ensure_immutables` was building the extractor alongside the byte program and every route
needing the cheap half paid for the expensive one. Built only where captures are extracted through it:
first search **1490 → 573 µs on arm64**, **1958 → 813 on x86-64**, `\d{4}-\d{2}-\d{2}` 296 → 167.

## v2026.7.61

7.61 (**icase scan train — a folded ASCII class stops losing its route**): §E.4 re-stamped under the usual
interleaved A/B against v2026.7.60, same bench file both sides, one group at a time, machine idle: two
rounds per group and **four rounds of 5 s for `captures`**, which was load-bearing —
`captures/word_bound` read +3.1 % on two rounds and **−0.3 % on four**. No row regresses; every row other
than the four `icase_class` gains lands within ±1.6 %.

**The row this train was about.** 7.60 added an `icase_class` family because the suite had no
case-insensitive pattern at all, a gap two compile-cost defects had already come through; it read 3.37×
behind the `regex` crate immediately. The profile refuted the obvious explanation — `(?i)[a-z]+` was not on
the code-point scan but on the lazy DFA (`lazy_dfa::anchored_end`, 23.5 %). `find/icase_class` **631.8 →
252.1 µs (−60.1 %)**, 3.37× behind becomes **1.34×**; `captures/icase_class` **680.0 → 295.8 (−56.5 %)**,
3.66× → 1.57×; `compile/icase_class` −30.8 % (9.66× ahead → 14.01×); `first_use/icase_class` −39.6 %.

**What it was.** Under icase `[a-z]` gains the long s and the Kelvin sign, both MULTI-BYTE, so the class was
expanded to a byte-level alternation — one branch for the ASCII bitmap, one per UTF-8 sequence.
`(?i)[a-z]+` compiled to 8 byte classes and 18 instructions against 1 and 5 for `[a-z]+`, stopped being a
class loop and matched no route at all: two rare fold partners were costing the route. An ASCII bitmap with
a few non-ASCII members IS a code-point class and is now emitted as one. arm64 walk: `(?i)[a-z]+` −57.7 %,
`(?i)[a-zA-Z0-9_]+` −58.4 %; x86-64 instructions −58.2 % and −58.7 %; `[a-z]+`/`\w+`/`.`/`[^,]+`/`dog`/
`(?i)dog`/`\b\w+\b` at 0.0 % on arm64 and within 0.7 % on x86-64. One-pass eligibility improves as a side
effect: `(?i)[àé]+` goes from "byte-class conflict" to one-pass.

**`real::dfa` widened, not narrowed.** That entry point refused `klass_cp` outright, so the change above
would have made these patterns unconstructible there — a capability regression, not an acceptable price.
The refusal was a limit of the entry point: `dfa_flatten` now expands `klass_cp` through the same
`build_byte_program` the lazy DFA has always used, so text-mode `\d`/`\s`, Unicode properties, non-ASCII
classes and folded ASCII classes all build there now, where that API had never accepted one.

**A second-order cost the fuzzer found**, within the hour and through a path the change never touched:
`regex_set` builds one of these DFAs internally, so widening what the DFA ACCEPTS widened what it ATTEMPTS —
on `^\w` it spent 27.9 s where it had errored immediately. `max_dfa_states` bounds the RESULT of subset
construction and nothing bounded the WORK, which is superlinear: a folded ASCII class expands to 26
instructions and builds in 0.04 ms, `\d+` to 261 and 1.88 ms, and text-mode `\w+` to **3434 and 418 ms** —
thirteen times the size for two hundred and twenty times the time. `max_dfa_byte_program` = 512 sits between
them, and everything it refuses was already refused before `klass_cp` became expandable here. A repeat
multiplies the expansion (each occurrence gets its own trie), so `\d{2}` is already 517. Reproducer:
**27.91 s → 0.33 s**. Third cost defect this fuzzer has found, first one that was second-order.

**Disclosed, not chased:** `(\w+)@(\w+)` first use stays **2.64× behind**; `no_match` 2.09×/2.05× (a ratio
on 1.6 µs against 778 ns); `captures/word_bound` 1.82×; `find/literal` 1.57×; text-mode `\w` still declines
in `real::dfa`, by the cap and as before.

## v2026.7.60

7.60 (**route + measurement train — a counted repeat gets a route, and the suite learns to see it**):
§E.4 re-stamped under the usual interleaved A/B against v2026.7.59, same bench file both sides (which is
what lets this release's new families exist on both), one group at a time, two rounds, machine idle. No row
regresses by more than 3 %; every row other than the three `repeat` gains lands within ±2.2 %.

**The row this train was about did not exist before it.** The suite had eight families and not one counted
repeat — every family used `+`, `*` or a literal — and no case-insensitive pattern at all, while both
compile-cost defects this project has shipped lived in the icase path. Three families were added for the
second reason and immediately found something bigger, for the first: `find/repeat` (`\w{8}`) **2.24 ms →
123.9 µs (−94.5 %)**, 24.55× behind the `regex` crate becomes **1.35×**; `captures/repeat` 24.46× becomes
1.33×; `first_use/repeat` 93.5× ahead becomes 113.4× ahead.

**What it was.** `\w+` and `\w{8}` look alike and are not. Three shapes bracket the counted one and all
three were already fast (`[a-z]{8}` via fixed_shape, `\w{8,}` and `\w+` via the code-point class loop),
leaving exactly one combination unserved: a code-point class with a bounded count. It ran on the general
Pike VM and grew superlinearly — 247 µs for `\w+`, 499 for `\w{4}`, 2369 for `\w{8}`. The recognizer
declined that shape on purpose ("no MIN-only run to bound a search against"): the route bounded from BELOW
and an exact count needs it stopped from above. arm64 walk: `\w{8}` **−95.1 %**, `\w{4}` −57.0 %, `\d{4}`
−9.9 %, with `\w+`/`\w{8,}`/`[a-z]+`/`\b\w+\b` at 0.0 %; x86-64 `\w{8}` **−96.8 %** on the clock.

**Compile cost, continued from 7.59.** That release made each case fold cheaper without stopping it
happening once per repetition; a bounded repeat holds ONE class node and emits it k times, so `\w{500}`
folded `\w` five hundred times. A four-way cache on (class, fold mode, negated), holding the FINISHED class
so `coalesce_ranges` stops re-sorting too: `\w{64}` icase **3.84 → 0.128 ms (−96.7 %)**, `\w{256}` −20.1 %,
`\d{4}-\d{2}-\d{2}` −51.0 %, and `\w{500}a` **−97.0 %** in x86-64 instructions. `\w{k}a` now grows
sub-linearly in k where it was linear.

**A net, and a fuzzer refuted before it shipped.** `tests/frontend/test_compile_scaling.cpp` gates the
invariant both defects broke — compile cost scales with DISTINCT constructs, not repetitions — by comparing
the marginal cost of one more repetition under icase against the same marginal without it, which cancels
machine speed (`\d` 671.68× → 0.78×, `\w` 383.25× → 0.67×; bound 8×). Verified to FAIL on the tree the
fixes landed on. It replaces a compile-cost fuzzer whose design its own first measurement refuted: a
per-instruction budget cannot discriminate a 340× legitimate spread.

**Caught by the differential, not shipped:** the first version of the route lost `\w{4}\b` over
"abcdefghi" (matches at 5, returned nothing) because both retry loops advance by the whole run on failure —
right for a maximal run, wrong for a bounded one. The counted shape now declines a word boundary and the
code says what lifting it needs. Second release running where differencing a new route against the general
VM is what caught the defect.

**Disclosed, not chased:** `(?i)[a-z]+` scans **3.37× behind** the crate and 4.3× behind its own plain
form — under icase `[a-z]` gains the long s and Kelvin sign and stops being a pure ASCII class; `(?i)dog`
is 1.17× ahead, so it is the class widening and not the flag. Visible for the first time here.
`(\w+)@(\w+)` first use stays 2.64× behind. **Disagreeing measurements, both stated:** on gcc/x86-64 the
unbounded shapes read +2.3 % in INSTRUCTIONS (three branch orderings, all identical, so it is the code's
presence not its position) while the clock reads −0.2 % for `\w+` and +1.3 % for `\b\w+\b`, and arm64 reads
0.0 % for both.

## v2026.7.59

7.59 (**build train — the one-pass table stops being expensive to build**): §E.4 re-stamped under the
usual interleaved A/B protocol against v2026.7.58 (same bench file both sides, one group at a time,
machine idle), three rounds for `first_use`, two for `captures`/`compile`, and four rounds of 5 s for
`find` where two rows first read as regressions — `find/digits` +12.5 % and `find/word_bound` +4.2 % on
two rounds became **+0.8 %** and **+0.3 %** on four, per-round dispersion 0.2 %.

**The row this train was about.** `(\w+)@(\w+)` on first use had been disclosed and not chased in three
consecutive releases. `first_use/email` **2.78 → 1.58 ms (−43.2 %)**, 4.57× behind the `regex` crate
becomes **2.66×**. Direct harness, construct plus one search, arm64: `(\w+)@(\w+)` 2729.3 → 1538.2 µs
(**−43.6 %**), `(\d+)@(\d+)` 189.7 → 105.0 (**−44.6 %**); x86-64 instructions **−39.7 %**; allocations
601 486 → 236 566 blocks.

**What it was.** Four steps, each picked by profiling per function rather than by inspection — and the
profile contradicted the obvious guess twice. The largest cost was not the minimizer (19.1 %) but
`compute_lazy_alphabet` (26.5 %), whose byte-equivalence pass compared each byte against every open class,
re-walking all 475 predicates per pair; it now builds each byte's signature once and groups. Minimization
rows are split by what varies, so a round hashes 41 words instead of 162 and reads a packed run of assigned
targets instead of walking all 103 edges. The trie's sequences moved from a vector OF vectors (164 220
blocks averaging 4.6 bytes) into one pool, and its recursion stopped re-allocating per interval per level.
The alphabet transpose reads each class's bitmap instead of asking `test` for all 256 bytes.

**A compile-time defect the fuzzer found.** CI timed out at 10 s on `\w{500}accc…` with icase inside
`unicode_casefold` — pre-existing, verified by rebuilding the reproducer and measuring both commits
(12.10 s and 12.00 s). The linear-time guarantee covers MATCHING; nothing covered compile. The fold loop
scanned all 2940 table entries per class asking `any_of` over its 771 ranges, once per repetition. Now
range-driven with a binary-search seek: `\w{800}a` icase **305 → 51 ms (−83 %)**, the reproducer
**12.22 → 1.86 s**. Second train running where the fuzzer found a cost defect the bench suite structurally
cannot — it measures the patterns people write, not the patterns that are merely legal.

**What it costs.** Nothing measurable: every other criterion row within ±1.7 %, the walk unchanged on the
direct harness (`\w+` and `[a-z]+` 0.0 %, `\d+` −0.1 %), and patterns whose build is already cheap unmoved.
§A/§B/§Unicode/§multi-pattern carry their earlier figures.

**Refuted and reverted, recorded so it is not retried:** callgrind put a `memset` in `minimize` at 4.87 %
of the build; replacing it with a round stamp moved arm64 wall clock −0.2 % and x86-64 instructions −0.2 %.
`rep stos` is a known callgrind over-count and this is what that looks like.

## v2026.7.58

7.58 (**scan train — the membership accessors stop paying for themselves**): §E.4 re-stamped under the
same interleaved A/B protocol as 7.57 (same bench file both sides, one group at a time, machine idle),
two rounds for `find`/`compile`/`first_use` and four rounds of 5 s for `captures`, with `compile` carried
as a control and flat within 1.8 %. The extra `captures` rounds were not decoration: `captures/digits`
read +8.7 % on two rounds and +2.1 % on four, per-round dispersion falling to 0.1 %.

**The row this train was about.** `\b\w+\b` has been the largest tracked scan deficit for three releases,
disclosed and not chased each time; it moves here without touching `\b`. `find/word_bound` **317.8 →
261.6 µs (−17.7 %)**, 1.90× behind the `regex` crate becomes **1.56×**; `captures/word_bound` **453.2 →
318.5 (−29.7 %)**, 2.71× becomes **1.91×**. Direct harness, 64 KiB walk, arm64: `\d+` **−40.1 %**, `\w+`
and `\b\w+\b` **−17.7 %**.

**What it was.** Three things, all found by profiling per function — seven hypotheses were measured and
refuted first, and every fix came from a profile. `class_table` was emitted out of line at 6.23 M
instructions against 1.44 M inlined, because `derive_class_table` was split out without an attribute and
the compiler inlined its 256-iteration loop straight back (`noinline` there, `always_inline` on all three
accessors; clang was already inlining the byte one and neither code-point one, which is why the families
gain on opposite ISAs). The per-regex "row filled" flags were three allocations where one atomic bit word
does — a pattern with no code-point class still allocated two one-element vectors per construction
(first use `[a-z]+` +10.8 % → +4.4 %). And the per-`run()` program-identity compare, which is per MATCH on
a walk, is now folded away at compile time for callers that own their state (`StateBoundToProgram`,
defaulting to false so embedders keep it).

**What it costs.** `first_use/fields` +4.1 %, `/digits` +3.6 %, `/class` +3.3 %; `captures/class` +2.6 %,
`find/digits` +2.5 %, `captures/digits` +2.1 %; `[a-z]+` 64 KiB walk +2.8 % on arm64 — and **−3.7 %** in
x86-64 instructions, the one row where the two ISAs disagree in sign and the residual this train did not
close. `dog` is neutral (distributions overlap over 12 runs). Every other criterion row within ±1.7 %.
§A/§B/§Unicode/§multi-pattern carry their earlier figures; `static_regex` keeps 7.57's promise, all
eleven table rows and all ten inner-literal rows still favouring the compile-time regex (walk 1.00×–1.19×,
single shot 1.22×–72.76×).

**Two faults this train introduced and caught.** Three cache fields added mid-struct in `basic_pike_state`
shifted every field after them and cost `find/word_bound` **+32.3 %** then +28.4 % on a second round — on a
pattern reading none of them, and visible only through the C ABI, which crosses per match where the C++
harnesses do not. `pattern_hints` documents that exact fault and says "appended last" five times; the rule
was read after the consequence was measured, for the second release running. And a directory-wide `git add`
bumped `include/real/version.hpp` alone, which `preflight`'s version-check caught by failing two
consecutive commits — the guard working as designed.

**Disclosed, not chased:** `(\w+)@(\w+)` `first_use` is still **4.57× behind** (2.78 ms against 609 µs) —
the lazily built one-pass table is untouched here, as in 7.57 and 7.56.

## v2026.7.57

7.57 (**scan train — the inner-literal prefilter learns to place and confirm without an automaton**): §E.4
re-stamped, under an interleaved A/B protocol against v2026.7.56 with the same bench file on both sides
(one criterion group at a time, two rounds). The protocol is load-bearing: run inside the full suite
instead, `find/word_bound` reads 409 µs against 319 on the *same binary*, so that row's absolute value
depends on what else ran in the process.

**The row this train was about.** `(\w+)@(\w+)` was the one family where the `regex` crate led on both
operations. It is `class+ <literal> class+`, and the prefilter now places the match start by walking the
prefix class back from the `@` and confirms by walking the suffix class forward — two class walks in place
of a reverse DFA plus a one-pass extraction. `find/email` **132.4 → 46.2 µs (−65.1 %)**, `captures/email`
**140.8 → 52.1 (−63.0 %)**: 3.06× behind becomes **1.07×**. Every other criterion row moves by at most
2.8 %. Three shapes reached the same way, each recognised on the compiled program: one greedy class loop
before the literal (the reverse), the same after it (the confirm), and a loopless fixed sequence of
code-point atoms (`\d{4}-\d{2}-\d{2}`, placed by counting code points rather than bytes).

**What it did for the compile-time engine.** `static_regex` has no per-regex cache, so it could reach none
of the automaton-backed routes; every one of these three shapes needs no cache at all. With the VM scratch
also no longer zeroed on each `search()` (it is worst-case sized and nothing reads it before writing),
`make bench-static`'s eleven-row table and all ten inner-literal rows now favour the compile-time regex on
both walk and single-shot, where five rows lost before — `(\w+)@(\w+)` went **2252 → 39 µs** on the walk.

**Scan, separately.** The cp-class leftmost scan asked `width()` for a byte's worth of decode where one bit
was wanted: `\d+` −36.2 %, `\w+` −5.4 % (arm64, each pattern alone in its TU), and the same fault fixed in
the possessive loop, split by compiler because gcc measurably wants the old form there. `\b\w+\b` ends
2.5 % ahead of 7.56 through the crate and remains the largest scan deficit at 2.42× behind.

**Two faults this train introduced and caught.** MSVC rejects an object with an indeterminate subobject even
where nothing reads it, which broke every compile-time `static_assert` — CI-only, the local gate has no MSVC
leg. And the six new hint fields, added mid-struct next to the block they belong with, moved the class-loop
fast path's hot fields across a cache line: `dog` +30 % and `\b\w+\b` +27.7 % through the crate, on
patterns reading none of them. `pattern_hints` already documents that exact fault for its own `small_set`
array and says "appended last" five times; the rule was read after the consequence was measured.

**Disclosed, not chased:** `(\w+)@(\w+)` `first_use` is still **4.65× behind** (2.77 ms against 596 µs) —
the lazily built one-pass table is untouched here, and the scan rows above no longer need it.

## v2026.7.56

7.56 (**cold-path train — build and compile cost, no behaviour change**): §E.4 re-stamped and given two new
families the scan rows structurally could not see. `find`/`captures` build the pattern *outside* the timed
closure and criterion's warm-up absorbs any lazy build, so two costs lived in that gap: a quadratic Unicode
word-subset test (`\b\w+\b` **105.4 → 7.3 µs** at compile, 14.4× arm64 / 22× x86, `is_unicode_word_subset_cp_class`
was 94.73 % of that build and rescanned `word_ranges` from index 0 per step — now one merge of two sorted
lists), and the one-pass table for `(\w+)@(\w+)` (**21.3 → 2.91 ms** on first use over five passes: flat
hoisted scratch, sparse signature rows rewriting only the words a round changes, one interned class per
UTF-8 byte range plus the index memo that makes it pay, jump-chain resolution in the flood — which alone
made the flood land ON the minimal automaton, 660 nodes in and 660 out where it was 2508 in — and dropping a
duplicate Tier-A/Tier-B expansion). §E.4's `captures` rows moved too, from one runtime-length
`copy_from_slice` compiling to a `memcpy` CALL to carry two `usize`: `captures/class` **207.9 → 186.45 µs**
(1.10× behind → **parity**), `captures/digits` 61.0 → 54.72 (**REAL 1.39× ahead**), `captures/fields` 43.4 →
39.76 (**3.86× ahead**), `captures/literal` 27.7 → 18.98, `find/literal` 17.3 → 16.31, `captures/email` 143.6
→ 137.15. REAL leads **8 of 8** compile rows (2.65×–49.7×) and **7 of 8** first-use rows (1.77×–35.3×); the
exception is `(\w+)@(\w+)` first use, **4.94× behind** after −86.8 %, disclosed not chased — declining the
one-pass table instead was measured and rejected, it buys 3.3× on the scan with break-even near 900 KB.
§A/§B/§Unicode/§multi-pattern **untouched**: every change is on a cold path taken once per regex, outside
every scan loop, and the x86 instruction counts for `find_iter` over 64 KiB are identical to five decimals
(`[a-z]+` 45249867, `\b\w+\b` 79627271). Layout: no `pattern_hints` change. Known and disclosed: 7 of 9
dispatch routes still bill nothing to the deterministic work counter, so the linearity gate does not cover
them — all nine measure linear (3.77×–4.04× for 4× the bytes), so this is a missing net, not a defect.

## v2026.7.55

7.55 — **no re-stamp at the time.** The perf train shipped (one-search exact-literal route, two-byte NEON
literal prefilter, per-slot DFA ownership; see docs/release-notes/v2026.7.55.md for its measured per-commit
deltas), but `docs/BENCHMARKS.md` kept its `2026.7.51` figures and `make version-check` warned about the gap
— the notes named it and declined to hold a verified train for a documentary pass. §A/§Unicode/§E/§B/§E.4
were re-stamped afterwards, on both ISAs, as a separate doc-only train; the Version row above carries that
stamp. The same is true of 7.52–7.54: their Version rows all still read `2026.7.51`, so this log has no
entry for them by its own convention (it carries that row verbatim).

## v2026.7.51

7.51 (correctness/parity train — parser/compat/doc/harness only: ecma dialect gate, nullable-captured-repeat compat routing, without() uint16 widening fix, six RE2-parity syntax forms (all parse/compile-time), help/gate infra; adds no case and touches no measured §A/§E/§B/§Unicode hot path; layout: `pattern_hints` gains one bool (`nullable_captured_repeat`) — **verified sizeof/offsetof-neutral** (7.42 discipline): `sizeof(pattern_hints)` 200, `sizeof(program_view)` 376, `sizeof(dynamic_program)` 464, all three `offsetof` probed unchanged, v2026.7.50 vs this tip, both clang 16/arm64 and g++ 13/arm64 agree; not a re-stamp trigger).

## v2026.7.50

7.50 (**prefilter rare-discriminant**: URL filler-heavy sparse-URL ~**13× x86** (0.766→0.057 ns/B, rust order) / ~**14× arm64**, parity verified incl. near-miss `http:/`/`shttp://` = 0 false match; **neutral on URL-dense** (memory-bound); density abandon on dense `:`; class-loop plat / witness −7.7% incident; multi-literal/Teddy = follow-up).

## v2026.7.49

7.49 (**hardening pass**: **cp_hi cache-poisoning correctness** — thread-local sparse hi tables were keyed by a pointer into a dead program's `cp_ranges`; address reuse after destroy poisoned membership for high code points (`\p{}`, `[\w€]`, …); key is now content fingerprint stored once at intern so the 7.47 hot path is restored — A/B 2-ISA: `\p{L}+` CJK ~7.2 ns/B maintained, naive per-codepoint FNV was ~78×; ASCII class-loop flat. **GlobalReplace empty-abut** in RE2 drop-in (nullable `a*` no longer double-replaces abutting empty; found by true-libre2 differential). Infra: `fuzz-re2`, C ABI golden, euro pin/seed. **No new perf claim** (perf-fix restores 7.47 only).

## v2026.7.48

7.48 (correctness: **fixed_shape no longer peels a mid-pattern `\b` as a trail wrap** — under `flags::bytes` / compat, `\w{2}\bthe` had matched just `\w{2}` because `match_byte_klass_run` stopped at the assert and dropped the following literal; found by fuzz-compat crash-86573f5 on the tsan-core CI land; true trail `\b` still arms fixed_shape).

## v2026.7.47

7.47 (`\p{}` membership: **2-stage sparse `cp_hi_table`** extends the European page bitmap beyond U+07FF for CJK/astral; `thread_local`, `sizeof(pike_state)` unchanged; K=32 small scripts stay on bsearch; oracle 1.1M `\p{L}` **0 mismatch**) — dual-ISA A/B (binaries frozen): `\p{L}+` CJK **−35 % x86** (11.15→7.23) / **−26 % arm64** (6.89→5.12); `\w+` mixed **−17 % / −12 %**. **Collatéral disclosé**: `\p{N}+`/`sc=Han`/`scx=Cyrl` **+22–27 % arm64-ONLY** (codegen-luck front-end on page-only / single-cp paths under K=32 still on bsearch — **not the new 2-stage**; x86 A/B plat-to-faster) — accepted, leçon AC, not chased; D1b (program-level shared tables) on the shelf. ASCII class-loop **plat** both ISAs. §Unicode re-stamped this train (arm64 full table below + x86 REAL from superviseur A/B). §A re-stamped on `1936d05` (v7.46) before this train (docs-only): x86-64 class-loop drift since `a994ff9` (words +13 %, digits +20 %) characterized as `run_class_loop` codegen luck (competitors flat = machine noise), **not chased**; arm64/clang flat-to-slightly-faster.

## v2026.7.46

7.46 (issue #3 StatusLine: The x86-64 class-loop drifted since `a994ff9`: words `[a-z]+` 3.72→4.20 (+13 %), digits `[0-9]+` 2.08→2.51 (+20 %), alternation +11 %; fields/date/hex/literal +2–5 %; lookahead −4 %. **Characterized**: frozen competitor binaries (PCRE2/RE2) moved only ±5 % across sessions (PCRE2 digits even −5 %) = machine noise; the REAL surplus (~+10–15 % on words/digits) is **`run_class_loop` codegen drift** accumulated over 7.44→7.46 (each train touches the `pike.hpp` TU — same family as the `fields +39 %` of 7.43; front-end alignment, Valgrind byte-identical, **not chased**: irreducible, and a *wash* vs profile_runner corpora where 7.45 measured faster witness/fields). arm64/clang is **flat to slightly faster** vs the prior arm64 stamp (words/digits/fields +0–4 %, alternation −7 %, lookahead −9 % — no class-loop drift). Ratios below re-derived by `benchmarks/verify_bench_ratios.py`.

7.46 (issue #3 StatusLine: **inner-literal via pure-literal alternation flush** — `extract_inner_literal` no longer aborts on `info|error|warn`-style alts, so `req=` arms as the required downstream run; reverse-prefix covers alt+date) — dual-ISA: StatusLine **−58.7% x86** (4.19→1.73, ~2.4×) / **1.32× arm64**; **URL `https?://…` P0.2 measured and declined** (+65% x86 — memmem `://` loses to the strong v7.45 first-byte/`http`+DFA baseline; IL stays unarmed, route unchanged) — principle: IL wins when the baseline prefilter is *weak*, loses when it is *strong*. Non-reg: email-sparse flat (7.45 gain holds); **witness −8%** incident; wplus +1.3% / fields noise (accepted). Residual: StatusLine still far from rust (SIMD-substring = future arc, not this train).

## v2026.7.45

7.45 (issue #3 sparse-with-hits: **shared confirm-DFA cache** — fwd/rev/IL-prefix reverse DFAs process-wide per regex under a per-slot mutex, zero growth on `regex_immutables`, `confirm_at`→`anchored_end`, DFA search `noinline` out of `run()`) — dual-ISA: sparse `(\w+)@(\w+)` **−96.8%** x86 (~31×; arm64 same class, 0.69→0.05 ns/B) **reverses the gap vs rust** on that corpus; **witness `[a-z]+` −8%** and **fields `[^,]+` −41%** x86 collateral gains from the same amortization; **wplus `\w+` +4% residual x86-gcc only** (arm64 flat; front-end codegen-luck, accepted — well below 7.43's disclosed fields +39% / 7.36's emoji residual; layout isolation confirmed zero struct growth and noinline resolved witness +6%→−8%). Bonus on dongweigogo's #3 additions without targeting them: URL `https?://[^\s]+` **−30%**, StatusLine **−22%** (gap vs rust dented not closed — literal prefilter next). Not a full §A re-stamp of absolute cells — the Version row documents the train's own A/B.

## v2026.7.44

7.44 (issue #3 WordBoundary residual: the light peel — top-level `\b`/`\B` peeled so `\b\w+@\w+\b` keeps the `@` inner-literal route, and single-atom `\B\w`/`\B\d` arm the cp-class wrap; maximal `\B\w+` deliberately stays general, skip-run unsound) — on **both ISAs** a clean win with **zero regression**: x86-64/gcc A/B (superviseur, interleaved fixed binaries) measured `\B\w` **−82%** (~5.6× faster), arm64 ~4.2×; email `\b\w+@\w+\b` ~2× on match-bearing corpora (the no-match memmem-`@` short-circuit is faster still); existing peel `\b\w+\b` and `fields`/`wplus` flat within noise; **witness `[a-z]+` −10% incident gain** on x86 (no intentional hot-path change — pure absence of AC-style code-bloat in `pike.hpp`). The opposite of 7.43's disclosed `fields` +39% exception: this train documents gains, not a pending re-stamp. §A/§E re-stamped on `a994ff9` (post O2r-1b, the A2 fix, the P0 #2 icase fix + seed-128 tune, and the mono/multi cascade split), 2026-07-11; §Unicode's `.`/ascii-witness cells re-stamped on `258783b` (post wagon-4 strict-UTF-8 fix for `.`), 2026-07-11 — every section carrying a re-stamp note is now re-stamped within this same train, not a stale carry-over.

## v2026.7.43

7.43 (issue #3: the Aho-Corasick multi-literal alternation route, N≥12 branches — `real/engine/aho_corasick.hpp`) touches an EXISTING §A cell for the first time since 7.41's own disclosed exception: on x86-64/gcc specifically, `fields [^,]+` (a class-loop pattern, unrelated to alternation and never dispatched through the new route) regresses ~39% from a documented, isolated front-end loop-alignment codegen-luck effect — the AC engine's mere presence in `pike_vm::run()`'s translation unit, not a logic or memory-access change (Valgrind/callgrind confirmed byte-identical executed instructions and cache misses before/after; two targeted per-function alignment fixes were tried and reverted rather than trading the regression for a new one on a different class-loop pattern sharing the same inlined hot loop — see `run()`'s own doc comment in `pike.hpp` and the v2026.7.43 release notes for the full diagnostic trail). The `fields [^,]+` x86-64 figures below are therefore likely STALE as of this train and pending re-measurement — flagged here rather than silently carried over, same discipline as 7.41's own note below. arm64/clang is unaffected (confirmed both by the Valgrind/codegen analysis and this project's own interleaved A/B on this devbox). The suite's own alternation case (`alt the|fox|dog`, 3 literals) is itself unaffected: it stays structurally below the new N=12 routing threshold and continues through the pre-existing `run_alternation`/`small_set` path unchanged, byte-for-byte. The AC route's own gain (2.5-2.6× on a 15-literal alternation past the threshold, arm64, min-of-11 — not one of this suite's own numbered cases) is a disclosed, separate finding reported in the v2026.7.43 release notes, not part of this baseline.

## v2026.7.42

7.42 (compat reorganized under `real/compat/` + the RE2 drop-in, `real/compat/re2/` — `FullMatch`/`PartialMatch`/`Consume`/`FindAndConsume`/`Replace`/`GlobalReplace`/`QuoteMeta`/`Arg`/`Options`/`Set` — + `\C` the raw-byte escape, both `flags::bytes` and, completing the drop-in, `flags::allow_raw_byte` for byte-offset-native consumers like `real/compat/re2/`) adds no case to this suite and touches no §A/§E/§B/§Unicode/§multi-pattern measured code path — a structural include move, a new header-only zero-dep compat surface with no hot-path wiring of its own, and a raw-byte escape reusing the existing byte-klass opcode (zero new runtime wiring, confirmed via the meta-seam). `real::flags` widening `uint8_t`→`uint16_t` (out of bits for `allow_raw_byte`) is layout-neutral — `sizeof`/`offsetof` byte-identical on every downstream struct, including `pattern_hints` (no `flags` field at all) — and confirmed flat by its own interleaved-binaries A/B on both ISAs (arm64 + x86-64, `witness`/`fields`/`wplus`, all within noise); reported in the v2026.7.42 release notes, not a re-stamp trigger.

## v2026.7.41

7.41 (issue #3: the `{n,}` counted-quantifier fast path + the `small_set` cap 4→8 raise + its own layout-fix) adds no case to this suite; the EXISTING cases it measurably touches — the class_loop (`words [a-z]+`, `witness [a-z]+`) — shift ~5% FASTER (favorable: the figures below are now conservative, not a stale carry-over hiding a loss), not a re-stamp trigger for the suite as a whole — this is the first recent train to touch a measured §A cell at all; the prior trains below all disclosed a clean miss (see the v2026.7.41 release notes for the full finding). A dedicated §A re-measure arc will re-stamp properly; this note keeps the carry-over honest rather than silent in the meantime.

## v2026.7.40

7.40 (a Go binding, `bindings/go/` v0.1, cgo over the frozen C ABI — plus the ABI's own `(NULL,0)`-everywhere fix) adds no case to this suite and touches no §A/§E/§B/§Unicode/§multi-pattern measured code path — a new binding surface with its own cold-build numbers (~8s arm64 / ~15s x86-64 native), not this suite's concern, reported in the v2026.7.40 release notes, not a re-stamp trigger.

## v2026.7.39

7.39 (R4: C-ABI foundation — `real_match`/`real_find_iter_between`/`real_sub`, 14→20 functions — + Python `RegexSet` rewired onto the native fused `real::regex_set`) adds no case to this suite and touches no §A/§E/§B/§Unicode/§multi-pattern measured code path — bindings-frontier-only, zero engine changes; RegexSet's own before/after numbers (small-set ~28% faster, fused-eligible ~5-9×, shape-dependent) are a disclosed, separate finding reported in the v2026.7.39 release notes, not part of this baseline, not a re-stamp trigger.

## v2026.7.38

7.38 (possessive-capture-fix + R2 typed `class_ref`/byte-possessive-capture arming + R3 shape-envelope extraction) adds no case to this suite; the byte-possessive route (`a++`-style) and its now-armed captured variant are a disclosed, separate finding — not part of this baseline — measured directly by their own route-toggle A/B (arm64 + x86-64) and reported in the v2026.7.38 release notes, not a re-stamp trigger.

## v2026.7.37

7.37 (triage-fixes: 4 correctness bugs + a CPython oracle-bug filter) adds no case to this suite and touches no measured code path — a route-toggle A/B (auto vs forced-general) confirms D1-perf's own possessive-loop ratios are unaffected by the new B-1 window-edge / same-table guards (see the v2026.7.37 release notes), not a re-stamp trigger.

## v2026.7.36

7.36 (D1-perf Étage A, possessive fast-path routes) adds no case to this suite; the ONE existing case it measurably touches — "unicode . (emoji, one codepoint)" — is a disclosed, isolated ~5% arm64 residual (reversed to a gain on x86-64), not a re-stamp trigger for the suite as a whole (see the v2026.7.36 release notes for the full finding); the possessive-vs-greedy win itself is a separate, disclosed finding, not part of this baseline.

## v2026.7.35

7.35 (D1, possessive quantifiers / atomic groups) adds no case to this suite and touches no EXISTING measured code path — confirmed flat by its own interleaved-binaries A/B, not a re-stamp trigger.

## v2026.7.34

7.34 (the P1 `\b`/`\B`-junction fix) touches no measured code path — confirmed flat by its own dual-ISA A/B, not a re-stamp trigger. §multi-pattern unchanged (Stage-2 + Arc I/II + Arc B `\b` unlock)
