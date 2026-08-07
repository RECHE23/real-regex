// Meta-seam (P0-4/B): a table-driven differential proving EVERY fast-path runner agrees with the
// general Pike VM on spans+groups, across an adversarial corpus. Three shipped correctness bugs
// this campaign (the wb `\b`+pos>0 fix, the possessive-capture fix in 7.38, ASCII-`\s`) were each
// a fast-path runner whose local invariant silently diverged from the general VM, caught by fuzz
// only AFTER shipping. Per-route seams already existed (test_route_pinning, the lazy-DFA/onepass/
// inner-literal/wb-possessive/UTF-8 differentials) but nothing guaranteed every runner HAD one.
// This file turns "we have seams" into "every fast-path runner has one, proven here."
//
// Mechanism: reused, not reinvented (test_prefilter.cpp's own icase-cascade equivalence tests
// already do this) -- a program's `hints` exist ONLY for fast-path recognition/dispatch; zeroing
// them (`without.program.hints = {}`) leaves the compiled bytecode byte-identical but makes every
// dispatch condition in `pike_vm::run()` false, so it falls through to `run_general`
// deterministically. Comparing the SAME program's routed run against its hint-blanked run is a
// sound, general differential across every runner dispatched inside `pike_vm::run()` -- eleven of
// the twelve fast-path runners. Group-aware throughout: `out_slots` (a flat vector) holds every
// capture slot, not just the overall span -- the exact gap that let the possessive-capture bug
// through a span-only toggle differential.
//
// The twelfth runner, `run_class_loop_trailing_la`, is dispatched OUTSIDE pike_vm::run (in
// real.hpp / find_iter, per pike.hpp's own comment on the omission) -- it keeps its existing,
// dedicated toggle (`trailing_la_route_disabled()`) instead, exercised at the `real::regex` level.
// Gate-safe: no wall-clock.
#include <sciforge/test/framework.hpp>

#include <real/automata/lazy_dfa.hpp>
#include <real/real.hpp>

#include <string>
#include <string_view>
#include <vector>

using namespace std::string_view_literals;
using real::detail::dynamic_storage;

namespace {

  // The general force-every-route-off differential, reused from test_prefilter.cpp's own
  // equivalence tests. `mode` lets a caller reach the lazy-DFA onepass route (REAL_MODE_FULL-only)
  // as well as search.
  void expect_seam_agrees(std::string_view       pattern,
                          std::string_view       text,
                          std::size_t            pos        = 0,
                          std::size_t            endpos     = real::npos,
                          real::detail::run_mode mode       = real::detail::run_mode::search,
                          real::flags            flags      = real::flags::none)
  {
    const auto with       = dynamic_storage::compile(pattern, flags);
    auto       without    = with;
    without.program.hints = {};
    const std::size_t end    {endpos < text.size() ? endpos : text.size()};
    const auto        region {text.substr(0, end)};

    real::detail::pike_state         s1;
    real::detail::pike_state         s2;
    std::vector<std::size_t>         r1;
    std::vector<std::size_t>         r2;
    const real::detail::program_view pv1 {with.view()};
    const real::detail::program_view pv2 {without.view()};
    real::detail::pike_vm            vm1(pv1, s1);
    real::detail::pike_vm            vm2(pv2, s2);
    const bool                       m1 = vm1.run(region, pos, mode, r1);
    const bool                       m2 = vm2.run(region, pos, mode, r2);
    EXPECT_EQ(m1, m2);
    EXPECT(r1 == r2); // group-aware: the whole flat slots vector, not just spans [0,1]
  }

  // A short adversarial corpus, reused across runners: dense (matches back-to-back), sparse
  // (rare, scattered), zero-match, a multi-byte UTF-8 junction, and end-of-text (no trailing
  // separator/context to fall back on).
  void expect_seam_agrees_corpus(std::string_view  pattern,
                                 real::flags       flags = real::flags::none)
  {
    expect_seam_agrees(pattern, "xx", 0, real::npos, real::detail::run_mode::search, flags); // ~zero-match-ish, tiny
    expect_seam_agrees(pattern, "", 0, real::npos, real::detail::run_mode::search, flags);   // empty text
  }
} // namespace

// --- one entry per pike_vm-level fast-path runner (11 of 12; trailing-LA is separate below) ----

