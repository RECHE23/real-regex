// Alternation, groups and captures. Several cases reproduce the historical
// alternation bugs of this engine family (nested exit targets, empty
// branches, branch order, captures across 3+ branches) as regression tests.
#include <string>
#include <string_view>

#include "framework.hpp"
#include "real/real.hpp"

using namespace std::string_view_literals;

TEST(alternation_basics)
{
  const real::regex rx("cat|dog|bird");
  EXPECT(rx.fullmatch("cat"));
  EXPECT(rx.fullmatch("dog"));
  EXPECT(rx.fullmatch("bird"));
  EXPECT(!rx.fullmatch("cow"));
  EXPECT_EQ(rx.search("hotdog!").start(), 3U);
}

TEST(alternation_is_leftmost_first_not_longest)
{
  // Python: re.search(r'(a|ab)', 'ab').group(0) == 'a'
  EXPECT_EQ(real::regex("a|ab").search("ab")[0], "a"sv);
  EXPECT_EQ(real::regex("ab|a").search("ab")[0], "ab"sv);
}

TEST(alternation_empty_branches)
{
  EXPECT(real::regex("a|").fullmatch(""));
  EXPECT(real::regex("a|").fullmatch("a"));
  EXPECT(real::regex("|a").fullmatch(""));
  EXPECT(real::regex("a||b").fullmatch(""));
  EXPECT(real::regex("(a|)b").fullmatch("b"));
  EXPECT(real::regex("(a|)b").fullmatch("ab"));
}

TEST(nested_alternation_exit_targets)
{
  // Historical bug: inner Split exit targets not adjusted in nested alts.
  const real::regex rx("((a|b)|c)d");
  EXPECT(rx.fullmatch("ad"));
  EXPECT(rx.fullmatch("bd"));
  EXPECT(rx.fullmatch("cd"));
  EXPECT(!rx.fullmatch("d"));
  const real::regex rx2("(a|(b|c)x|d)y");
  EXPECT(rx2.fullmatch("ay"));
  EXPECT(rx2.fullmatch("bxy"));
  EXPECT(rx2.fullmatch("cxy"));
  EXPECT(rx2.fullmatch("dy"));
  EXPECT(!rx2.fullmatch("by"));
}

TEST(alternation_under_star)
{
  // Historical bug: star quantifier over a multi-literal alternation.
  const real::regex rx("(foo|bar)*baz");
  EXPECT(rx.fullmatch("baz"));
  EXPECT(rx.fullmatch("foobaz"));
  EXPECT(rx.fullmatch("barfoobaz"));
  EXPECT(rx.fullmatch("foofoobarbaz"));
  EXPECT(!rx.fullmatch("fobaz"));
}

TEST(captures_basic)
{
  const real::regex        rx("(\\d{4})-(\\d{2})");
  auto m = rx.search("date: 2026-06!");
  EXPECT(m);
  EXPECT_EQ(m.size(), 3U);
  EXPECT_EQ(m[0], "2026-06"sv);
  EXPECT_EQ(m[1], "2026"sv);
  EXPECT_EQ(m[2], "06"sv);
  EXPECT_EQ(m.start(1), 6U);
  EXPECT_EQ(m.end(2), 13U);
}

TEST(captures_numbered_by_open_paren)
{
  const real::regex        rx("((a)(b))c");
  auto m = rx.fullmatch("abc");
  EXPECT_EQ(m[1], "ab"sv);
  EXPECT_EQ(m[2], "a"sv);
  EXPECT_EQ(m[3], "b"sv);
  EXPECT_EQ(rx.group_count(), 3U);
}

TEST(captures_across_three_branch_alternation)
{
  // Historical bug: captures lost in alternations of 3+ branches.
  const real::regex        rx("(a)|(b)|(c)");
  auto m = rx.search("zb");
  EXPECT(m);
  EXPECT_EQ(m.start(1), real::npos); // like Python: (None, 'b', None)
  EXPECT_EQ(m[2], "b"sv);
  EXPECT_EQ(m.start(3), real::npos);
}

TEST(unset_and_repeated_groups_python_semantics)
{
  EXPECT_EQ(real::regex("(a)?b").fullmatch("b").start(1), real::npos);
  // Quantified group keeps its last iteration: Python start(1) == 2.
  EXPECT_EQ(real::regex("(ab)+").fullmatch("abab").start(1), 2U);
  EXPECT_EQ(real::regex("(a+)+").match("aaa")[1], "aaa"sv);
  // (a*)(a*): greedy first, empty second.
  auto m = real::regex("(a*)(a*)").fullmatch("aa");
  EXPECT_EQ(m[1], "aa"sv);
  EXPECT_EQ(m[2], ""sv);
}

