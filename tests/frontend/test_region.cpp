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

  // The leftmost-longest twins. Same predicate as their leftmost-first siblings -- the subject must
  // outlive the result -- asked arity by arity, because `find_iter_longest` carries DEFAULTS on
  // `pos`/`endpos` while its deleted rvalue-regex overload took exactly three parameters, so arities
  // 1 and 2 slipped past it onto the `const&` overload, and `const X&` binds to an rvalue.

  //! Literals must reach both longest forms at every arity. False while a delete has no forwarder.
  template <typename R>
  concept longest_takes_literal = requires(const R& re) {
    re.search_longest("xax");
    re.search_longest("xax", std::size_t {1});
    re.search_longest("xax", std::size_t {1}, std::size_t {3});
    re.find_iter_longest("xax");
    re.find_iter_longest("xax", std::size_t {1});
    re.find_iter_longest("xax", std::size_t {1}, std::size_t {3});
  };

  //! A TEMPORARY subject on either longest form. Must all be false: the result borrows the text.
  template <typename R>
  concept search_longest_takes_temporary = requires(const R& re) {
    re.search_longest(std::string("xax"));
  };
  template <typename R>
  concept search_longest_region_takes_temporary = requires(const R& re) {
    re.search_longest(std::string("xax"), std::size_t {1});
  };
  template <typename R>
  concept find_iter_longest_takes_temporary = requires(const R& re) {
    re.find_iter_longest(std::string("xax"));
  };
  template <typename R>
  concept find_iter_longest_region_takes_temporary = requires(const R& re) {
    re.find_iter_longest(std::string("xax"), std::size_t {1});
  };

  //! `find_iter_longest` on a TEMPORARY REGEX, at each arity: a range-for would outlive the regex.
  template <typename R>
  concept find_iter_longest_on_temporary_regex_1 = requires(R && re) {
    std::move(re).find_iter_longest("xax");
  };
  template <typename R>
  concept find_iter_longest_on_temporary_regex_2 = requires(R && re) {
    std::move(re).find_iter_longest("xax", std::size_t {1});
  };
  template <typename R>
  concept find_iter_longest_on_temporary_regex_3 = requires(R && re) {
    std::move(re).find_iter_longest("xax", std::size_t {1}, std::size_t {3});
  };
  //! The same three through a `string_view`, which is how a non-literal subject arrives.
  template <typename R>
  concept find_iter_longest_sv_on_temporary_regex_1 = requires(R && re) {
    std::move(re).find_iter_longest(std::string_view {"xax"});
  };
  template <typename R>
  concept find_iter_longest_sv_on_temporary_regex_2 = requires(R && re) {
    std::move(re).find_iter_longest(std::string_view {"xax"}, std::size_t {1});
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

// The leftmost-longest twins carried the OPPOSITE defect of the one 17d555c fixed next door: not a
// guard too wide, a guard absent. `search_longest` returns a borrowing result and `find_iter_longest`
// returns a range over the subject, yet neither had the `const std::string&&` deletion its
// leftmost-first sibling has — so a temporary subject compiled and the result pointed into a buffer
// that died at the end of the full expression. Undefined behaviour, accepted silently.
//
// `find_iter_longest` had a second, narrower hole in the same place: its deleted rvalue-regex
// overload took exactly three parameters, while the callable one carries defaults. So arities 1 and
// 2 never reached the deletion and bound to the `const&` overload instead, because `const X&` binds
// to an rvalue quite happily. `regex("a").find_iter_longest("xax")` compiled and the range outlived
// its regex.
//
// The forwarders and the deletions arrive TOGETHER, which is the lesson from the sibling wagon: a
// `const std::string&&` deletion with no `const char*` forwarder makes a bare literal AMBIGUOUS,
// since `const char*` reaches `std::string_view` and `std::string` by two user-defined conversions
// of equal rank. Before this change a literal compiled only because it had no competitor — the
// absence of a rival, not a guarantee.
TEST(the_longest_forms_take_a_literal_and_refuse_a_temporary)
{
  static_assert(longest_takes_literal<real::regex>,
                "a literal must reach both longest forms at every arity, forwarders included");
  static_assert(!search_longest_takes_temporary<real::regex>,
                "search_longest(std::string&&) must be deleted: the result borrows the text");
  static_assert(!search_longest_region_takes_temporary<real::regex>,
                "search_longest(std::string&&, pos) must be deleted too");
  static_assert(!find_iter_longest_takes_temporary<real::regex>,
                "find_iter_longest(std::string&&) must be deleted: the range borrows the text");
  static_assert(!find_iter_longest_region_takes_temporary<real::regex>,
                "find_iter_longest(std::string&&, pos) must be deleted too");

  // A temporary REGEX cannot hand out a range, at any arity and by either subject spelling.
  static_assert(!find_iter_longest_on_temporary_regex_1<real::regex>, "arity 1 slipped past the delete");
  static_assert(!find_iter_longest_on_temporary_regex_2<real::regex>, "arity 2 slipped past the delete");
  static_assert(!find_iter_longest_on_temporary_regex_3<real::regex>, "arity 3 must stay refused");
  static_assert(!find_iter_longest_sv_on_temporary_regex_1<real::regex>, "string_view, arity 1");
  static_assert(!find_iter_longest_sv_on_temporary_regex_2<real::regex>, "string_view, arity 2");

  const real::regex rx("\\w+");

  // Literals answer, and answer the SAME as the `sv` spelling they replace — a forwarder that drops
  // or reorders an argument compiles and is still wrong, so this is checked at runtime as well.
  EXPECT_EQ(rx.search_longest("foo bar baz")[0], rx.search_longest("foo bar baz"sv)[0]);
  EXPECT_EQ(rx.search_longest("foo bar baz", 4)[0], rx.search_longest("foo bar baz"sv, 4)[0]);
  EXPECT_EQ(rx.search_longest("foo bar baz", 4, 7)[0], rx.search_longest("foo bar baz"sv, 4, 7)[0]);
  EXPECT_EQ(rx.search_longest("foo bar baz", 4)[0], "bar"sv);
  EXPECT_EQ(rx.search_longest("foo bar baz", 4, 7)[0], "bar"sv);

  std::vector<std::string_view> lit;
  std::vector<std::string_view> view;
  for (const auto& m : rx.find_iter_longest("foo bar baz", 4)) {
    lit.push_back(m[0]);
  }
  for (const auto& m : rx.find_iter_longest("foo bar baz"sv, 4)) {
    view.push_back(m[0]);
  }
  EXPECT_EQ(lit.size(), 2U);
  EXPECT_EQ(lit, view);
  EXPECT_EQ(lit[0], "bar"sv);
  lit.clear();
  for (const auto& m : rx.find_iter_longest("foo bar baz", 4, 7)) {
    lit.push_back(m[0]);
  }
  EXPECT_EQ(lit.size(), 1U);
  EXPECT_EQ(lit[0], "bar"sv);

  // A NAMED string is an lvalue, cannot bind to `const std::string&&`, and must stay callable: the
  // guard is about lifetime, not about the type.
  const std::string named {"foo bar baz"};
  EXPECT_EQ(rx.search_longest(named, 4)[0], "bar"sv);
  EXPECT_EQ(rx.search_longest(named, 4, 7)[0], "bar"sv);
  std::size_t counted {0};
  for (const auto& m : rx.find_iter_longest(named, 4)) {
    static_cast<void>(m);
    ++counted;
  }
  EXPECT_EQ(counted, 2U);

  // `search_longest` stays callable on a temporary regex, exactly as `search` does — the deletion
  // being added here is about the SUBJECT, not the regex. (Whether its result should detach the
  // name tables is a separate question and deliberately not touched.)
  EXPECT_EQ(real::regex("\\w+").search_longest("foo bar", 4)[0], "bar"sv);
}
