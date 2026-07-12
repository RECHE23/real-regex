// Possessive quantifiers (X*+/X++/X?+/X{n,m}+) and atomic groups (?>...) — D1, Tier 1 only
// (a bare atom, or one wrapped in exactly one capturing group). A general "Tier 1.5" for
// compound deterministic bodies was scoped OUT after verifying a genuine VM-architecture wall
// (see compiler.hpp's emit_possessive_repeat) — any compound body under repetition, or any
// alternation body even with no repetition, is a clean compile-time rejection instead. Every
// expected result below is transcribed from the D0 oracle matrix (scratchpad/d0-atomic-
// possessive/oracle_matrix.py, 46/46 probes verified against Python 3.14's live `re`), restricted
// to the probes Tier 1's amended scope actually covers; the rest became rejection tests.
#include <string_view>

#include <sciforge/test/framework.hpp>
#include "real/real.hpp"

using namespace std::string_view_literals;

TEST(possessive_star_plus_question_basic)
{
  EXPECT_EQ(real::regex("a*+").search("aaa").end(), 3U);
  EXPECT(!real::regex("a*+a").search("aaa"));          // classic no-giveback trap
  EXPECT(real::regex("a*a").search("aaa"));            // control: plain greedy DOES give back
  EXPECT_EQ(real::regex("a++").search("aaa").end(), 3U);
  EXPECT(!real::regex("a++a").search("aaa"));
  EXPECT(!real::regex("a?+a").search("a"));
  EXPECT(real::regex("a?a").search("a"));                // control
  EXPECT_EQ(real::regex("a*+").search("bbb").end(), 0U); // zero repetitions is a valid possessive match
}

TEST(possessive_bounded_counts)
{
  EXPECT_EQ(real::regex("a{2,4}+").search("aaaa").end(), 4U);
  // 5 a's available: possessive takes the max (4), no giveback -- but a 5th 'a' is still free.
  auto m = real::regex("a{2,4}+a").search("aaaaa");
  EXPECT(m);
  EXPECT_EQ(m.start(), 0U);
  EXPECT_EQ(m.end(), 5U);
  // Exactly 4 a's: possessive takes all 4, no giveback, trailing 'a' has nothing left.
  EXPECT(!real::regex("a{2,4}+a").search("aaaa"));
  EXPECT_EQ(real::regex("a{3}+").search("aaaa").end(), 3U); // exact count: nothing to give back anyway
}

TEST(possessive_on_class)
{
  auto m = real::regex(R"([^"]*+")").search("abc\"");
  EXPECT(m);
  EXPECT_EQ(m.end(), 4U);
  EXPECT(!real::regex(R"([^"]*+c)").search("abc\"")); // possessive eats 'abc' (c isn't a quote), trailing c starves
  EXPECT(real::regex(R"(\d++)").search("123abc"));
  EXPECT_EQ(real::regex(R"(\d++)").search("123abc").end(), 3U);
}

TEST(atomic_group_tier1_bodies)
{
  // (?>X*) desugars to X*+ regardless of the inner repeat's own flag (D0-2's detection-order
  // finding) -- (?>[^"]*) and (?>\d+), the dominant real-world shapes, must not be rejected as
  // unbounded.
  auto m = real::regex(R"re((?>[^"]*)")re").search("abc\"");
  EXPECT(m);
  EXPECT_EQ(m.end(), 4U);
  EXPECT_EQ(real::regex(R"((?>\d+))").search("123abc").end(), 3U);
  EXPECT(real::regex("(?>a*+)").search("aaa")); // already-possessive inner repeat: still Tier 1
}

TEST(atomic_group_vacuous_no_repeat)
{
  // No outer repeat at all: nothing to give back regardless of body shape, as long as it's
  // deterministic (split-free) -- compiled inline, safe even for a multi-instruction concat.
  EXPECT(real::regex("(?>ab)").fullmatch("ab"));
  EXPECT(real::regex("(?>)").fullmatch(""));
  EXPECT_EQ(real::regex("(?>)a").search("abc").end(), 1U);
  EXPECT(real::regex("(?>a)").fullmatch("a"));
}

