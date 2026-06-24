// Bounded lookahead (?=...) / (?!...) — linear-time, ReDoS-safe (COMMIT 1; lookbehind is
// COMMIT 2). Engine-level tests on real::regex (the dynamic path that carries lookarounds).
// Python re-parity lives in python/tests; here we pin the rejection of unbounded / nested /
// over-long sub-patterns and the capture-isolation of the sub-VM. Subjects are ASCII so the
// byte/char distinction does not intrude.
#include <string_view>

#include "framework.hpp"
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
  EXPECT_THROWS(real::regex("(?=a*)"), real::regex_error);
  EXPECT_THROWS(real::regex("(?=a+)"), real::regex_error);
  EXPECT_THROWS(real::regex("(?=a{2,})"), real::regex_error);
}

TEST(lookaround_rejects_nested)
{
  EXPECT_THROWS(real::regex("(?=(?!a)b)"), real::regex_error);
}

TEST(lookaround_rejects_over_long_sub)
{
  EXPECT_THROWS(real::regex("(?=a{300})"), real::regex_error); // > max_lookaround_length (255)
}

TEST(lookbehind_still_rejected_in_commit_1)
{
  // Lookbehind arrives in COMMIT 2; for now it is a clean compile error, not a miscompile.
  EXPECT_THROWS(real::regex("(?<=a)b"), real::regex_error);
  EXPECT_THROWS(real::regex("(?<!a)b"), real::regex_error);
}
