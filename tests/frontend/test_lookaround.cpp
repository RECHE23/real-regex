// Bounded lookahead (?=...) / (?!...) — linear-time, ReDoS-safe (COMMIT 1; lookbehind is
// COMMIT 2). Engine-level tests on real::regex (the dynamic path that carries lookarounds).
// Python re-parity lives in python/tests; here we pin the rejection of unbounded / nested /
// over-long sub-patterns and the capture-isolation of the sub-VM. Subjects are ASCII so the
// byte/char distinction does not intrude.
#include <string_view>

#include <sciforge/test/framework.hpp>
#include "real/automata/lazy_dfa.hpp" // trailing_la_route_disabled
#include "real/real.hpp"

using namespace std::string_view_literals;

TEST(lookahead_positive_leading_assertion)
{
  // (?=\d) requires a digit at the position; \w+ then consumes the run.
  const real::regex rx("(?=\\d)\\w+");
  const auto        m {rx.search("abc 123 xyz"sv)};
  EXPECT(m);
  EXPECT_EQ(m[0], "123"sv);
  EXPECT_EQ(m.start(), 4U);
}

TEST(lookahead_positive_trailing_assertion)
{
  const real::regex rx("\\d+(?=px)"); // a number immediately followed by "px"
  EXPECT_EQ(rx.search("10px 20em")[0], "10"sv);
  EXPECT(!rx.search("20em"));
}

TEST(lookahead_negative)
{
  const real::regex rx("foo(?!bar)");
  EXPECT_EQ(rx.search("foobaz")[0], "foo"sv); // not followed by "bar"
  EXPECT(!rx.search("foobar"));               // the only "foo" is followed by "bar"
}

TEST(lookahead_negative_then_consume)
{
  const real::regex rx("q(?!u)\\w"); // a 'q' not followed by 'u', then one more word char
  EXPECT_EQ(rx.search("qu qa qx")[0], "qa"sv);
}

TEST(lookahead_assertion_inside_sub)
{
  // A \b inside the lookahead exercises assert_position in the sub-VM closure.
  const real::regex rx("(?=\\bfoo)\\w+");
  EXPECT_EQ(rx.search("a foo bar")[0], "foo"sv);
}

// --- L1 peephole correctness traps: single-width assertion bodies, pinned against the re oracle BEFORE the
//     compile-time peephole exists. The sub-VM path gets each right today; the L1 direct-test opcode must too.
TEST(l1_trap_negation_is_over_the_result_not_the_class)
{
  // (?=[a-z]) at end-of-text is FALSE (no char to test); (?![a-z]) at end-of-text is TRUE (the body cannot
  // match, so its negation holds). A peephole that negated the CLASS instead of the assertion RESULT flips both.
  EXPECT_EQ(real::regex("[a-z]+(?=[a-z])").search("abc")[0], "ab"sv);  // lookahead fails at end -> run stops short
  EXPECT_EQ(real::regex("[a-z]+(?![a-z])").search("abc")[0], "abc"sv); // negative holds at end -> whole run
  EXPECT(!real::regex("a(?=b)").search("a"sv));                        // (?=b) with no next byte -> no match
  EXPECT(real::regex("a(?!b)").search("a"sv));                         // (?!b) with no next byte -> match
}

TEST(l1_trap_lookbehind_at_start_of_text)
{
  // (?<=x) at position 0 is FALSE (nothing behind); (?<!x) at position 0 is TRUE.
  EXPECT(!real::regex("(?<=x)y").search("y"sv));                 // start of text -> behind fails
  EXPECT_EQ(real::regex("(?<!x)y").search("y"sv)[0], "y"sv);     // start of text -> negative behind holds
  EXPECT_EQ(real::regex("(?<=x)y").search("ay xy").start(), 4U); // only the 'y' preceded by 'x'
  EXPECT_EQ(real::regex("(?<!x)y").search("xy ay").start(), 4U); // only the 'y' NOT preceded by 'x'
}

TEST(lookahead_does_not_corrupt_main_captures)
{
  // Isolation (behavioral): evaluating the lookahead must not disturb the main thread's
  // captures. (a+)(?=b) on "aaab": group 0 and group 1 are both "aaa" — they would be
  // wrong if the sub-VM had touched the main scratch (state_).
  const real::regex rx("(a+)(?=b)");
  const auto        m {rx.search("aaab")};
  EXPECT(m);
  EXPECT_EQ(m[0], "aaa"sv);
  EXPECT_EQ(m[1], "aaa"sv);
}

TEST(lookahead_two_in_sequence_no_residual_state)
{
  // Two lookaheads back to back: the reused sub-scratch must be reset between evals.
  const real::regex rx(R"((?=\d)(?=\w)\w+)");
  EXPECT_EQ(rx.search("ab 9z")[0], "9z"sv);
}

TEST(lookahead_bounded_repeat_is_allowed)
{
  // A bounded repeat inside the lookahead is fine (L_max = 3 bytes, under the cap).
  const real::regex rx("(?=\\d{3})\\d+");
  EXPECT_EQ(rx.search("ab 12 345 cd")[0], "345"sv);
}