TEST(possessive_captured_single_atom)
{
  // (a)*+ -- captures stay live (Tier 1 covers a bare atom wrapped in exactly one capturing
  // group); group(1) reflects the LAST successful iteration's span, matching re's own semantics.
  auto m = real::regex("(a)*+b").search("aaab");
  EXPECT(m);
  EXPECT_EQ(m.start(), 0U);
  EXPECT_EQ(m.end(), 4U);
  EXPECT_EQ(m[1], "a"sv);
  auto m2 = real::regex("(a){2,4}+").search("aaaa");
  EXPECT(m2);
  EXPECT_EQ(m2[1], "a"sv);
}

TEST(possessive_capture_not_corrupted_by_failed_final_attempt)
{
  // Regression probe for a bug found by hand-tracing during D1 design: a possessive loop always
  // attempts one more repetition after every success. If the capture's start slot were written
  // BEFORE the atom test (speculatively), the failed final attempt would overwrite it, leaving a
  // torn [failed-attempt-start, prior-end) span instead of the correct, LAST-SUCCESSFUL span.
  // (a)*+b on "aaab": the loop tries a 4th rep at position 3 ('b'), which fails -- group(1) must
  // still read the 3rd (successful) iteration's span [2,3), not [3,3).
  auto m = real::regex("(a)*+b").search("aaab");
  EXPECT(m);
  EXPECT_EQ(m.start(1), 2U);
  EXPECT_EQ(m.end(1), 3U);
}

TEST(branch_order_trap_needs_a_real_choice_which_tier1_never_has)
{
  // Tier 1 has no alternation at all (a single atom can't branch), so the classic "(?>ab|a)b"
  // branch-order trap simply doesn't arise for it -- it's a Tier 3 rejection instead (see
  // compound_body_and_alternation_rejected below), never a silently-wrong match.
  EXPECT_THROWS(real::regex("(?>ab|a)b"), real::regex_error);
}

TEST(compound_body_and_alternation_rejected)
{
  // A general "Tier 1.5" (repeated compound deterministic bodies) and genuine alternation
  // bodies (even with no repeat) are both a clean compile-time rejection in this train --
  // documented divergence from `re` (which accepts all of these), not a shaky implementation.
  EXPECT_THROWS(real::regex("(?:ab)*+"), real::regex_error);      // compound body, deterministic, repeated
  EXPECT_THROWS(real::regex("(?:a++)*+"), real::regex_error);     // nested possessive-of-group, still compound
  EXPECT_THROWS(real::regex("(?:a?+)*+"), real::regex_error);     // the empty-progress-hazard shape re handles; here, rejected outright
  EXPECT_THROWS(real::regex("(?>ab|a)"), real::regex_error);      // alternation, no outer repeat
  EXPECT_THROWS(real::regex("(?>a|ab)b"), real::regex_error);     // alternation, no outer repeat
  EXPECT_THROWS(real::regex("(a|b)*+"), real::regex_error);       // possessive quantifier over an alternation
  EXPECT_THROWS(real::regex("(ab)*+"), real::regex_error);        // possessive quantifier over a MULTI-atom captured group
  EXPECT_THROWS(real::regex("(?>(a))*+"), real::regex_error);     // possessive over an already-atomic group: still a repeat over a non-bare-atom body
}

