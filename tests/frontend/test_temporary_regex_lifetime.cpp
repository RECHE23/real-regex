// A result from a TEMPORARY regex must resolve group names for as long as it lives.
//
// `search`/`match`/`fullmatch` are callable on a temporary, and stay so: the one-expression form is
// safe and ubiquitous. But the result also borrowed the pattern text and the named-group table from
// that temporary, so storing it and then asking for a group BY NAME read freed memory --
// AddressSanitizer named it a heap-use-after-free through `dynamic_program::~dynamic_program`, and
// without a sanitizer it is a silent wrong answer. `find_iter`/`find_all` were already deleted on an
// rvalue regex for the same lifetime reason; the single attempts were not, and could not be, so the
// result takes its own copy of the name context instead.
//
// These tests fail (or trip ASan) without `detach_from_regex`. The subject is a named lvalue
// throughout: the SEPARATE rule that the subject must outlive the result is unchanged here.
#include <string>
#include <type_traits>
#include <vector>

#include <sciforge/test/framework.hpp>
#include "real/real.hpp"

namespace {

  //! The subject every case searches; a named lvalue, so only the regex is temporary.
  std::string subject()
  {
    return std::string {"xxxx-abc-zz"};
  }
} // namespace

// The plain case: store the result of a temporary regex, then look a group up by name.
TEST(named_lookup_survives_the_temporary_regex_it_came_from)
{
  const std::string text {subject()};
  const auto        m    {real::regex {"(?<word>[a-z]+)-"}.search(text)};

  EXPECT(m.matched());
  EXPECT_EQ(m.group_index("word"), 1U);
  EXPECT_EQ(std::string {m["word"]}, std::string {"xxxx"});
  EXPECT_EQ(m.start("word"), 0U);
  EXPECT_EQ(m.end("word"), 4U);
  EXPECT_EQ(m.group_index("absent"), real::npos);
}

// The documented alias must NAME the type `real::regex::search` returns on a live regex. It was
// spelled over `std::vector<std::size_t>` while the dynamic policy's slots are SBO-backed, so this
// declaration did not compile at all -- the alias documented itself as a result type it could not
// hold. A temporary regex yields the OWNING twin instead, which is a distinct type on purpose.
TEST(the_match_result_alias_names_what_search_returns)
{
  const std::string        text {subject()};
  const real::regex        re   {"(?<word>[a-z]+)-"};
  const real::match_result m    {re.search(text)};

  EXPECT(m.matched());
  EXPECT_EQ(std::string {m["word"]}, std::string {"xxxx"});

  // Both public aliases DERIVE their type rather than restating it, so they cannot drift from what
  // they document -- which is exactly how the old spelling stayed wrong from the first commit.
  static_assert(std::is_same_v<real::match_result, real::regex::result_type>);
  static_assert(std::is_same_v<real::owning_match_result, real::regex::owning_result_type>);
  static_assert(std::is_same_v<decltype(re.search(text)), real::match_result>);
  static_assert(!std::is_same_v<real::regex::result_type, real::regex::owning_result_type>);
  static_assert(std::is_same_v<decltype(real::regex {"a"}.search(text)), real::owning_match_result>);
  // On the compile-time policy there is nothing to own, so the two collapse into one type.
  using static_re = real::static_regex<"a">;
  static_assert(std::is_same_v<static_re::result_type, static_re::owning_result_type>);
}

// Copying a detached result must carry the name context with it, including past the original.
TEST(a_copy_of_a_detached_result_resolves_names_too)
{
  const std::string                            text {subject()};
  std::vector<real::regex::owning_result_type> kept;
  {
    const auto m {real::regex {"(?<word>[a-z]+)-"}.search(text)};
    kept.push_back(m);
    kept.push_back(m);
  }
  EXPECT_EQ(kept.size(), 2U);
  for (const real::regex::owning_result_type& m : kept) {
    EXPECT_EQ(std::string {m["word"]}, std::string {"xxxx"});
  }
}

// Every rvalue overload detaches, not just the string_view one: the region-aware form and the
// string-literal form route through the same helper.
TEST(the_region_aware_and_literal_rvalue_overloads_detach_as_well)
{
  const std::string text {subject()};

  const auto region      {real::regex {"(?<w>[a-z]+)"}.search(text, 5, 8)};
  EXPECT(region.matched());
  EXPECT_EQ(std::string {region["w"]}, std::string {"abc"});

  const auto literal {real::regex {"(?<w>[a-z]+)-"}.match("abc-def")};
  EXPECT(literal.matched());
  EXPECT_EQ(std::string {literal["w"]}, std::string {"abc"});

  const auto whole {real::regex {"(?<w>[a-z]+)"}.fullmatch("abc")};
  EXPECT(whole.matched());
  EXPECT_EQ(std::string {whole["w"]}, std::string {"abc"});
}

// With no named groups there is nothing to copy, so nothing is allocated -- and the pattern view is
// cleared rather than left dangling at a pattern that is gone.
TEST(an_unnamed_pattern_detaches_without_owning_anything)
{
  const std::string text {subject()};
  const auto        m    {real::regex {"[a-z]+"}.search(text)};

  EXPECT(m.matched());
  EXPECT_EQ(std::string {m[0]}, std::string {"xxxx"});
  EXPECT_EQ(m.group_index("word"), real::npos);
  EXPECT_EQ(m.start("word"), real::npos);
  EXPECT_EQ(std::string {m["word"]}, std::string {});
}

