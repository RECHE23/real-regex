// find_iter / find_all / replace / split, with Python's empty-match rules
// (verified against re: spans, sub and split outputs are identical).
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include <sciforge/test/framework.hpp>
#include "real/real.hpp"

using namespace std::string_view_literals;

TEST(find_all_basic)
{
  const real::regex rx("\\d+");
  const auto        all = rx.find_all("a1 bb22 c333");
  EXPECT_EQ(all.size(), 3U);
  EXPECT_EQ(all[0][0], "1"sv);
  EXPECT_EQ(all[1][0], "22"sv);
  EXPECT_EQ(all[2][0], "333"sv);
  EXPECT_EQ(all[2].start(), 9U);
  EXPECT_EQ(rx.find_all("none").size(), 0U);
}

TEST(find_iter_is_lazy_and_range_for_compatible)
{
  const real::regex rx("[ab]");
  std::string       seen;
  for (const auto& match : rx.find_iter("xaybz")) {
    seen += match[0];
  }
  EXPECT_EQ(seen, std::string("ab"));
}

TEST(find_iter_with_captures_and_anchors)
{
  const real::regex rx("^(\\w+)", real::flags::multiline);
  const auto        all = rx.find_all("foo x\nbar y");
  EXPECT_EQ(all.size(), 2U);
  EXPECT_EQ(all[0][1], "foo"sv);
  EXPECT_EQ(all[1][1], "bar"sv);
}

TEST(temporary_regex_cannot_dangle_in_iteration)
{
  // find_iter / find_all on a temporary regex are deleted overloads: in a
  // C++20 range-for the temporary would die before the loop body. This
  // must not compile (caught originally by the constexpr interpreter):
  //   for (auto& match : real::regex("x").find_iter(text)) { ... }
  const real::regex rx("x");
  EXPECT(rx.find_iter("x").begin() != rx.find_iter("x").end());
}

TEST(empty_matches_python_spans)
{
  // Python: [(0,0), (1,2), (2,2), (3,3)] — empty right after non-empty.
  const real::regex rx("x*");
  const auto        all = rx.find_all("axb");
  EXPECT_EQ(all.size(), 4U);
  EXPECT_EQ(all[0].start(), 0U);
  EXPECT_EQ(all[0].end(), 0U);
  EXPECT_EQ(all[1].start(), 1U);
  EXPECT_EQ(all[1].end(), 2U);
  EXPECT_EQ(all[2].start(), 2U);
  EXPECT_EQ(all[2].end(), 2U);
  EXPECT_EQ(all[3].start(), 3U);
  EXPECT_EQ(all[3].end(), 3U);
}

TEST(empty_matches_advance_whole_codepoints)
{
  // '.' over "héo": 3 matches, never splitting the 2-byte é.
  const real::regex dot(".");
  const auto        dots = dot.find_all("héo");
  EXPECT_EQ(dots.size(), 3U);
  EXPECT_EQ(dots[1][0], "é"sv);
  // Empty matches between codepoints: 3 positions, like Python.
  const real::regex xs("x*");
  EXPECT_EQ(xs.find_all("éo").size(), 3U);
}

TEST(replace_basic_and_count)
{
  const real::regex rx("\\d+");
  EXPECT_EQ(rx.replace("a1b22c", "#"), std::string("a#b#c"));
  EXPECT_EQ(rx.replace("a1b22c", "#", 1), std::string("a#b22c"));
  EXPECT_EQ(rx.replace("none", "#"), std::string("none"));
  // Python: re.sub('x*', '-', 'axb') == '-a--b-'
  EXPECT_EQ(real::regex("x*").replace("axb", "-"), std::string("-a--b-"));
}

TEST(replace_group_references)
{
  const real::regex rx("(\\w+)@(\\w+)");
  EXPECT_EQ(rx.replace("bob@host", "$2:$1"), std::string("host:bob"));
  EXPECT_EQ(rx.replace("bob@host", "$&!"), std::string("bob@host!"));
  EXPECT_EQ(rx.replace("bob@host", "$0!"), std::string("bob@host!"));
  EXPECT_EQ(rx.replace("bob@host", "$$$1"), std::string("$bob"));
  const real::regex named("(?P<user>\\w+)@(?P<host>\\w+)");
  EXPECT_EQ(named.replace("bob@host", "${host}/${user}"), std::string("host/bob"));
  // Unset group expands to nothing.
  EXPECT_EQ(real::regex("(a)|(b)").replace("b", "[$1$2]"), std::string("[b]"));
}