TEST(lookahead_sub_pattern_shapes)
{
  // l_max_bytes and the sub-VM across sub-pattern shapes — all length-bounded, accepted.
  EXPECT(real::regex("(?=.)x").search("x"));                              // any (.)
  EXPECT_EQ(real::regex(R"((?=[^0-9])\w+)").search("ab12")[0], "ab12"sv); // negated class
  EXPECT_EQ(real::regex(R"((?=ab|xy)\w+)").search("xyz")[0], "xyz"sv);    // alternation
  EXPECT_EQ(real::regex(R"((?=(?:ab)c)\w+)").search("abc")[0], "abc"sv);  // non-capturing group
}

TEST(lookahead_negative_sub_partially_matches_then_fails)
{
  // The negative sub parks 'b', consumes it, then 'c' mismatches, so the sub cannot
  // match and (?!bc) holds — exercises the sub-VM consume-then-dead-end path.
  EXPECT_EQ(real::regex("a(?!bc)").search("abx")[0], "a"sv);
}

TEST(lookaround_rejects_unbounded_sub)
{
  // True unbounded (*, +, {n,}) → "unbounded … use a fixed repeat count".
  const auto expect_unbounded = [](const char* pat) {
                                  bool threw = false;
                                  try {
                                    real::regex r(pat);
                                  } catch (const real::regex_error& e) {
                                    threw = true;
                                    EXPECT(std::string_view(e.what()).find("unbounded lookaround") != std::string_view::npos);
                                  }
                                  EXPECT(threw);
                                };
  expect_unbounded("(?=a*)");
  expect_unbounded("(?=a+)");
  expect_unbounded("(?=a{2,})");
  expect_unbounded("(?<=a*)");    // behind, unbounded
  expect_unbounded("(?<!a+)");
  expect_unbounded("(?<=\\w+)b"); // true unbounded word run
}

TEST(lookaround_rejects_nested)
{
  EXPECT_THROWS(real::regex("(?=(?!a)b)"), real::regex_error);
  EXPECT_THROWS(real::regex("(?<=(?<=a)b)"), real::regex_error); // nested behind
}

TEST(lookaround_rejects_over_long_sub)
{
  // Bounded but over max_lookaround_length (255) → "too long", never "unbounded".
  const auto expect_too_long = [](const char* pat) {
                                 bool threw = false;
                                 try {
                                   real::regex r(pat);
                                 } catch (const real::regex_error& e) {
                                   threw = true;
                                   const std::string_view msg {e.what()};
                                   EXPECT(msg.find("too long") != std::string_view::npos);
                                   EXPECT(msg.find("unbounded") == std::string_view::npos);
                                 }
                                 EXPECT(threw);
                               };
  expect_too_long("(?=a{300})");    // 300 B > 255
  expect_too_long("(?<=a{300})");
  expect_too_long("(?<=\\w{64})b"); // \w counts 4 B → 256 > 255, fixed count
}

TEST(lookbehind_positive)
{
  EXPECT_EQ(real::regex("(?<=ab)c").search("xabc")[0], "c"sv);  // 'c' preceded by "ab"
  EXPECT_EQ(real::regex("(?<=ab)c").search("xabc").start(), 3U);
  EXPECT(!real::regex("(?<=ab)c").search("ac bc"));             // no "ab" precedes a 'c'
}

TEST(lookbehind_negative)
{
  // 'c' NOT preceded by "ab": leftmost is the first 'c' (preceded by 'x').
  EXPECT_EQ(real::regex("(?<!ab)c").search("xc abc")[0], "c"sv);
  EXPECT_EQ(real::regex("(?<!ab)c").search("xc abc").start(), 1U);
}

TEST(lookbehind_must_end_exactly_at_pos)
{
  // THE lookbehind trap: "ab" occurs but ends at index 2, not right before 'c' at 3, so
  // (?<=ab) must NOT hold there. A "somewhere in the window" check would wrongly match.
  EXPECT(!real::regex("(?<=ab)c").search("abxc"));
}

TEST(lookbehind_bounded_repeat_is_allowed)
{
  EXPECT_EQ(real::regex(R"((?<=\d{3})x)").search("12 123x")[0], "x"sv);
}

TEST(lookbehind_codepoint_aligned_start)
{
  // Multi-byte literal: L_max counts bytes (é = 2 bytes), and a candidate start may not fall
  // on a UTF-8 continuation byte (A9). 'x' is preceded by the codepoint é.
  EXPECT_EQ(real::regex("(?<=é)x").search("éx")[0], "x"sv);
  EXPECT(!real::regex("(?<=é)x").search("ax")); // preceded by ASCII 'a', not é
}

TEST(lookbehind_does_not_corrupt_main_captures)
{
  // Isolation: evaluating the lookbehind must not disturb the trailing capture.
  const auto m {real::regex("(?<=ab)(c)").search("abc")};
  EXPECT(m);
  EXPECT_EQ(m[0], "c"sv);
  EXPECT_EQ(m[1], "c"sv);
}

