// Region-aware match/search/fullmatch: pos (the VM start offset — NOT a slice, so
// zero-width anchors still see the absolute position) and endpos (a truncated view).
// Byte offsets; subjects are ASCII so byte == char. These pin the C++ engine overloads
// that back the Python binding's pos/endpos (whose parity vs re is tested separately).
#include <string_view>

#include "framework.hpp"
#include "real/real.hpp"

using namespace std::string_view_literals;

TEST(region_pos_starts_matching_there)
{
  const real::regex rx("\\w+");
  const auto        m {rx.search("foo bar baz"sv, 4)};
  EXPECT(m);
  EXPECT_EQ(m[0], "bar"sv);
  EXPECT_EQ(m.start(), 4U);
  EXPECT_EQ(rx.match("foo bar"sv, 4)[0], "bar"sv); // anchored exactly at pos
  EXPECT(!rx.match("foo bar"sv, 3));               // pos 3 is a space → nothing anchored there
}

TEST(region_endpos_truncates)
{
  const real::regex rx("\\w+");
  const auto        m {rx.search("hello world"sv, 0, 3)};
  EXPECT(m);
  EXPECT_EQ(m[0], "hel"sv);                                     // capped at endpos = 3
  EXPECT_EQ(m.end(), 3U);
  EXPECT_EQ(rx.fullmatch("hello world"sv, 0, 5)[0], "hello"sv); // region [0,5) is all \w
  EXPECT(!rx.fullmatch("hello world"sv, 0, 11));                // the space breaks fullmatch
}

TEST(region_pos_greater_than_endpos_no_match)
{
  EXPECT(!real::regex("\\w+").search("hello"sv, 4, 2));
}

TEST(region_endpos_clamped_to_length)
{
  const auto m {real::regex("\\w+").search("hi"sv, 0, 999)};
  EXPECT(m);
  EXPECT_EQ(m[0], "hi"sv);
}

TEST(region_anchor_text_start_is_absolute_not_slice)
{
  const real::regex rx("\\Aabc");
  EXPECT(rx.match("abcdef"sv, 0));    // \A at pos 0 holds
  EXPECT(!rx.search("xabcdef"sv, 1)); // \A at pos 1 fails — absolute, not a re-based slice
}

TEST(region_caret_multiline_versus_plain_at_pos)
{
  // ^ MULTILINE at pos>0 holds IFF text[pos-1] == '\n' (the "pos is not slicing" case).
  const real::regex ml("^bar", real::flags::multiline);
  EXPECT_EQ(ml.search("foo\nbar"sv, 4)[0], "bar"sv); // pos 4 right after '\n'
  EXPECT(!ml.search("foobar"sv, 3));                 // pos 3 not after '\n'
  // ^ without MULTILINE only holds at absolute 0, even right after a '\n'.
  EXPECT(!real::regex("^bar").search("foo\nbar"sv, 4));
}

TEST(region_dollar_and_text_end_see_endpos)
{
  EXPECT_EQ(real::regex("o$").search("foobar"sv, 0, 3).end(), 3U); // $ at endpos = 3
  EXPECT(real::regex("r\\Z").search("barbaz"sv, 0, 3));            // \Z at endpos = 3
}

TEST(region_dollar_plain_before_trailing_newline_at_endpos)
{
  // $ (non-multiline) also holds just before a '\n' that sits at endpos-1.
  EXPECT(real::regex("o$").search("foo\nx"sv, 0, 4)); // region "foo\n"
}

TEST(region_dollar_multiline_before_internal_newline)
{
  const auto m {real::regex("o$", real::flags::multiline).search("foo\nbar"sv, 0, 7)};
  EXPECT(m);
  EXPECT_EQ(m.end(), 3U); // 'o' at index 2, $ before the internal '\n' at index 3
}

TEST(region_spans_are_absolute)
{
  const auto m {real::regex("(\\w)(\\w+)").search("xx hello"sv, 3)};
  EXPECT(m);
  EXPECT_EQ(m.start(), 3U);
  EXPECT_EQ(m[1], "h"sv);
  EXPECT_EQ(m.start(1), 3U);
  EXPECT_EQ(m[2], "ello"sv);
  EXPECT_EQ(m.start(2), 4U);
}
