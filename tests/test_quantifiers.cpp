// Quantifiers: greedy and lazy *, +, ?, {n}, {n,}, {,match}, {n,match} — and the
// linear-time guarantee on patterns that explode backtracking engines.
#include <chrono>
#include <string_view>

#include "framework.hpp"
#include "real/real.hpp"

using namespace std::string_view_literals;

TEST(star_plus_question_greedy)
{
  EXPECT(real::regex("a*").fullmatch(""));
  EXPECT(real::regex("a*").fullmatch("aaaa"));
  EXPECT(real::regex("a+").fullmatch("a"));
  EXPECT(!real::regex("a+").fullmatch(""));
  EXPECT(real::regex("a?").fullmatch(""));
  EXPECT(real::regex("a?").fullmatch("a"));
  EXPECT(!real::regex("a?").fullmatch("aa"));
  EXPECT(real::regex("ab*c").fullmatch("ac"));
  EXPECT(real::regex("ab*c").fullmatch("abbbc"));
  EXPECT_EQ(real::regex("a+").match("aaab").end(), 3U); // greedy: longest
  EXPECT_EQ(real::regex("a*").match("bbb").end(), 0U);
}

TEST(lazy_quantifiers_prefer_shortest)
{
  EXPECT_EQ(real::regex("a+?").match("aaa").end(), 1U);
  EXPECT_EQ(real::regex("a*?").match("aaa").end(), 0U);
  EXPECT_EQ(real::regex("a??").match("a").end(), 0U);
  // The classic: lazy dot stops at the first closing bracket.
  EXPECT_EQ(real::regex("<.+?>").search("<a><b>")[0], std::string_view("<a>"));
  EXPECT_EQ(real::regex("<.+>").search("<a><b>")[0], std::string_view("<a><b>"));
}

TEST(counted_repetitions)
{
  EXPECT(real::regex("a{3}").fullmatch("aaa"));
  EXPECT(!real::regex("a{3}").fullmatch("aa"));
  EXPECT(!real::regex("a{3}").fullmatch("aaaa"));
  EXPECT(real::regex("a{2,}").fullmatch("aa"));
  EXPECT(real::regex("a{2,}").fullmatch("aaaaa"));
  EXPECT(!real::regex("a{2,}").fullmatch("a"));
  EXPECT(real::regex("a{2,4}").fullmatch("aa"));
  EXPECT(real::regex("a{2,4}").fullmatch("aaaa"));
  EXPECT(!real::regex("a{2,4}").fullmatch("aaaaa"));
  EXPECT(real::regex("a{,3}").fullmatch(""));
  EXPECT(real::regex("a{,3}").fullmatch("aaa"));
  EXPECT(!real::regex("a{,3}").fullmatch("aaaa"));
  EXPECT(real::regex("a{0}").fullmatch(""));
  EXPECT_EQ(real::regex("a{2,4}").match("aaaaa").end(), 4U);  // greedy
  EXPECT_EQ(real::regex("a{2,4}?").match("aaaaa").end(), 2U); // lazy
}

TEST(quantifiers_apply_to_classes_and_dot)
{
  EXPECT(real::regex("\\d+").fullmatch("12345"));
  EXPECT(real::regex("[ab]*").fullmatch("abba"));
  EXPECT(real::regex(".*").fullmatch("héllo wörld"));
  EXPECT(real::regex("[^x]{2}").fullmatch("éé"));
  EXPECT_EQ(real::regex("\\d{4}").search("le 2026-06-09").start(), 3U);
}

TEST(invalid_braces_are_literal_like_python)
{
  EXPECT(real::regex("a{").fullmatch("a{"));
  EXPECT(real::regex("a{}").fullmatch("a{}"));
  EXPECT(real::regex("a{,}").fullmatch("a{,}"));
  EXPECT(real::regex("a{2,3x").fullmatch("a{2,3x"));
  EXPECT(real::regex("{2}").fullmatch("{2}"));
  EXPECT(real::regex("a{2}").fullmatch("aa")); // sanity: valid braces repeat
}

TEST(quantifier_errors)
{
  EXPECT_THROWS(real::regex("*a"), real::regex_error);
  EXPECT_THROWS(real::regex("+a"), real::regex_error);
  EXPECT_THROWS(real::regex("?a"), real::regex_error);
  EXPECT_THROWS(real::regex("a**"), real::regex_error);
  EXPECT_THROWS(real::regex("a*+"), real::regex_error);
  EXPECT_THROWS(real::regex("a*?*"), real::regex_error);
  EXPECT_THROWS(real::regex("a*{2}"), real::regex_error);
  EXPECT_THROWS(real::regex("a{3,2}"), real::regex_error);
  EXPECT_THROWS(real::regex("a{1001}"), real::regex_error);
}