TEST(lookahead_and_lookbehind_combined)
{
  // Both directions in one pattern: 'b' preceded by 'a' and followed by 'c'.
  EXPECT_EQ(real::regex("(?<=a)b(?=c)").search("abc")[0], "b"sv);
  EXPECT(!real::regex("(?<=a)b(?=c)").search("abd")); // followed by 'd', not 'c'
}

TEST(lookbehind_variable_width_beyond_re)
{
  // REAL allows any *bounded* lookbehind sub, including variable-width — re and PCRE require
  // a fixed width here. (?<=a|bb) holds whether 'a' (1 back) or "bb" (2 back) precedes pos.
  const real::regex rx("(?<=a|bb)c");
  EXPECT_EQ(rx.search("xac")[0], "c"sv);  // preceded by 'a'
  EXPECT_EQ(rx.search("xbbc")[0], "c"sv); // preceded by "bb"
  EXPECT(!rx.search("xbc"));              // preceded by a single 'b' — neither branch
}

TEST(lookaround_in_bytes_mode)
{
  using real::flags;
  // Byte mode: the lookaround sub matches raw bytes, and the lookbehind start scan does NOT
  // skip UTF-8 continuation bytes (the byte_mode short-circuit in lookbehind_matches).
  EXPECT_EQ(real::regex("(?<=ab)c", flags::bytes).search("xabc")[0], "c"sv);
  EXPECT(!real::regex("(?<=ab)c", flags::bytes).search("abxc"));            // exact-end-at-pos holds
  EXPECT_EQ(real::regex("a(?!b)", flags::bytes).search("ab ax")[0], "a"sv); // negative lookahead
  // é = 0xC3 0xA9: a lookbehind may start on the continuation byte 0xA9 (no alignment skip),
  // so (?<=\xC3) holds right before it.
  EXPECT(real::regex(R"((?<=\xC3)\xA9)", flags::bytes).search("é"sv));
  EXPECT(!real::regex(R"((?<=\xC3)\xA9)", flags::bytes).search("a\xA9"sv)); // 0xA9 not preceded by 0xC3
}

// --- P3c trailing-LA class+ route: body keeps the class-loop scan; lookaround is an end-condition.
//     Differential against the pure Pike VM (trailing_la_route_disabled) proves transparency.
TEST(trailing_la_class_loop_flagship_spans)
{
  // Bench-engines case: [a-z]+(?=[a-z]) — greedy run stops one short when the next byte is a letter.
  const real::regex rx(R"([a-z]+(?=[a-z]))");
  EXPECT_EQ(rx.search("abc")[0], "ab"sv);
  EXPECT(!rx.search("a"sv)); // single letter: no end where (?=[a-z]) holds
  EXPECT_EQ(rx.search("ab")[0], "a"sv);
  const auto all {rx.find_all("the quick")};
  EXPECT_EQ(all.size(), 2U);
  EXPECT_EQ(all[0][0], "th"sv);
  EXPECT_EQ(all[1][0], "quic"sv);
  // Negative trailing LA: the maximal run itself holds when next is NOT in the class.
  EXPECT_EQ(real::regex(R"([a-z]+(?![a-z]))").search("abc def")[0], "abc"sv);
  EXPECT_EQ(real::regex(R"([0-9]+(?![0-9]))").search("123 45x")[0], "123"sv);
}

TEST(trailing_la_class_loop_routed_equals_core)
{
  using real::detail::trailing_la_route_disabled;
  const char* patterns[] = {
    R"([a-z]+(?=[a-z]))",  R"([a-z]+(?![a-z]))", R"([0-9]+(?![0-9]))",
    R"([a-z]+(?=z))",      R"([a-z]+(?=[a-z]{2}))", R"([a-f]+(?=[0-9]))",
  };
  const char* texts[] = {
    "the quick brown fox alpha", "abcz xyz abz zz", "123 45x 6",
    "abcde xy zzz", "aaa bbb c", "the the the quick", "", "a", "ab",
  };
  for (const char* p : patterns) {
    const real::regex rx(p);
    for (const char* t : texts) {
      trailing_la_route_disabled() = false;
      const auto routed {rx.find_all(t)};
      trailing_la_route_disabled() = true;
      const auto core   {rx.find_all(t)};
      trailing_la_route_disabled() = false;
      EXPECT_EQ(routed.size(), core.size());
      for (std::size_t i = 0; i < routed.size(); ++i) {
        EXPECT_EQ(routed[i].start(), core[i].start());
        EXPECT_EQ(routed[i].end(), core[i].end());
      }
    }
  }
}

TEST(trailing_la_class_loop_modes)
{
  const real::regex pos(R"([a-z]+(?=[a-z]))");
  const real::regex neg(R"([a-z]+(?![a-z]))");
  // prefix (match): only from the start
  EXPECT_EQ(pos.match("abc")[0], "ab"sv);
  EXPECT(!pos.match(" abc"));     // not at start
  // fullmatch: whole string must be the match span
  EXPECT(!pos.fullmatch("abc"));  // LA at end fails for positive self-class
  EXPECT(neg.fullmatch("abc"));   // negative holds at end-of-text
  EXPECT(!neg.fullmatch("abc ")); // trailing space not in body
}