TEST(possessive_inside_lookaround_rejected)
{
  // Unbounded possessive/atomic constructs inside a lookaround are ALREADY rejected by the
  // pre-existing, generic "unbounded lookaround" check (l_max_bytes doesn't know about Tier 1's
  // possessive semantics at all -- it just sees an ordinary node_kind::repeat with max == -1
  // and rejects on that basis alone, exactly as it would for an ordinary unbounded greedy
  // repeat). Confirmed by exception message, not just type -- these do NOT exercise the new
  // capture_free checks below; they exercise the pre-existing path, which is itself worth
  // pinning (both reasons lead to rejection, but via genuinely different code).
  {
    bool threw = false;
    try {
      real::regex r("(?=a*+)");
    } catch (const real::regex_error& e) {
      threw = true;
      EXPECT(std::string_view(e.what()).find("unbounded lookaround") != std::string_view::npos);
    }
    EXPECT(threw);
  }
  EXPECT_THROWS(real::regex("(?<=a*+)"), real::regex_error);
  EXPECT_THROWS(real::regex("(?=(?>a*))"), real::regex_error); // unbounded -> same pre-existing path

  // BOUNDED possessive/atomic constructs inside a lookaround pass the unbounded check (a{2,4}+'s
  // l_max_bytes is a finite 4, regardless of the possessive flag l_max_bytes doesn't consult) --
  // these are what actually reach and exercise the new capture_free checks in emit_possessive_
  // repeat (X{n,m}+ suffix) and emit_atomic_group (the (?>X{n,m}) desugaring), confirmed by the
  // exact, distinct message.
  const char* const bounded_in_lookaround_msg = "possessive/atomic quantifiers inside a lookaround are not supported yet";
  {
    bool threw = false;
    try {
      real::regex r("(?=a{2,4}+)");
    } catch (const real::regex_error& e) {
      threw = true;
      EXPECT(std::string_view(e.what()).find(bounded_in_lookaround_msg) != std::string_view::npos);
    }
    EXPECT(threw);
  }
  {
    bool threw = false;
    try {
      real::regex r("(?<=a{2,4}+)");
    } catch (const real::regex_error& e) {
      threw = true;
      EXPECT(std::string_view(e.what()).find(bounded_in_lookaround_msg) != std::string_view::npos);
    }
    EXPECT(threw);
  }
  {
    bool threw = false;
    try {
      real::regex r("(?=(?>a{2,4}))"); // (?>X{n,m}) desugaring -- emit_atomic_group's OWN capture_free check
    } catch (const real::regex_error& e) {
      threw = true;
      EXPECT(std::string_view(e.what()).find(bounded_in_lookaround_msg) != std::string_view::npos);
    }
    EXPECT(threw);
  }

  // (?>a), with no repeat at all, compiles vacuously inline (emit_atomic_group's shape 2) --
  // it never touches a Tier 1 opcode, so it stays perfectly safe inside a lookaround too. Found
  // by an initially-wrong test assertion here, corrected against the actual compiled behavior --
  // the boundary is precise: only a REPEATED Tier 1 construct is rejected, not all (?>...) syntax.
  EXPECT(real::regex("(?=(?>a))a").fullmatch("a"));
}

TEST(possessive_leftmost_per_attempt_independence)
{
  // A failed/committed atomic decision at one start offset carries no state into a different
  // start offset's independent attempt -- confirmed on the oracle's own constructed case:
  // position 0 commits branch-order (via possessive here) and fails; position 1's attempt is
  // wholly independent, tested via a possessive-quantifier-equivalent shape (Tier 1 has no
  // alternation, so this exercises the SAME independence property through a*+ instead).
  auto m = real::regex("a*+a").search("xaa");
  // position 0 ('x'): no 'a' to start with, possessive matches 0 reps, trailing 'a' needs 'x' -- fails.
  // position 1 ('aa'): a*+ possessively eats both a's, trailing 'a' has nothing left -- fails too.
  // position 2 ('a'): a*+ eats the one remaining 'a', trailing 'a' has nothing -- fails.
  EXPECT(!m);
  auto m2 = real::regex("a*+b").search("xaab");
  EXPECT(m2); // position 1: a*+ eats "aa", trailing b matches -- succeeds, demonstrating independence from position 0's dead end
  EXPECT_EQ(m2.start(), 1U);
  EXPECT_EQ(m2.end(), 4U);
}

TEST(possessive_orthogonal_to_flags)
{
  // possessive is orthogonal to icase/ascii/bytes -- the fold/table selection doesn't interact.
  EXPECT(real::regex("A*+", real::flags::icase).fullmatch("aaa"));
  EXPECT(real::regex("[a-z]*+", real::flags::bytes).fullmatch("abc"));
}

TEST(atomic_group_multiple_quantifier_suffix_still_rejected)
{
  // Possessive is mutually exclusive with lazy, and a possessive marker can't itself be
  // re-quantified -- matches Python re's own rejection of these exact shapes.
  EXPECT_THROWS(real::regex("a*+?"), real::regex_error);
  EXPECT_THROWS(real::regex("a*?+"), real::regex_error);
  EXPECT_THROWS(real::regex("a{2,3}+?"), real::regex_error);
}

TEST(atomic_group_unterminated_is_a_clean_parse_error)
{
  // (?> with no closing paren -- parse_atomic_group's own "missing ), unterminated subpattern"
  // path, distinct from the emission-time Tier 1/3 rejections tested elsewhere in this file.
  EXPECT_THROWS(real::regex("(?>a"), real::regex_error);
  EXPECT_THROWS(real::regex("(?>"), real::regex_error);
}

