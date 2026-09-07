// Region-aware match/search/fullmatch: pos (the VM start offset — NOT a slice, so
// zero-width anchors still see the absolute position) and endpos (a truncated view).
// Byte offsets; subjects are ASCII so byte == char. These pin the C++ engine overloads
// that back the Python binding's pos/endpos (whose parity vs re is tested separately).
#include <string>
#include <string_view>
#include <vector>

#include <sciforge/test/framework.hpp>
#include "real/real.hpp"

using namespace std::string_view_literals;

namespace {

  //! Whether a region form accepts a bare string literal (no `sv`). False while the call is ambiguous.
  template <typename R>
  concept region_takes_literal = requires(const R& re) {
    re.search("xax", std::size_t {1});
    re.match("xax", std::size_t {1});
    re.fullmatch("xax", std::size_t {1});
    re.find_iter("xax", std::size_t {1});
    re.search("xax", std::size_t {1}, std::size_t {3});
    re.find_iter("xax", std::size_t {1}, std::size_t {3});
  };

  //! Whether a region form accepts a TEMPORARY std::string. Must stay false: the result outlives it.
  template <typename R>
  concept region_takes_temporary_string = requires(const R& re) {
    re.search(std::string("xax"), std::size_t {1});
  };

  //! Same question for the other three, so the guard is asserted door by door and not by sample.
  template <typename R>
  concept match_takes_temporary_string = requires(const R& re) {
    re.match(std::string("xax"), std::size_t {1});
  };
  template <typename R>
  concept fullmatch_takes_temporary_string = requires(const R& re) {
    re.fullmatch(std::string("xax"), std::size_t {1});
  };
  template <typename R>
  concept find_iter_takes_temporary_string = requires(const R& re) {
    re.find_iter(std::string("xax"), std::size_t {1});
  };
} // namespace

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

TEST(region_find_iter_iterates_within_region)
{
  const real::regex             rx("\\w+");
  std::vector<std::string_view> got;
  for (const auto& m : rx.find_iter("foo bar baz qux"sv, 4, 11)) {
    got.push_back(m[0]); // region [4,11) = "bar baz"
  }
  EXPECT_EQ(got.size(), 2U);
  EXPECT_EQ(got[0], "bar"sv);
  EXPECT_EQ(got[1], "baz"sv);
}

TEST(region_find_iter_stops_mid_word_at_endpos)
{
  const real::regex             rx("\\w+");
  std::vector<std::string_view> got;
  for (const auto& m : rx.find_iter("hello world"sv, 0, 8)) {
    got.push_back(m[0]); // region [0,8) = "hello wo" → the second word is truncated
  }
  EXPECT_EQ(got.size(), 2U);
  EXPECT_EQ(got[0], "hello"sv);
  EXPECT_EQ(got[1], "wo"sv);
}

TEST(region_find_iter_one_arg_unchanged_and_default_endpos)
{
  const real::regex rx("\\w+");
  std::size_t       n {};
  for (const auto& m : rx.find_iter("a b c"sv)) { // 1-arg: whole text, unchanged
    (void) m;
    ++n;
  }
  EXPECT_EQ(n, 3U);
  std::vector<std::string_view> got;
  for (const auto& m : rx.find_iter("a b c"sv, 2)) { // from pos 2, default endpos
    got.push_back(m[0]);
  }
  EXPECT_EQ(got.size(), 2U);
  EXPECT_EQ(got[0], "b"sv);
}

TEST(region_find_iter_anchors_in_region)
{
  // ^ MULTILINE within the region holds at pos only if text[pos-1] == '\n'.
  const real::regex             rx("^\\w+", real::flags::multiline);
  std::vector<std::string_view> got;
  for (const auto& m : rx.find_iter("foo\nbar\nbaz"sv, 4, 11)) {
    got.push_back(m[0]); // ^ at 4 (after \n) and at 8 (after \n)
  }
  EXPECT_EQ(got.size(), 2U);
  EXPECT_EQ(got[0], "bar"sv);
  EXPECT_EQ(got[1], "baz"sv);
}

// An INVERTED region (pos past endpos) used to hand `pos` to the VM past the end of the truncated
// subject, and a substr deep inside threw std::out_of_range -- which escaped the engine as
// `string_view::substr`, a standard-library exception surfacing from a search. Python `re` returns
// no match for the same call, and pos == endpos still yields the zero-width match, so the fix is a
// guard and not a rejection.
TEST(inverted_region_yields_no_match_and_throws_nothing)
{
  const real::regex      nullable("x*");
  const real::regex      literal("a");
  const std::string_view text                          {"abc"};

  const std::pair<std::size_t, std::size_t> inverted[] {{1, 0}, {2, 0}, {3, 1}};
  for (const auto& [pos, end] : inverted) {
    try {
      EXPECT(!nullable.search(text, pos, end));
      EXPECT(!literal.search(text, pos, end));
      EXPECT(!nullable.match(text, pos, end));
      EXPECT(!nullable.fullmatch(text, pos, end));
    }
    catch (const std::exception&) {
      EXPECT(false);  // nothing may escape: that was the defect
    }
  }

  // pos == endpos is NOT inverted -- the zero-width match belongs there.
  for (const std::size_t at : {std::size_t {0}, std::size_t {1}, std::size_t {3}}) {
    const auto m = nullable.search(text, at, at);
    EXPECT(m);
    EXPECT_EQ(m.start(), at);
    EXPECT_EQ(m.end(), at);
  }
}