TEST(counted_class_fixed_width)
{
  // Whole-pattern "class{n}" takes a fixed-width fast path; results must equal
  // the general engine. Fixed-width tokens (hex ids, codes) are the target.
  const real::regex hex("[0-9a-f]{8}");
  EXPECT_EQ(hex.search("req=a3f9c1d8 end").start(), 4U);
  EXPECT_EQ(hex.search("req=a3f9c1d8 end")[0], "a3f9c1d8"sv);
  EXPECT(!hex.search("only 1234ab here")); // fewer than 8 hex in a row
  const auto all = hex.find_all("a3f9c1d8 x deadbeef");
  EXPECT_EQ(all.size(), 2U);
  EXPECT_EQ(all[1][0], "deadbeef"sv);
  // match / fullmatch anchoring.
  EXPECT_EQ(hex.match("deadbeefXX").end(), 8U);
  EXPECT(!hex.match("zz"));            // not at start
  EXPECT(hex.fullmatch("deadbeef"));
  EXPECT(!hex.fullmatch("deadbeef0")); // exactly 8, no more
  // A longer run yields back-to-back fixed windows (no overlap).
  const real::regex d4("[0-9]{4}");
  EXPECT_EQ(d4.find_all("123456789").size(), 2U);
}

TEST(fixed_shape_sequence)
{
  // A straight-line mix of classes and literals (e.g. a date) is a fixed-width
  // sequence taking the same fast path; results equal the general engine.
  const real::regex date("[0-9]{4}-[0-9]{2}-[0-9]{2}");
  EXPECT_EQ(date.search("on 2026-06-13!").start(), 3U);
  EXPECT_EQ(date.search("on 2026-06-13!")[0], "2026-06-13"sv);
  EXPECT(!date.search("2026/06/13"));      // wrong separators
  EXPECT(!date.search("202-06-13"));       // too few year digits
  EXPECT_EQ(date.find_all("2026-06-13 1999-01-02").size(), 2U);
  EXPECT(date.fullmatch("2026-06-13"));
  EXPECT(!date.fullmatch("2026-06-13 "));  // trailing content
  // Class + literal interleaving.
  const real::regex mix("a[0-9]c");
  EXPECT_EQ(mix.find_all("xa5cya9c").size(), 2U);
}

TEST(class_scan_fast_paths_iterate)
{
  // The "class+" and codepoint-class fast paths build a flat membership table
  // once and reuse it across an iterated walk. Iterating several matches must
  // give exactly the general engine's greedy result (and exercises the table
  // cache-hit on every match after the first).
  const real::regex word("[a-z]+");
  const auto        ws = word.find_all("foo bar123 baz qux");
  EXPECT_EQ(ws.size(), 4U);
  EXPECT_EQ(ws[0][0], "foo"sv);
  EXPECT_EQ(ws[1][0], "bar"sv);              // stops before the digits
  EXPECT_EQ(ws[3][0], "qux"sv);
  EXPECT_EQ(word.match("hello!").end(), 5U); // greedy, longest run
  EXPECT(word.fullmatch("abc"));
  EXPECT(!word.fullmatch("abc1"));           // trailing non-class

  const real::regex digits("[0-9]+");
  EXPECT_EQ(digits.find_all("a12b345c6").size(), 3U);

  // Codepoint-class fast path (negated class spanning a greedy run), iterated.
  const real::regex field("[^,]+");
  const auto        fs = field.find_all("a,bb,ccc");
  EXPECT_EQ(fs.size(), 3U);
  EXPECT_EQ(fs[1][0], "bb"sv);
  EXPECT_EQ(fs[2][0], "ccc"sv);
  // `.` spans whole (multibyte) codepoints; the ASCII table only gates ASCII.
  const real::regex dot(".+");
  EXPECT_EQ(dot.find_all("ab\ncd").size(), 2U); // `.` excludes the newline
}

TEST(pathological_pattern_stays_linear)
{
  // a*a*a*a*a*b is exponential for naive backtrackers; the Pike VM is
  // linear by construction. Generous bound: just proves no blowup.
  const real::regex rx("a*a*a*a*a*a*a*a*b");
  const std::string text(2000, 'a');
  const auto        start = std::chrono::steady_clock::now();
  EXPECT(!rx.search(text));
  const auto elapsed = std::chrono::steady_clock::now() - start;
  EXPECT(elapsed < std::chrono::seconds(2));
}