// --- regression: Tier 1 as an alternation branch losing priority to a same-round-convergent,
//     lower-priority sibling ----------------------------------------------------------------
//
// Found by the D1 differential fuzzer's own possessive-quantifier generator (test_
// differential_fuzz.py) before this shipped anywhere: `(\w){1,3}+|[a-z]` on "20bc" gave the
// RIGHT overall span for its second find_iter match (3,4) but the WRONG (unset) group(1) --
// silently attributing the match to the lower-priority, non-capturing branch2 instead of the
// higher-priority, capturing branch1, which had in fact won.
//
// Root cause: the original design decided a Tier 1 loop's "give up" transition in step(), one
// round after its mandatory copy consumed a byte. A single-leaf sibling branch2 (`[a-z]`)
// resolves in ONE round and reaches the shared post-alternation convergence point during the
// PREVIOUS round's closure -- chronologically before branch1's step()-time exit decision got a
// chance to compete for the same pc. Plain "first felt this generation wins" dedup has no
// notion of true priority once insertion order like this is violated.
//
// Fix: the match/no-match decision moved from step() into add_thread's closure -- evaluated AT
// INSERTION TIME, in the same priority-ordered pass as everything else (precedented by
// assert_lookaround, which already runs a whole sub-VM decision at closure time). A failing
// attempt's exit is now pushed onto the SAME closure stack immediately, at the exact priority
// position this thread's own insertion earned -- so a higher-priority branch1 always claims the
// shared convergence pc before a lower-priority branch2's closure gets there. See pike.hpp's
// add_thread, the byte_loop_possessive/klass_loop_possessive/klass_cp_loop_possessive cases.
TEST(possessive_alternation_priority_regression)
{
  const real::regex rx1(R"((\w){1,3}+|[a-z])");
  auto              matches = rx1.find_all("20bc");
  EXPECT_EQ(matches.size(), 2U);
  EXPECT_EQ(matches[0].start(), 0U);
  EXPECT_EQ(matches[0].end(), 3U);
  EXPECT_EQ(matches[0][1], "b"sv);
  EXPECT_EQ(matches[1].start(), 3U);
  EXPECT_EQ(matches[1].end(), 4U);
  EXPECT_EQ(matches[1][1], "c"sv); // the bug: this was unset (branch2 wrongly won)

  // Same shape, reversed branch order: the (non-capturing) simple class now has HIGHER
  // priority, so it should legitimately win over the (capturing) Tier 1 loop when both can
  // match -- confirming the fix respects true priority in BOTH directions, not just always
  // favoring Tier 1.
  const real::regex rx2(R"([a-z]|(\w){1,3}+)");
  auto              matches2 = rx2.find_all("20bc");
  EXPECT_EQ(matches2.size(), 2U);
  EXPECT_EQ(matches2[0].start(), 0U);
  EXPECT_EQ(matches2[0].end(), 3U); // [a-z] can't match '2'/'0', so branch2 (Tier 1) wins here
  EXPECT_EQ(matches2[0][1], "b"sv);
  EXPECT_EQ(matches2[1].start(), 3U);
  EXPECT_EQ(matches2[1].end(), 4U);
  EXPECT_EQ(matches2[1].start(1), real::npos); // branch1 ([a-z], no group) legitimately wins -- group 1 unset

  // Uncaptured Tier 1, same hazard shape, plus the class-based (not codepoint) opcode family.
  // 'z' IS a member of [a-z] (the range's own upper bound), so the possessive loop legitimately
  // wins BOTH times find_iter seeds a new attempt (0 and 3) -- two matches, not one.
  const real::regex rx3(R"([a-z]{1,3}+|z)");
  auto              matches3 = rx3.find_all("abcz");
  EXPECT_EQ(matches3.size(), 2U);
  EXPECT_EQ(matches3[0].start(), 0U);
  EXPECT_EQ(matches3[0].end(), 3U);
  EXPECT_EQ(matches3[1].start(), 3U);
  EXPECT_EQ(matches3[1].end(), 4U);

  // Byte-atom Tier 1 (not klass/klass_cp) as the higher-priority branch, still correct.
  const real::regex rx4(R"(a{1,3}+|z)");
  auto              matches4 = rx4.find_all("aaaz");
  EXPECT_EQ(matches4.size(), 2U);
  EXPECT_EQ(matches4[0].start(), 0U);
  EXPECT_EQ(matches4[0].end(), 3U);
  EXPECT_EQ(matches4[1].start(), 3U);
  EXPECT_EQ(matches4[1].end(), 4U);
}