TEST(nullable_loop_capture_keeps_last_nonempty_iteration)
{
  // Documented divergence from Python, shared with Perl/PCRE: for (a*)*
  // on "aa", Python reports the loop's final *empty* iteration (''); we —
  // like engines that forbid repeating an empty match — report "aa".
  // Group 0 is identical in both. This is the only known divergence.
  EXPECT_EQ(real::regex("(a*)*").match("aa")[0], "aa"sv);
  EXPECT_EQ(real::regex("(a*)*").match("aa")[1], "aa"sv);
  EXPECT_EQ(real::regex("(a*)*").match("").end(), 0U);
}

TEST(non_capturing_groups)
{
  const real::regex        rx("(?:ab)+(c)");
  auto m = rx.fullmatch("ababc");
  EXPECT_EQ(rx.group_count(), 1U);
  EXPECT_EQ(m[1], "c"sv);
  EXPECT(real::regex("(?:a|b)c").fullmatch("bc"));
}

TEST(named_groups_python_and_dotnet_styles)
{
  const real::regex        rx("(?P<year>\\d{4})-(?<month>\\d{2})");
  auto m = rx.search("2026-06");
  EXPECT_EQ(m.group_index("year"), 1U);
  EXPECT_EQ(m.group_index("month"), 2U);
  EXPECT_EQ(m["year"], "2026"sv);
  EXPECT_EQ(m["month"], "06"sv);
  EXPECT_EQ(m.start("year"), 0U);
  EXPECT_EQ(m.end("month"), 7U);
  EXPECT_EQ(m.group_index("day"), real::npos);
  EXPECT_EQ(m["day"], ""sv);
}

TEST(quantified_groups)
{
  EXPECT(real::regex("(ab){2,3}").fullmatch("ababab"));
  EXPECT(!real::regex("(ab){2,3}").fullmatch("ab"));
  EXPECT(real::regex("(a|b){2}?c").fullmatch("abc"));
  EXPECT_EQ(real::regex("(a)?").fullmatch("").start(1), real::npos);
}

TEST(group_errors)
{
  EXPECT_THROWS(real::regex("(a"), real::regex_error);
  EXPECT_THROWS(real::regex(")a"), real::regex_error);
  EXPECT_THROWS(real::regex("a)"), real::regex_error);
  EXPECT_THROWS(real::regex("(?P<1a>x)"), real::regex_error);
  EXPECT_THROWS(real::regex("(?P<n>x)(?P<n>y)"), real::regex_error);
  EXPECT_THROWS(real::regex("(?P<n*>x)"), real::regex_error);
  EXPECT_THROWS(real::regex("(?P=n)"), real::regex_error);
  EXPECT_THROWS(real::regex("(?=x)"), real::regex_error);
  EXPECT_THROWS(real::regex("(?!x)"), real::regex_error);
  EXPECT_THROWS(real::regex("(?<=x)"), real::regex_error);
  EXPECT_THROWS(real::regex("(?<!x)"), real::regex_error);
  EXPECT_THROWS(real::regex("(?>x)"), real::regex_error);
}

TEST(nesting_depth_is_bounded_instead_of_overflowing_the_stack)
{
  // Regression: the recursive-descent parser used to crash (stack overflow)
  // on '('*50000 — a denial of service through any binding. Deep nesting is
  // now a clean regex_error; reasonable nesting keeps working.
  const auto nested = [](std::size_t depth) {
    std::string pattern(depth, '(');
    pattern += "a";
    pattern.append(depth, ')');
    return pattern;
  };
  EXPECT(real::regex(nested(100)).fullmatch("a"));
  EXPECT(real::regex(nested(real::detail::max_nesting_depth)).fullmatch("a"));
  EXPECT_THROWS(real::regex(nested(real::detail::max_nesting_depth + 1)), real::regex_error);
  EXPECT_THROWS(real::regex(nested(50000)), real::regex_error);
  EXPECT_THROWS(real::regex(std::string(50000, '(')), real::regex_error);
}

TEST(unterminated_group_reports_open_paren_position)
{
  try {
    real::regex rx("ab(cd");
    EXPECT(false);
  }
  catch (const real::regex_error& e) {
    EXPECT_EQ(e.position(), 2U);
  }
}