TEST(replace_errors)
{
  const real::regex rx("(a)");
  EXPECT_THROWS(rx.replace("a", "$"), real::regex_error);
  EXPECT_THROWS(rx.replace("a", "$2"), real::regex_error);
  EXPECT_THROWS(rx.replace("a", "$x"), real::regex_error);
  EXPECT_THROWS(rx.replace("a", "${nope}"), real::regex_error);
  EXPECT_THROWS(rx.replace("a", "${}"), real::regex_error);
  EXPECT_THROWS(rx.replace("a", "${a"), real::regex_error);
}

TEST(split_python_semantics)
{
  const real::regex comma(",");
  const auto        parts = comma.split("a,b,,c");
  EXPECT_EQ(parts.size(), 4U);
  EXPECT_EQ(parts[0], "a"sv);
  EXPECT_EQ(parts[2], ""sv);
  EXPECT_EQ(parts[3], "c"sv);
  // With a capturing group, the separator is kept: ['a', ',', 'b,c'].
  const auto kept = real::regex("(,)").split("a,b,c", 1);
  EXPECT_EQ(kept.size(), 3U);
  EXPECT_EQ(kept[1], ","sv);
  EXPECT_EQ(kept[2], "b,c"sv);
  // Python: re.split('x*', 'axb') == ['', 'a', '', 'b', ''].
  const auto empties = real::regex("x*").split("axb");
  EXPECT_EQ(empties.size(), 5U);
  EXPECT_EQ(empties[1], "a"sv);
  EXPECT_EQ(empties[3], "b"sv);
  // No match: one piece, the whole text.
  EXPECT_EQ(comma.split("abc").size(), 1U);
}

TEST(iteration_state_is_reusable)
{
  const real::regex rx("a");
  const std::string text(1000, 'a');
  EXPECT_EQ(rx.find_all(text).size(), 1000U);
  EXPECT_EQ(rx.replace(text, "b"), std::string(1000, 'b'));
}

TEST(empty_match_then_nonempty_at_same_position_cpython37)
{
  // Regression (found by differential fuzzing vs re): CPython 3.7+ allows a
  // non-empty match to start right after an empty match at the SAME position.
  // \W??\b on "cbAAA.A__A": after the empty (5,5), the non-empty (5,6)
  // (consuming '.') must appear before advancing — it used to be skipped.
  const real::regex     rx("\\W??\\b");
  const auto            all = rx.find_all("cbAAA.A__A");
  EXPECT_EQ(all.size(), 5U);
  EXPECT_EQ(all[1].start(), 5U);
  EXPECT_EQ(all[1].end(), 5U); // empty
  EXPECT_EQ(all[2].start(), 5U);
  EXPECT_EQ(all[2].end(), 6U); // non-empty, same start
  EXPECT_EQ(all[3].start(), 6U);
  // The skip stays codepoint-aligned: x* over "é" yields two matches, never
  // a match starting inside the 2-byte sequence.
  const real::regex star("x*");
  EXPECT_EQ(star.find_all("é").size(), 2U);
}

TEST(find_iter_models_forward_iterator)
{
  const real::regex rx("\\d+");
  using iter_t = decltype(rx.find_iter("").begin());
  static_assert(std::forward_iterator<iter_t>); // conformance: post-increment + multipass

  // it++ returns the pre-increment position (an independent copy); iteration then continues.
  auto       range {rx.find_iter("a1 b22 c333")};
  auto       it    {range.begin()};
  const auto first {it++};
  EXPECT_EQ((*first)[0], "1"sv);  // the copy kept the old position
  EXPECT_EQ((*it)[0], "22"sv);    // *this advanced
  ++it;
  EXPECT_EQ((*it)[0], "333"sv);
}

// The iterator refreshes one match in place across steps (reusing its slot buffer, the per-match diet),
// so a saved COPY of an earlier match must stay independent of later ones. Collect every match by value
// during iteration and check them all afterwards — the copies must not alias the reused buffer.
TEST(find_iter_copies_survive_the_reused_buffer)
{
  const real::regex                                   rx("(\\w)(\\d+)");
  auto                                                range {rx.find_iter("a1 b22 c333")};
  std::vector<std::decay_t<decltype(*range.begin())>> saved;
  for (const auto& match : range) {
    saved.push_back(match); // a by-value copy — must own its slots
  }
  EXPECT_EQ(saved.size(), std::size_t {3});
  EXPECT_EQ(saved[0][0], "a1"sv);
  EXPECT_EQ(saved[0][1], "a"sv);
  EXPECT_EQ(saved[0][2], "1"sv);
  EXPECT_EQ(saved[1][0], "b22"sv);
  EXPECT_EQ(saved[1][2], "22"sv);
  EXPECT_EQ(saved[2][0], "c333"sv);
  EXPECT_EQ(saved[2][2], "333"sv);
}