// --- coverage top-up: the 3 opcodes x {miss-at-min, max-reached, min=0 exit, end-of-text
//     mid-run, anchor/\b interaction}, Tier 3 exact messages, parser shapes, is_deterministic's
//     no-outer-repeat branches (group / nested-possessive / exact-count), `.` as a Tier 1 atom --
//     each a real behavior probe, not filler (coverage-bump-735 fiche). ----------------------

TEST(possessive_miss_at_min_fails_for_all_three_opcode_families)
{
  // byte_loop_possessive: min=2, only 1 'a' available -- the mandatory copy itself fails (dies
  // outright, no exit path to offer -- min is required).
  EXPECT(!real::regex("a{2,4}+").search("a"));
  // klass_loop_possessive: min=2 on a byte/ASCII class, same shortfall.
  EXPECT(!real::regex("[a-z]{2,4}+", real::flags::bytes).search("a"));
  // klass_cp_loop_possessive: min=2 on a Unicode predicate class (\w), same shortfall.
  EXPECT(!real::regex("\\w{2,4}+").search("a"));
  // Captured variant: the mandatory copy's own save/emit_node/save dies the same way; no torn
  // capture is left behind because the thread is simply gone.
  EXPECT(!real::regex("(a){2,4}+").search("a"));
}

TEST(possessive_max_reached_stops_the_optional_tail_exactly_at_the_bound)
{
  // Exactly max repetitions available: the possessive loop must stop AT max, not read one past
  // it (an off-by-one here would either under-consume or run past the corpus).
  EXPECT_EQ(real::regex("a{2,4}+").search("aaaa").end(), 4U);
  EXPECT_EQ(real::regex("a{2,4}+").search("aaaaa").end(), 4U); // MORE than max available: still stops at 4
  EXPECT_EQ(real::regex("[a-z]{2,4}+", real::flags::bytes).search("aaaaa").end(), 4U);
  EXPECT_EQ(real::regex("\\w{2,4}+").search("aaaaa").end(), 4U);
  // Captured: group(1) reflects the LAST (4th) iteration specifically, not the 5th (never attempted).
  auto m = real::regex("(a){2,4}+").search("aaaaa");
  EXPECT(m);
  EXPECT_EQ(m.end(), 4U);
  EXPECT_EQ(m.start(1), 3U);
  EXPECT_EQ(m.end(1), 4U);
}

TEST(possessive_min_zero_exits_immediately_when_the_first_attempt_fails)
{
  // min=0: zero repetitions is a legitimate possessive match -- the FIRST attempt failing must
  // not be treated as an overall failure, for all three opcode families.
  EXPECT_EQ(real::regex("a*+").search("zzz").end(), 0U);
  EXPECT_EQ(real::regex("[a-z]*+", real::flags::bytes).search("999").end(), 0U);
  EXPECT_EQ(real::regex("\\w*+").search("!!!").end(), 0U);
  EXPECT_EQ(real::regex("a{0,3}+").search("zzz").end(), 0U);
}

TEST(possessive_stops_cleanly_at_end_of_text_mid_run)
{
  // The loop must stop at text_.size() without reading past it, for an unbounded tail landing
  // exactly on the corpus boundary (byte_loop_possessive) and a codepoint tail that could
  // otherwise attempt to decode past the end (klass_cp_loop_possessive).
  EXPECT_EQ(real::regex("a++").search("aaa").end(), 3U);
  EXPECT_EQ(real::regex("\\w++").search("aaa").end(), 3U);
  // Multi-byte codepoint run ending exactly at the corpus boundary: dc.length must not push the
  // decode past text_.size() on the last iteration.
  EXPECT_EQ(real::regex("\\w++").search("a\xc3\xa9").end(), 3U); // "aé" -- 1 ASCII + 1 two-byte codepoint
}

TEST(possessive_word_boundary_interaction)
{
  // \b/\B still evaluate correctly around a Tier 1 possessive run -- the possessive opcode's
  // own closure-time routing doesn't disturb the ordinary assert_position handling elsewhere
  // in the same program.
  EXPECT(real::regex("\\ba++\\b").fullmatch("aaa"));
  EXPECT(!real::regex("\\ba++\\b").fullmatch("aaab")); // \b fails right after the possessive run
  auto m = real::regex("\\ba{2,4}+\\b").search("xx aaa yy");
  EXPECT(m);
  EXPECT_EQ(m.start(), 3U);
  EXPECT_EQ(m.end(), 6U);
}

