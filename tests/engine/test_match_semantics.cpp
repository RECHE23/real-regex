// The experimental, opt-in leftmost-LONGEST search mode (`match_semantics::longest`, reached via
// `regex::search_longest`). The default (leftmost-first) is untouched — that byte-identity is the anti-regression
// contract, asserted by the whole rest of the suite plus the exhaustive/matrix gates running in first mode. Here
// we pin the longest bounds against the RE2 `set_longest_match` oracle (validated offline) and the v1 capture
// rule (the winning thread's captures at the longest bound, NOT POSIX submatch).
#include <string_view>

#include <sciforge/test/framework.hpp>
#include "real/real.hpp"

using namespace std::string_view_literals;

namespace {
  void expect_longest(std::string_view pat,
                      std::string_view text,
                      std::size_t      s,
                      std::size_t      e)
  {
    const auto m = real::regex(pat).search_longest(text);
    EXPECT(m.matched());
    EXPECT(m.start() == s);
    EXPECT(m.end() == e);
  }
}

TEST(match_semantics_longest_acids)
{
  // leftmost-longest: the leftmost start, then the longest match from it (POSIX / RE2 set_longest_match).
  expect_longest("a|ab"sv, "ab"sv, 0, 2);        // the longer branch wins the bounds
  expect_longest("aabaa|b"sv, "aabaa"sv, 0, 5);
  expect_longest("ab|a|abc"sv, "abc"sv, 0, 3);   // the longest of three branches
  expect_longest("x*y|x*"sv, "xxxx"sv, 0, 4);
  expect_longest("a+?"sv, "aaa"sv, 0, 3);        // a lazy quantifier behaves greedily under longest
  expect_longest("(a|aa)*"sv, "aaa"sv, 0, 3);
  expect_longest("(a|ab)(b|)"sv, "abb"sv, 0, 3);
  expect_longest("a*"sv, "aaa"sv, 0, 3);
  // leftmost still dominates length: an earlier start wins even if a later start could match longer.
  expect_longest("a|ab"sv, "xab"sv, 1, 3);
}

TEST(match_semantics_first_is_unchanged)
{
  // The default mode is byte-identical to before this mode existed — leftmost-first, source-order priority.
  const auto f = real::regex("a|ab").search("ab");
  EXPECT(f.matched() && f.start() == 0 && f.end() == 1); // the FIRST branch, not the longest
  const auto g = real::regex("a+?").search("aaa");
  EXPECT(g.matched() && g.start() == 0 && g.end() == 1); // lazy stays lazy in first mode
}

TEST(match_semantics_longest_captures_are_winning_thread)
{
  // v1: captures are the winning (greedy) thread's at the longest bound — documented, not POSIX submatch.
  const auto m = real::regex("(a|ab)(b|)").search_longest("abb");
  EXPECT(m.matched() && m.start() == 0 && m.end() == 3);
  EXPECT(m[1] == "ab"sv); // the (ab)(b) path reaches [0,3)
  EXPECT(m[2] == "b"sv);
  const auto n = real::regex("(a+)(a+)").search_longest("aaaa");
  EXPECT(n.matched() && n.start() == 0 && n.end() == 4);
  EXPECT(n[1] == "aaa"sv); // greedy first group
  EXPECT(n[2] == "a"sv);
}

TEST(match_semantics_find_iter_longest)
{
  // The iteration twin of search_longest: each occurrence is leftmost-longest, not leftmost-first.
  const real::regex   re       {"a|ab"};
  const std::size_t   starts[] {0, 4, 8};
  const std::size_t   ends[]   {2, 6, 10}; // the longer branch at each occurrence
  std::size_t         i        {0};
  for (const auto& m : re.find_iter_longest("ab xab yab")) {
    EXPECT(i < 3);
    EXPECT(m.start() == starts[i] && m.end() == ends[i]);
    ++i;
  }
  EXPECT(i == 3);
  // first mode (find_iter) is unchanged: each occurrence is the shorter, leftmost-first branch.
  const std::size_t first_ends[] {1, 5, 9};
  i = 0;
  for (const auto& m : re.find_iter("ab xab yab")) {
    EXPECT(m.start() == starts[i] && m.end() == first_ends[i]);
    ++i;
  }
}