// The overload set had a hole exactly where this file's own spelling hid it. Every subject above is
// written `"…"sv`, so a bare string literal never reached a REGION form — and with `pos`, a
// `const char*` converts to `std::string_view` AND to `std::string`, two user-defined conversions of
// equal rank. The second candidate is the `const std::string&&` overload deleted to stop a temporary
// from dangling, so `re.search("xax", 1)` was AMBIGUOUS, and the compiler named the deleted overload
// — which reads as "your text would dangle" about a literal that has static storage duration and
// cannot.
//
// The no-pos forms were never affected: they already carry a `const char*` overload. `split` carries
// one WITH its second argument, and that is the model the region forms now follow — forward to the
// `string_view` overload, nothing else.
TEST(a_string_literal_reaches_the_region_forms_without_sv)
{
  static_assert(region_takes_literal<real::regex>,
                "a bare literal must reach every region form; ambiguity here is the defect");

  const real::regex rx("\\w+");

  // Two arguments: the shape that did not compile.
  EXPECT_EQ(rx.search("foo bar baz", 4)[0], "bar"sv);
  EXPECT_EQ(rx.match("foo bar", 4)[0], "bar"sv);
  EXPECT(!rx.match("foo bar", 3));            // pos 3 is a space
  EXPECT(rx.fullmatch("foobar", 0));
  EXPECT(!rx.fullmatch("foo bar", 0));

  // Three arguments: pos and endpos.
  EXPECT_EQ(rx.search("foo bar baz", 4, 7)[0], "bar"sv);
  EXPECT_EQ(rx.match("foo bar baz", 4, 7)[0], "bar"sv);
  EXPECT(rx.fullmatch("foo bar", 4, 7));

  // The literal must agree with the `sv` spelling it replaces, or the forwarding is wrong.
  EXPECT_EQ(rx.search("foo bar baz", 4)[0], rx.search("foo bar baz"sv, 4)[0]);
  EXPECT_EQ(rx.search("foo bar baz", 4, 7)[0], rx.search("foo bar baz"sv, 4, 7)[0]);

  // find_iter is used in a range-for, which is where the missing overload was reached from.
  std::vector<std::string_view> seen;
  for (const auto& m : rx.find_iter("foo bar baz", 4)) {
    seen.push_back(m[0]);
  }
  EXPECT_EQ(seen.size(), 2U);
  EXPECT_EQ(seen[0], "bar"sv);
  EXPECT_EQ(seen[1], "baz"sv);
  seen.clear();
  for (const auto& m : rx.find_iter("foo bar baz", 4, 7)) {
    seen.push_back(m[0]);
  }
  EXPECT_EQ(seen.size(), 1U);
  EXPECT_EQ(seen[0], "bar"sv);

  // On a TEMPORARY regex the single attempts stay callable and detach their result, as they do
  // without `pos`; only find_iter is deleted there, because a range-for would outlive the regex.
  EXPECT_EQ(real::regex("\\w+").search("foo bar", 4)[0], "bar"sv);
  EXPECT_EQ(real::regex("\\w+").match("foo bar", 4)[0], "bar"sv);
  EXPECT(real::regex("\\w+").fullmatch("foo bar", 4, 7));
  EXPECT_EQ(real::regex("(\\w)(\\w+)").search("foo bar", 4).str(2), "ar"sv); // the detached name/pattern path
}

// What the deletion was always aimed at, and what must survive the fix: a TEMPORARY std::string,
// whose buffer dies at the end of the full expression while the result still points into it. A
// literal is static and safe; a temporary is not. Asserted as non-callability rather than in prose,
// so an overload that re-opens the hole fails here instead of in a caller's undefined behaviour —
// and asserted door by door, because a contract written once has already been found true at some
// doors and false at others in this codebase.
TEST(a_temporary_string_still_cannot_reach_the_region_forms)
{
  static_assert(!region_takes_temporary_string<real::regex>,
                "search(std::string&&, pos) must stay deleted: the result borrows the text");
  static_assert(!match_takes_temporary_string<real::regex>, "match(std::string&&, pos) must stay deleted");
  static_assert(!fullmatch_takes_temporary_string<real::regex>, "fullmatch(std::string&&, pos) must stay deleted");
  static_assert(!find_iter_takes_temporary_string<real::regex>, "find_iter(std::string&&, pos) must stay deleted");

  // A NAMED string is not a temporary and stays callable — the guard is about lifetime, not type.
  // An lvalue cannot bind to `const std::string&&` at all, so only the string_view overload is
  // viable here and the call was never ambiguous either.
  const std::string  named {"foo bar baz"};
  const real::regex  rx("\\w+");
  EXPECT_EQ(rx.search(named, 4)[0], "bar"sv);
  EXPECT_EQ(rx.search(named, 4, 7)[0], "bar"sv);
}