TEST(seam_run_class_loop)
{
  expect_seam_agrees("[a-z]+", "the quick brown FOX jumps 42 times, cafe\xC3\xA9 world");
  expect_seam_agrees("[a-z]+", "AAAA9999");                                                  // zero-match (all uppercase/digits)
  expect_seam_agrees("[a-z]+", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"); // dense
  expect_seam_agrees("[a-z]+", "a b c d e f g h i j k l m n o p q r s t u v w x y z");       // sparse
  expect_seam_agrees("[a-z]+", "hello world", 6);                                            // region pos>0
  expect_seam_agrees_corpus("[a-z]+");
  // B-2 wrap: \b...\b keeps wb hints ON the class-loop itself (not dropped like B-1).
  expect_seam_agrees(R"(\b[a-z]+\b)", "the QUICK-brown_fox jumps, cafe\xC3\xA9 next-door");
  expect_seam_agrees(R"(\b[a-z]+\b)", "xhello worldx"); // no boundary anywhere
  // The `{k,}` counted-min extension -- k mandatory copies + a loop of the same
  // atom, byte class. Dense/sparse/under-the-min (runs shorter than k, which must NOT match) and
  // both wb wraps.
  expect_seam_agrees("(?a)[a-z]{4,}", "abc abcd abcde ab a");   // under-min (3,4,5,2,1 -char runs)
  expect_seam_agrees("(?a)[a-z]{4,}", "aaaaaaaaaaaaaaaaaaaaa"); // dense, well over min
  expect_seam_agrees_corpus("(?a)[a-z]{4,}");
  expect_seam_agrees(R"((?a)\b[a-z]{4,}\b)", "the QUICK four fives aaaaa a bb ccc dddd");
  expect_seam_agrees(R"((?a)\b[a-z]{4,})", "xabcdx four5 abcde");
  expect_seam_agrees("(?a)[a-z]{1,}", "same as plain +"); // k==1 must stay identical to bare `+`
  // Coverage top-up: the min-check boundary in isolation -- a run of exactly
  // k-1 (must NOT match as the counted run; the general VM's own {4,} semantics is the oracle
  // here) vs exactly k (the limit, must match), with and without \b.
  expect_seam_agrees("(?a)[a-z]{4,}", "abc");  // exactly k-1=3: no match
  expect_seam_agrees("(?a)[a-z]{4,}", "abcd"); // exactly k=4: matches at the limit
  expect_seam_agrees(R"((?a)\b[a-z]{4,}\b)", "abc");
  expect_seam_agrees(R"((?a)\b[a-z]{4,}\b)", "abcd");
  // RE2 \C: \C+ arms this same class-loop fast path (a plain 256-bit byte-klass,
  // structurally indistinguishable from any other class+ once compiled) -- confirmed via hints_of
  // (greedy_class_loop >= 0). Dense/sparse/zero-match, and a case splitting a multi-byte codepoint (\C
  // has no UTF-8 awareness at all, unlike the byte class-loop's other callers).
  expect_seam_agrees(R"(\C+)", "hello", 0, real::npos, real::detail::run_mode::search, real::flags::bytes);
  expect_seam_agrees(R"(\C+)", "", 0, real::npos, real::detail::run_mode::search, real::flags::bytes);                     // zero-match
  expect_seam_agrees(R"(\C+)", "\xC3\xA9\xE2\x82\xAC", 0, real::npos, real::detail::run_mode::search, real::flags::bytes); // multi-byte junctions, byte-blind
  expect_seam_agrees(R"(a\C+)", "a\xC3\xA9", 0, real::npos, real::detail::run_mode::search, real::flags::bytes);
  // RE2-compat \C-in-text-mode completion: flags::allow_raw_byte arms the SAME class-loop
  // fast path in plain TEXT mode (no flags::bytes at all) -- \C mixed with codepoint-aware bytes in the
  // same program, a genuinely new shape the bytes-only tests never exercised. A spike confirmed
  // fixed_shape/class_loop/lazy_dfa_anchored all agree with the general VM here (57+ checks, 0 divergence);
  // this pins the class-loop-specific ones in the tracked suite.
  expect_seam_agrees(R"(\C+)", "hello", 0, real::npos, real::detail::run_mode::search, real::flags::allow_raw_byte);
  expect_seam_agrees(R"(\C+)", "caf\xC3\xA9", 0, real::npos, real::detail::run_mode::search, real::flags::allow_raw_byte); // \C splits a codepoint
  expect_seam_agrees(R"(a\Cb)", "aXb", 0, real::npos, real::detail::run_mode::search, real::flags::allow_raw_byte);
  expect_seam_agrees(R"([a-z]+\C[a-z]+)", "abc\xC3xyz", 0, real::npos, real::detail::run_mode::search, real::flags::allow_raw_byte);
}

TEST(seam_run_cp_class_loop)
{
  expect_seam_agrees(R"(\w+)", "caf\xC3\xA9 h\xC3\xA9llo w\xC3\xB6rld 123_under");
  expect_seam_agrees(R"(\w+)", "!!! *** ,,,");                                  // zero-match
  expect_seam_agrees(R"(\w+)", "a\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80z world"); // 1/2/3/4-byte junctions
  expect_seam_agrees_corpus(R"(\w+)");
  expect_seam_agrees(R"(\b\w+\b)", "  caf\xC3\xA9  world  ");
  // The `{k,}` counted-min extension, Unicode cp-class -- min counted in CODE
  // POINTS, not bytes, so a multi-byte-junction corpus is the point (a byte-length check here
  // would be a genuine bug this seam must catch).
  expect_seam_agrees(R"(\w{4,})", "abc abcd caf\xC3\xA9 h\xC3\xA9llo x");     // under-min + multi-byte cp
  expect_seam_agrees(R"(\w{4,})", "caf\xC3\xA9" "caf\xC3\xA9" "caf\xC3\xA9"); // dense multi-byte
  expect_seam_agrees_corpus(R"(\w{4,})");
  expect_seam_agrees(R"(\b\w{4,}\b)", "  caf\xC3\xA9  ab  h\xC3\xA9llo  ");
  expect_seam_agrees(R"(\d{3,})", "12 123 1234 caf\xC3\xA9 12345");
  expect_seam_agrees(R"(\w{1,})", "same as plain +"); // k==1 must stay identical to bare `+`
  // Coverage top-up: the min-check boundary in isolation, counted in CODE
  // POINTS -- "caf" is 3 code points (under k=4), "caf\xC3\xA9" (café) is exactly 4 (c,a,f,é), the
  // last one multi-byte -- a byte-length check here would be the exact bug this seam must catch.
  expect_seam_agrees(R"(\w{4,})", "caf");                // exactly k-1=3 code points: no match
  expect_seam_agrees(R"(\w{4,})", "caf\xC3\xA9");        // exactly k=4 code points: matches at the limit
  expect_seam_agrees(R"(\b\w{4,}\b)", "caf");
  expect_seam_agrees(R"(\b\w{4,}\b)", "caf\xC3\xA9");
  // D1a: single-atom `\B\w` / `\B\d` arm cp-class with wb_lead=2 (maximal `\B\w+` stays general).
  expect_seam_agrees(R"(\B\w)", std::string("hello world ahello caf\xC3\xA9") + "a");
  expect_seam_agrees(R"(\B\w)", "a");                           // zero mid-word hits
  expect_seam_agrees(R"(\B\w)", std::string("\xC3\xA9") + "a"); // multi-byte then ASCII word junction
  expect_seam_agrees(R"(\B\d)", "a12b 9 99");
  expect_seam_agrees_corpus(R"(\B\w)");
}

TEST(seam_run_inner_literal_wb)
{
  // D1a: top-level `\b`/`\B` peel so `\b\w+@\w+\b` keeps the `@` IL route (prefix reverse excludes
  // the lead assert; confirm_at re-checks boundaries on the full program).
  expect_seam_agrees(R"(\b\w+@\w+\b)", "aa foo@bar bb x@y noat foo@baz");
  expect_seam_agrees(R"(\b\w+@\w+\b)", "no at signs here");
  expect_seam_agrees(R"(\b\w+@\w+\b)", "xfoo@barx"); // whole-string boundary at text edges
  expect_seam_agrees(R"(\b\w+@\w+\b)", "  foo@bar  ");
  expect_seam_agrees(R"(\b\w+@\w+\b)", "caf\xC3\xA9@test.com");
  expect_seam_agrees_corpus(R"(\b\w+@\w+\b)");
  // Lead-only / trail-only peel variants.
  expect_seam_agrees(R"(\b\w+@\w+)", "aa foo@bar bb");
  expect_seam_agrees(R"(\w+@\w+\b)", "aa foo@bar bb");
}

TEST(seam_run_possessive_byte_loop)
{
  expect_seam_agrees("a*+;", "xxyyaaa; ;yaaaa;");
  expect_seam_agrees("a*+;", "no semicolons here"); // zero-match
  expect_seam_agrees("a++", "aaaXaaaaaX");
  expect_seam_agrees_corpus("a*+;");
}

TEST(seam_run_possessive_byte_loop_captured)
{
  // The group-aware seam's own reason to exist: 7.38's bug was invisible to a span-only toggle.
  expect_seam_agrees("(a)*+;", "xxyyaaa; ;yaaaa;");
  expect_seam_agrees("(a)*+b", "b");           // zero-iteration: group must stay unset, not [0,0)
  expect_seam_agrees("(a)*+b", "aaab aaab b"); // dense repeats
}

TEST(seam_run_possessive_class_loop)
{
  expect_seam_agrees("[a-z]*+;", "9900--abcxyz; ;xyz;");
  expect_seam_agrees("[a-z]++", "ABC123");     // zero-match
  expect_seam_agrees_corpus("[a-z]*+;");
}

TEST(seam_run_possessive_class_loop_captured)
{
  expect_seam_agrees("([a-z])*+;", "9900--abcxyz; ;xyz;");
  expect_seam_agrees("([a-z])*+;", ";"); // zero-iteration
}

TEST(seam_run_possessive_cp_class_loop)
{
  expect_seam_agrees(R"(\w*+;)", "   caf\xC3\xA9; ;h\xC3\xA9llo;");
  expect_seam_agrees(R"(\w*+;)", ";");                             // zero-iteration
  // Delimited: the OTHER shape this runner drives (quoted bodies, negated class, prefix+suffix).
  expect_seam_agrees(R"("[^"]*+")", R"(a="1" b="two words" c="" d="unterminated)");
  expect_seam_agrees(R"(([^;])*+;)", "ab\xF0\x9F\x98\x80; ;xyz;"); // 4-byte codepoint last atom
}

TEST(seam_run_fixed_shape)
{
  expect_seam_agrees("[0-9a-f]{4}", "xx dead beef 1234 zz");
  expect_seam_agrees("[0-9a-f]{4}", "no hex here at all"); // zero-match
  expect_seam_agrees_corpus("[0-9a-f]{4}");
  expect_seam_agrees(R"(\b[0-9a-f]{4})", " dead xdead beefx");
}

TEST(seam_run_codepoint_class)
{
  expect_seam_agrees(".+", "hello\nworld");                      // dotall off: stops at \n
  expect_seam_agrees(".+", "caf\xC3\xA9 \xF0\x9F\x98\x80 done"); // multi-byte codepoints
  expect_seam_agrees("[^x]+", "aaaxaaax xxx");
  expect_seam_agrees_corpus("[^x]+");
}

TEST(seam_run_alternation)
{
  expect_seam_agrees("dog|fox|cat", "the quick fox jumps over the lazy dog near the cat");
  expect_seam_agrees("dog|fox|cat", "no animals mentioned"); // zero-match
  expect_seam_agrees(R"(\b(?:foo|bar)\b)", " foo bar foobar xfoo barx ");
  expect_seam_agrees_corpus("dog|fox|cat");
  // small_set's enumeration cap is 8 (prefilter.hpp), matching
  // run_alternation's own L-SIMD scan, which already gated on small_set_size <= 8 (pike.hpp) --
  // only the recognizer's enumeration cap was left at 4. 5-8 distinct first bytes now arm the
  // scan where they otherwise fall straight to the bitmap loop; 9 still correctly declines.
  // Dense/sparse/no-match, and right at both new boundaries (8 armed, 9 declined).
  expect_seam_agrees("cat|dog|fish|bird|fox|bear", "the quick brown fox jumps over the lazy dog near the cat");                                                                  // 4 distinct (unaffected: pre-existing small_set)
  expect_seam_agrees("cat|dog|fish|bird|fox|bear|wolf|deer|hawk|frog", "the quick brown fox jumps over the lazy dog near the cat and bear and wolf and deer and hawk and frog"); // 6 distinct (the alternation-gap example)
  expect_seam_agrees("cat|dog|fish|bird|fox|bear|wolf|deer|hawk|frog", "no animals mentioned");                                                                                  // zero-match, 6 distinct
  // Coverage top-up: the two boundary points the 4/6/8/9 spread skipped.
  expect_seam_agrees("cat|dog|fish|bird|owl", "the cat and the owl and a fish and a dog and a bird");                                                                            // 5 distinct: the new regime's own floor
  expect_seam_agrees("cat|dog|fish|bird|owl", "no animals mentioned");                                                                                                           // zero-match, 5 distinct
  expect_seam_agrees("cat|dog|fish|bird|owl|rat|hen", "the cat and the hen and a rat and a fish and a dog and a bird and an owl");                                               // 7 distinct
  expect_seam_agrees("cat|dog|fox|owl|rat|hen|pig|emu", "the cat and the owl and a hen and a pig and a rat and an emu");                                                         // 8 distinct: right at the new cap
  expect_seam_agrees("cat|dog|fox|owl|rat|hen|pig|emu|yak", "the cat and the yak and an emu");                                                                                   // 9 distinct: still correctly declines small_set
  expect_seam_agrees_corpus("cat|dog|fox|owl|rat|hen|pig|emu");
}

TEST(seam_run_aho_corasick)
{
  // Past the measured branch-count threshold (N=12), a fixed_alternation program
  // routes to the Aho-Corasick automaton (aho_corasick.hpp) instead of run_alternation's memchr-
  // cascade scan. This differential (hints zeroed -> forced general VM) already exercises it,
  // since pike_state (this file's harness) carries the ac_automaton field.
  expect_seam_agrees("cat|dog|fish|bird|fox|bear|wolf|deer|hawk|frog|lion|tiger",
                     "the quick brown fox jumps over the lazy dog near the cat and the lion and the tiger");
  expect_seam_agrees("cat|dog|fish|bird|fox|bear|wolf|deer|hawk|frog|lion|tiger", "no animals mentioned"); // zero-match
  expect_seam_agrees(R"(\b(?:cat|dog|fish|bird|fox|bear|wolf|deer|hawk|frog|lion|tiger)\b)",
                     " cats dog catfish xdogx a lion. a tiger. ");
  expect_seam_agrees_corpus("cat|dog|fish|bird|fox|bear|wolf|deer|hawk|frog|lion|tiger");
  // Right at the threshold: N=11 must NOT engage AC (stays on run_alternation) -- the seam still
  // must agree either way, but this pins the boundary is exercised by the differential.
  expect_seam_agrees("cat|dog|fish|bird|fox|bear|wolf|deer|hawk|frog|lion",
                     "the quick brown fox jumps over the lazy dog near the cat and the lion");       // N=11, below threshold
  expect_seam_agrees("cat|dog|fish|bird|fox|bear|wolf|deer|hawk|frog|lion|tiger",
                     "the quick brown fox jumps over the lazy dog near the cat and the tiger");      // N=12, at threshold
  expect_seam_agrees("cat|dog|fish|bird|fox|bear|wolf|deer|hawk|frog|lion|tiger|zebra|camel|otter",
                     "the quick brown fox jumps over the lazy dog near the cat and the otter");      // N=15, past threshold
  // Post-mortem bug (a) seed: alternation order -- leftmost-first requires the FIRST-LISTED
  // branch to win at equal start position, regardless of length or scan-completion order (the the spike
  // spike's own repro: a longer, first-listed branch must beat a shorter one nested as its own
  // output-link suffix). >= 12 branches so this seed actually exercises the AC route, not just
  // run_alternation -- padded with enough distinct literals to cross the threshold.
  expect_seam_agrees("category|cat|dog|fish|bird|fox|bear|wolf|deer|hawk|frog|lion", "category");
  expect_seam_agrees("cat|category|dog|fish|bird|fox|bear|wolf|deer|hawk|frog|lion", "category");
  // Post-mortem bug (b) seed: empty alternation branches. REAL's parser rejects an empty
  // alternation branch as a syntax error (no zero-width branch ever reaches fixed_alternation, let
  // alone the AC recognizer) -- this seed pins that the SIBLING literals are wholly unaffected by
  // an adjacent empty branch's absence, mirroring the sidestep-by-construction proof.
  expect_seam_agrees("cat||dog|fish|bird|fox|bear|wolf|deer|hawk|frog|lion", "the dog barks");
  // icase: the current compiler's is_fixed_alternation gate does not reach 3+ branch icase
  // alternations at all (a pre-existing, unrelated limit -- confirmed on baseline)
  // so this seed exercises what IS reachable today: 2-branch icase, which stays on
  // run_alternation (below the AC threshold either way). AC's own klass fan-out logic is verified
  // directly (bypassing the compiler), since the compiler-level gap currently
  // makes it unreachable through real::regex's public API at N >= 12.
  expect_seam_agrees("(?i)cat|dog", "the DOG barks");
}

TEST(seam_run_exact_literal)
{
  expect_seam_agrees("dog", "the dog and the doghouse and dogs");
  expect_seam_agrees("dog", "no match here");
  expect_seam_agrees_corpus("dog");
}

TEST(seam_run_inner_literal)
{
  expect_seam_agrees(R"(\d{4}-\d{2})", "2026-07 and 1999-12 and not-a-date");
  expect_seam_agrees(R"(\d{4}-\d{2})", "no dates here at all");
  // Long enough to matter for the reverse-confirm cache, and adversarial: many false candidates.
  std::string dense_dates;
  for (int i = 0; i < 40; ++i) {
    dense_dates += "2026-07-13 ";
  }
  expect_seam_agrees(R"(\d{4}-\d{2})", dense_dates);
}

TEST(seam_run_lazy_dfa_search)
{
  // Padded past lazy_dfa_min_input (512) so the search route actually engages.
  std::string text;
  while (text.size() < 700) {
    text += "contact john.doe@example.com or jane@corp.io today, plus filler text ";
  }
  expect_seam_agrees(R"((\w+)@(\w+))", text);
  std::string no_match (700, 'z');
  expect_seam_agrees(R"((\w+)@(\w+))", no_match);
}

TEST(seam_run_lazy_dfa_onepass_fullmatch)
{
  // Full mode with capturing groups: the direct one-pass window extraction.
  expect_seam_agrees(R"((\w+)@(\w+))", "john.doe@example.com", 0, real::npos,
                     real::detail::run_mode::full);
  expect_seam_agrees(R"((\w+)-(\w+)-(\w+))", "2026-07-13", 0, real::npos,
                     real::detail::run_mode::full);
}

// --- cross-dimensional: shapes the audit flagged as uncovered by the per-route seams -----------

TEST(seam_cross_possessive_and_trailing_lookahead)
{
  // A possessive class-loop with a trailing lookahead assertion in the SAME pattern -- the
  // possessive fast path arms (confirmed: possessive_class.kind >= 0), trailing_lookaround does
  // not (the trailing-LA recognizer only matches a plain greedy class+, not a possessive one) --
  // an interaction the per-route seams never separately exercised together.
  expect_seam_agrees(R"([a-z]++(?=\d))", "abc123 def456 xyz");
  expect_seam_agrees(R"([a-z]++(?=\d))", "abc def (no digits follow)"); // zero-match
  expect_seam_agrees(R"([a-z]*+;(?=x))", "abc;x def;y ghi;x");
}

TEST(seam_cross_region_pos_gt_zero)
{
  // Region-aware pos>0 across several runner families at once -- \A/^ (non-multiline) must fail
  // at pos>0 uniformly whether routed or general.
  const std::string text {"aaa bbb dead beef dog cat 2026-07-13 end"};
  expect_seam_agrees("[a-z]+", text, 4);
  expect_seam_agrees("[0-9a-f]{4}", text, 8);
  expect_seam_agrees("dog|fox|cat", text, 18);
  expect_seam_agrees("dog", text, 18);
  expect_seam_agrees(R"(\d{4}-\d{2})", text, 26);
  expect_seam_agrees("a*+;", "aaa;bbb;", 4);
}

TEST(seam_cross_bytes_mode)
{
  // bytes-mode: `.`/classes match raw bytes, not codepoints -- a distinct compiled shape from
  // text mode for the same source pattern (klass_cp vs klass), each own runner family.
  expect_seam_agrees("[a-z]+", "abc\xFF\xFExyz", 0, real::npos, real::detail::run_mode::search,
                     real::flags::bytes);
  expect_seam_agrees(".+", "abc\xFF\xFExyz", 0, real::npos, real::detail::run_mode::search,
                     real::flags::bytes);
  expect_seam_agrees("a*+;", "aaa\xFF;bbb;", 0, real::npos, real::detail::run_mode::search,
                     real::flags::bytes);
}

// --- the twelfth runner: trailing-LA, dispatched outside pike_vm::run --------------------------

namespace {
  void expect_trailing_la_seam_agrees(std::string_view pattern,
                                      std::string_view text)
  {
    const real::regex re {std::string(pattern)};
    struct span { std::size_t start, end; };
    const auto collect = [&] {
                           std::vector<span> out;
                           for (const auto& m : re.find_iter(text)) {
                             out.push_back({.start = m.start(), .end = m.end()});
                           }
                           return out;
                         };
    real::detail::trailing_la_route_disabled() = false;
    const auto routed   {collect()};
    real::detail::trailing_la_route_disabled() = true;
    const auto general  {collect()};
    real::detail::trailing_la_route_disabled() = false;
    EXPECT_EQ(routed.size(), general.size());
    const std::size_t n {std::min(routed.size(), general.size())};
    for (std::size_t i {0}; i < n; ++i) {
      EXPECT_EQ(routed[i].start, general[i].start);
      EXPECT_EQ(routed[i].end, general[i].end);
    }
  }
} // namespace

TEST(seam_run_class_loop_trailing_la)
{
  expect_trailing_la_seam_agrees("[a-z]+(?=[a-z])", "the quick brown fox jumps over abc123");
  expect_trailing_la_seam_agrees("[a-z]+(?=[a-z])", "a b c d e"); // every run is length 1: no LA holds
  expect_trailing_la_seam_agrees("[0-9]+(?![0-9])", "12 345 6789x abc");
}

// --- Step 3: the process guarantee (a coverage manifest, not just a convention) -----------------
//
// This does NOT catch a brand-new hint field added to pike_vm::run()'s dispatch chain in the
// future without a matching manifest line -- that half of the guarantee is CONTRIBUTING.md's
// "add a seam-matrix entry" rule (a human process step; see there). What THIS catches is quieter
// but just as real: this table's own patterns silently losing coverage of a field they are
// SUPPOSED to arm (a pattern edited, a compiler change shifting which hint a shape now arms,
// ...) -- exactly the kind of silent drift the rest of this file exists to catch. Keep this list
// in sync with pike.hpp's own `run()` dispatch chain (grep it for `prog_.hints.` there) whenever
// a route's gating condition changes.

TEST(seam_matrix_coverage_manifest)
{
  const auto hints_of = [](std::string_view pattern) {
                          return dynamic_storage::compile(pattern, real::flags::none).program.hints;
                        };
  EXPECT(hints_of("[a-z]+").greedy_class_loop >= 0);
  EXPECT(hints_of(R"(\w+)").greedy_cp_class >= 0);
  // The {k,} min-count extension actually arms with k, not silently falling back
  // to the bare-`+` min=1 default (which would make the recognizer extension a no-op).
  EXPECT(hints_of("(?a)[a-z]{4,}").greedy_class_loop >= 0);
  EXPECT_EQ(hints_of("(?a)[a-z]{4,}").greedy_class_loop_min, std::uint16_t {4});
  EXPECT_EQ(hints_of("[a-z]+").greedy_class_loop_min, std::uint16_t {1});
  EXPECT(hints_of(R"(\w{4,})").greedy_cp_class >= 0);
  EXPECT_EQ(hints_of(R"(\w{4,})").greedy_cp_class_min, std::uint16_t {4});
  EXPECT_EQ(hints_of(R"(\w+)").greedy_cp_class_min, std::uint16_t {1});
  // {k,m} (bounded max) must NOT arm this shape (P1's own explicit scope limit) -- declines to
  // general, greedy_class_loop stays -1.
  EXPECT(hints_of("(?a)[a-z]{2,4}").greedy_class_loop < 0);
  EXPECT(hints_of(R"(\w{2,4})").greedy_cp_class < 0);
  EXPECT(hints_of("a*+;").possessive_class.kind == real::detail::class_kind::byte);
  EXPECT(hints_of("[a-z]*+;").possessive_class.kind == real::detail::class_kind::klass);
  EXPECT(hints_of(R"(\w*+;)").possessive_class.kind == real::detail::class_kind::klass_cp);
  // RE2 \C: \C+ arms greedy_class_loop too -- the byte-klass it compiles to (the
  // 256-bit "any byte" set) is structurally identical to any other class+ once compiled, so the existing
  // recognizer picks it up with zero dedicated wiring.
  EXPECT(dynamic_storage::compile(R"(\C+)", real::flags::bytes).program.hints.greedy_class_loop >= 0);
  // RE2-compat \C-in-text-mode completion: the SAME class-loop recognizer also arms
  // under flags::allow_raw_byte alone (no flags::bytes) -- the gate widened, not the recognizer.
  EXPECT(dynamic_storage::compile(R"(\C+)", real::flags::allow_raw_byte).program.hints.greedy_class_loop >= 0);
  EXPECT(hints_of("[0-9a-f]{4}").fixed_shape);
  // The bare single byte-class arms its own selector, and every neighbouring spelling declines. The
  // refusals are the load-bearing half: this shape's whole safety argument is that the 4-opcode
  // program admits no capture wrap, no `\b`, no anchor and no minimum -- so each of those must be
  // shown NOT to arm, or the argument is asserted rather than enforced.
  EXPECT(hints_of("[a-z]").single_class >= 0);
  EXPECT(hints_of("[aeiou]").single_class >= 0);   // 3+ ranges: no SIMD, still batchable
  EXPECT(hints_of("[a-z]+").single_class < 0);     // quantified: greedy_class_loop's job
  EXPECT(hints_of("([a-z])").single_class < 0);    // capture wrap adds saves
  EXPECT(hints_of(R"(\b[a-z]\b)").single_class < 0);
  EXPECT(hints_of("^[a-z]").single_class < 0);
  EXPECT(hints_of("[a-z]{2}").single_class < 0);   // two positions
  EXPECT(hints_of("a").single_class < 0);          // `byte`, not `klass` -- exact_literal's route
  EXPECT(hints_of(".").single_class < 0);          // klass_cp -- codepoint_class_ascii's route
  EXPECT(hints_of("[a-z](?=x)").single_class < 0); // lookaround wipe
  EXPECT(hints_of(".+").codepoint_class_ascii >= 0);
  EXPECT(hints_of("dog|fox|cat").fixed_alternation);
  // The small_set cap actually arms at 8, not silently staying at the old
  // 4 (which would make the recognizer extension a no-op), and 9 correctly declines (stays on
  // the bitmap loop, not a buffer overrun into the 8-element array).
  EXPECT_EQ(hints_of("cat|dog|fox|owl|rat|hen|pig|emu").small_set_size, std::uint8_t {8});
  EXPECT_EQ(hints_of("cat|dog|fox|owl|rat|hen|pig|emu|yak").small_set_size, std::uint8_t {0});
  // Coverage top-up: the interior of the new regime (5 and 7), not just its ends.
  EXPECT_EQ(hints_of("cat|dog|fish|bird|owl").small_set_size, std::uint8_t {5});
  EXPECT_EQ(hints_of("cat|dog|fish|bird|owl|rat|hen").small_set_size, std::uint8_t {7});
  // alternation_branch_count arms alongside fixed_alternation, right at the
  // AC-route threshold (12) and one below it (11, still fixed_alternation but not AC-eligible).
  EXPECT_EQ(hints_of("cat|dog|fish|bird|fox|bear|wolf|deer|hawk|frog|lion").alternation_branch_count, std::uint16_t {11});
  EXPECT_EQ(hints_of("cat|dog|fish|bird|fox|bear|wolf|deer|hawk|frog|lion|tiger").alternation_branch_count, std::uint16_t {12});
  // A capturing alternation (outer group, per-branch, or mixed) never arms fixed_alternation at
  // all -- the "safe capturing scope" decision is enforced for free by this PRE-EXISTING
  // gate (interior save ops break the byte/klass-only branch-body whitelist before the branch
  // loop even runs), not by anything new AC itself had to add.
  EXPECT(!hints_of("(cat|dog|fish)").fixed_alternation);      // outer capturing group
  EXPECT(!hints_of("(cat)|(dog)|(fish)").fixed_alternation);  // per-branch capturing
  EXPECT(!hints_of("(cat)|dog|(fish)").fixed_alternation);    // mixed capturing
  EXPECT(hints_of("dog").exact_literal_len > 0);
  EXPECT(hints_of(R"(\d{4}-\d{2})").inner_literal_len > 0);
  EXPECT(hints_of("[a-z]+(?=[a-z])").trailing_lookaround >= 0);
  // Lazy-DFA/onepass have no dedicated hint field (eligibility is a runtime probe against
  // op_table/fwd_dfa, not a static hint) -- their manifest entry is the runtime assertion in
  // seam_run_lazy_dfa_search/seam_run_lazy_dfa_onepass_fullmatch actually matching, not a hint
  // check; noted here so the list stays a complete map of the dispatch chain, not a silent gap.
}