// The lvalue path is untouched by all of this: it still borrows, which is free and correct while the
// regex is alive, and that is the contract the documentation states.
TEST(the_lvalue_path_still_borrows_and_still_resolves)
{
  const std::string text {subject()};
  const real::regex re   {"(?<word>[a-z]+)-"};
  const auto        m    {re.search(text)};

  EXPECT(m.matched());
  EXPECT_EQ(m.group_index("word"), 1U);
  EXPECT_EQ(std::string {m["word"]}, std::string {"xxxx"});
}

// A temporary `static_regex` borrows safely: its pattern and name table are `static constexpr`, so
// they outlive every result by construction and the owner stays empty (and literal, which is what
// lets a result be built during constant evaluation).
TEST(a_temporary_static_regex_needs_no_owner)
{
  const std::string text {subject()};
  const auto        m    {real::static_regex<"(?<w>[a-z]+)"> {}.search(text)};

  EXPECT(m.matched());
  EXPECT_EQ(std::string {m["w"]}, std::string {"xxxx"});
  static_assert(std::is_empty_v<real::detail::borrowed_names>);
}

// `search_longest` was the one single attempt left out. `search`, `match` and `fullmatch` each grew
// a `const&&` twin that detaches; `search_longest` stayed `const` with no ref-qualifier at all, so
// the SAME expression that is safe for `search` returned a borrowing result from a temporary regex.
// ASan named it: heap-use-after-free through `dynamic_program::~dynamic_program`, reached from the
// named lookup, at the exact shape the file's opening comment describes. Without a sanitizer it is
// `string_view::substr` throwing `out_of_range`, or a silent wrong answer.
//
// The whole-match span was never the problem and is not the test: it points into the SUBJECT, which
// is a named lvalue here. What died with the temporary is the pattern text and the named-group
// table, so every case below asks for a group BY NAME — the only question whose answer needs them.
TEST(search_longest_detaches_from_a_temporary_regex_like_its_siblings)
{
  const std::string text {subject()};

  // The plain form, which is the one ASan tripped on.
  const auto plain {real::regex {"(?<word>[a-z]+)-"}.search_longest(text)};
  EXPECT(plain.matched());
  EXPECT_EQ(plain.group_index("word"), 1U);
  EXPECT_EQ(std::string {plain["word"]}, std::string {"xxxx"}); // leftmost `[a-z]+-` in the subject
  EXPECT_EQ(plain.group_index("absent"), real::npos);

  // The region form, and the `const char*` forwarders: a literal must reach the detaching twin, not
  // fall back to the borrowing one, which is what a forwarder without its own `const&&` would do.
  const auto region {real::regex {"(?<w>[a-z]+)"}.search_longest(text, 5, 8)};
  EXPECT(region.matched());
  EXPECT_EQ(std::string {region["w"]}, std::string {"abc"});

  const auto literal {real::regex {"(?<w>[a-z]+)-"}.search_longest("abc-def")};
  EXPECT(literal.matched());
  EXPECT_EQ(std::string {literal["w"]}, std::string {"abc"});

  const auto literal_region {real::regex {"(?<w>[a-z]+)"}.search_longest("xx-abc-zz", 3, 6)};
  EXPECT(literal_region.matched());
  EXPECT_EQ(std::string {literal_region["w"]}, std::string {"abc"});

  // An unnamed pattern has nothing to copy, and must not dangle at a pattern that is gone.
  const auto unnamed {real::regex {"[a-z]+"}.search_longest(text)};
  EXPECT(unnamed.matched());
  EXPECT_EQ(unnamed.group_index("word"), real::npos);

  // Which type each value category yields, asserted rather than described: an rvalue regex owns its
  // name context, an lvalue keeps borrowing — that borrow is free and correct while the regex lives.
  //
  // FOUR parameter lists, so eight assertions. The first version of this block had six, covering
  // `string_view`, `string_view`+pos and the bare literal but not literal+pos — a list written and
  // not swept to its end, which is the third time that shape has shown up in this area. The missing
  // list had behavioural cover (the region+literal case above does look a group up by name, so a
  // forwarder that fell back to the borrowing overload would throw there), and behaviour is not the
  // same claim: it holds only for a caller who dereferences a NAME, while the type is the property.
  const real::regex live {"(?<word>[a-z]+)-"};
  static_assert(std::is_same_v<decltype(real::regex {"a"}.search_longest(text)),
                               real::owning_match_result>);
  static_assert(std::is_same_v<decltype(real::regex {"a"}.search_longest(text, 0)),
                               real::owning_match_result>);
  static_assert(std::is_same_v<decltype(real::regex {"a"}.search_longest("a")),
                               real::owning_match_result>);
  static_assert(std::is_same_v<decltype(real::regex {"a"}.search_longest("a", 0)),
                               real::owning_match_result>);
  static_assert(std::is_same_v<decltype(live.search_longest(text)), real::match_result>);
  static_assert(std::is_same_v<decltype(live.search_longest(text, 0)), real::match_result>);
  static_assert(std::is_same_v<decltype(live.search_longest("a")), real::match_result>);
  static_assert(std::is_same_v<decltype(live.search_longest("a", 0)), real::match_result>);
  EXPECT_EQ(live.search_longest(text).group_index("word"), 1U);
}
