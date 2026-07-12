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
  // The lookaround sub-VM's own hand-written dispatch (pike.hpp's lookahead_matches /
  // sub_fullmatch_window / sub_add_thread) hard-assumes only byte/klass/klass_cp ever appear in
  // a sub-region; klass_cp_loop_possessive there would silently misread its arg16 against the
  // wrong class table. A hard compile-time reject, not an attempt to thread Tier 1 through three
  // separate hand-written dispatchers.
  EXPECT_THROWS(real::regex("(?=a*+)"), real::regex_error);
  EXPECT_THROWS(real::regex("(?<=a*+)"), real::regex_error);
  EXPECT_THROWS(real::regex("(?=(?>a*))"), real::regex_error); // (?>a*) desugars to Tier 1 -- still rejected inside a lookaround
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
