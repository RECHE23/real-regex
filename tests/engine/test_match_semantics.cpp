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

// A leftmost-first case that the `regex` crate answers differently, pinned here because the answer is not a
// matter of taste: CPython's `re` reports exactly what REAL reports, span for span, and the crate reports a
// FOURTH alternative where the FIRST one matches at the same start. Reached by the Rust differential fuzzer
// (CI run 31441986955); the crate's bug is rust-lang/regex #1345's class, and it reproduces there even under
// anchoring, which is why that harness's exemption had to learn to confirm branch by branch.
//
// Byte arrays rather than string literals on purpose: the pattern and the subject both carry NUL and control
// bytes, and a concatenated escape sequence silently produced a 38-byte pattern out of a 31-byte one while
// this case was being reduced.
TEST(leftmost_first_prefers_the_first_alternative_over_a_shorter_later_one)
{
  static const unsigned char pattern_bytes[] {
    0x2E, 0x41, 0x2B, 0x41, 0x0A, 0x23, 0x01, 0x41, 0x40, 0x7C, 0x2E, 0x41, 0x2B, 0x41, 0x7A, 0x7A,
    0x00, 0x7C, 0x2E, 0x41, 0x2B, 0x41, 0x0A, 0x23, 0x01, 0x41, 0x40, 0x7C, 0x2E, 0x41, 0x2B
  };
  static const unsigned char subject_bytes[] {
    0x41, 0x7A, 0x7A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x45, 0x41, 0x0A, 0x23, 0x01, 0x41,
    0x40, 0x7C, 0x2E, 0x41, 0x2B, 0x41, 0x7A, 0x7A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x2E, 0x41, 0x41, 0x0A, 0x23, 0x01, 0x41, 0x40, 0x7C, 0x2E, 0x41, 0x2B,
    0x41, 0x7A, 0x7A, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x03, 0x45, 0x31
  };
  const std::string_view pattern {reinterpret_cast<const char*>(pattern_bytes), sizeof(pattern_bytes)};
  const std::string_view subject {reinterpret_cast<const char*>(subject_bytes), sizeof(subject_bytes)};

  const real::regex rx           {pattern};
  // CPython 3: [(10, 12), (14, 16), (18, 20), (20, 22), (36, 44), (45, 47), (47, 49)].
  // The crate:  ... (36, 39), (41, 43) ... -- the fourth branch `.A+` where the first branch matches 36..44.
  const std::size_t want_start[] {10, 14, 18, 20, 36, 45, 47};
  const std::size_t want_end[]   {12, 16, 20, 22, 44, 47, 49};
  std::size_t       i            {0};
  for (const auto& m : rx.find_iter(subject)) {
    EXPECT(i < 7);
    if (i < 7) {
      EXPECT(m.start(0) == want_start[i]);
      EXPECT(m.end(0) == want_end[i]);
    }
    ++i;
  }
  EXPECT(i == 7);
  // The same answer on the other enumerating surfaces, and with the lazy-DFA route pulled: this subject is
  // 61 bytes, far under the route's runway, so no batched walk is involved either way -- the pin is on the
  // general path's alternation priority.
  EXPECT(rx.count_matches(subject) == 7);
  EXPECT(rx.find_all(subject).size() == 7);
  const auto first {rx.search(subject)};
  EXPECT(first.matched());
  EXPECT(first.start(0) == 10);
  EXPECT(first.end(0) == 12);
}