TEST(tier3_rejection_exact_messages)
{
  const char* const  compound_msg = "possessive/atomic over a compound body is not supported yet";
  const auto         throws_with_message = [](const char* pat, const char* msg) {
                                             bool threw = false;
                                             try {
                                               real::regex r(pat);
                                             } catch (const real::regex_error& e) {
                                               threw = true;
                                               EXPECT(std::string_view(e.what()).find(msg) != std::string_view::npos);
                                             }
                                             EXPECT(threw);
                                           };
  throws_with_message("(?:ab)*+", compound_msg);
  throws_with_message("(?>ab|a)", compound_msg);
  throws_with_message("(a|b)*+", compound_msg);
}

TEST(atomic_group_no_repeat_deterministic_compound_shapes)
{
  // emit_atomic_group's "shape 2" (no outer repeat, deterministic body) via is_deterministic's
  // own branches beyond the simple concat/empty cases already covered elsewhere:
  // - node_kind::group (an ordinary, non-atomic group wrapping something deterministic)
  // - node_kind::repeat with possessive == true (a NESTED, already-valid Tier 1 construct)
  // - node_kind::repeat, ordinary, exact count (min == max, ordinarily deterministic too)
  EXPECT(real::regex("(?>(?:a*+))").fullmatch("aaa"));  // group wrapping a nested possessive repeat
  EXPECT(real::regex("(?>(?:a{3}))").fullmatch("aaa")); // group wrapping an ordinary EXACT-count repeat
  // Control: a{2,3} (non-exact bounded, ordinary) genuinely emits a split -- emit_repeat's
  // "optional copies" loop covers the ONE extra rep beyond min with a real give-back-capable
  // branch point -- so is_deterministic's exact-count shortcut (min == max) does NOT apply here,
  // and wrapping it with no outer repeat correctly still hits Tier 3's rejection (real give-back
  // potential to protect against, not a false positive).
  EXPECT_THROWS(real::regex("(?>(?:a{2,3}))"), real::regex_error);
}

TEST(possessive_dot_atom_bytes_and_text_mode)
{
  // `.` as a Tier 1 atom -- the any-node branch of emit_tier1_atom_test, both the bytes-mode
  // (klass_loop_possessive) and text-mode (klass_cp_loop_possessive, with its 3-slot UTF-8
  // continuation chain) paths.
  EXPECT_EQ(real::regex(".*+", real::flags::bytes).search("abc").end(), 3U);
  EXPECT_EQ(real::regex(".*+").search("abc").end(), 3U);
  EXPECT_EQ(real::regex(".++").search("a\xc3\xa9y").end(), 4U); // ASCII + 2-byte codepoint + ASCII, text mode
  EXPECT(real::regex("(?>.+)").fullmatch("a\xc3\xa9y"));
  // dotall interaction: `.` possessive should also respect (?s:...) scoping like ordinary `.`.
  EXPECT(real::regex(".*+", real::flags::dotall).fullmatch("a\nb"));
  EXPECT(!real::regex(".*+").fullmatch("a\nb")); // no dotall: '.' possessively stops before '\n', trailing text left over -- fullmatch fails
}

TEST(parser_possessive_desugar_and_scoped_flags)
{
  // a?+ specifically (the ? possessive suffix, distinct from */++ already covered above).
  EXPECT(!real::regex("a?+a").search("a"));
  EXPECT_EQ(real::regex("a?+").search("a").end(), 1U);
  // (?>X*) desugars to the SAME compiled shape as X*+, regardless of the inner repeat's own
  // possessive flag (D0-2's detection-order finding) -- confirmed on a bounded {n,m} form too.
  EXPECT_EQ(real::regex("(?>a{2,4})").search("aaaaa").end(), 4U);
  // Scoped flags around a Tier 1 possessive construct: icase folds the atom test itself,
  // orthogonally to the possessive machinery around it.
  EXPECT(real::regex("(?i:a*+)b").fullmatch("AAAb"));
  EXPECT(real::regex("(?i:(?>a+))b").fullmatch("AAAb"));
}
