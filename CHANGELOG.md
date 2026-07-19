# Changelog

Per-train benchmark-impact log: what each release train measurably touched (or explicitly did not touch) in `docs/BENCHMARKS.md`'s §A/§E/§B/§Unicode/§multi-pattern sections, carried verbatim from that file's Version row. This is not the release notes — for the complete per-release description of features, fixes, and breaking changes, see `docs/release-notes/` and the GitHub Releases page.

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
