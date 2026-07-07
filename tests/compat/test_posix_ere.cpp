// PX1a+PX2a: POSIX Extended (ERE) grammar routed to REAL's linear engine with leftmost-LONGEST bounds (the
// POSIX semantics) instead of std's backtracker. This pins the bounds against std::regex(extended) — the
// authoritative oracle — and asserts the routing actually fired (posix_longest(), the teeth: a pattern that
// silently delegated to std would report false and this would still pass the bounds but fail the teeth).
#include <regex>
#include <string>

#include <sciforge/test/framework.hpp>
#include "real/std/regex.hpp"

namespace rc = real::compat;

namespace {
  // [start, end) of the leftmost match, or (-1,-1) for no match. Templated over the engine's search.
  template <typename Regex, typename Match, typename SearchFn>
  std::pair<long, long> bounds(const char * text,
                               const Regex& re,
                               SearchFn     search)
  {
    Match m;
    if (search(text, m, re)) {
      return {static_cast<long>(m.position(0)), static_cast<long>(m.position(0) + m.length(0))};
    }
    return {-1, -1};
  }
}

TEST(posix_ere_bounds_equal_std_and_route_to_real)
{
  struct testcase { const char* pat; const char* text; };
  const testcase cases[] {
    {.pat = "a|ab", .text = "ab"}, {.pat = "aabaa|b", .text = "aabaa"}, {.pat = "ab|a|abc", .text = "abc"},
    {.pat = "x*y|x*", .text = "xxxx"}, {.pat = "(a|b)+", .text = "abab"}, {.pat = "a{2,3}", .text = "aaaa"},
    {.pat = "[a-z]+", .text = "hello9"}, {.pat = "a.c", .text = "axc"}, {.pat = "^ab", .text = "ab"},
    {.pat = "[[:alpha:]]+", .text = "ab12"}, {.pat = "[[:digit:]]+", .text = "xx42yy"},
    {.pat = "[[:alpha:]][[:digit:]]", .text = "a9"}, {.pat = "[[:upper:]]+", .text = "aBCd"},
    {.pat = "[[:space:]]", .text = "a b"}, {.pat = "[[:alnum:]_]+", .text = "a_9-z"},
  };
  for (const testcase& c : cases) {
    const rc::regex  real_re {c.pat, rc::regex_constants::extended};
    const std::regex std_re  {c.pat, std::regex::extended};
    const auto       real_b  {bounds<rc::regex, rc::cmatch>(c.text, real_re,
                                                            [](const char* t, rc::cmatch& m, const rc::regex& r) { return rc::regex_search(t, m, r); })};
    const auto std_b         {bounds<std::regex, std::cmatch>(c.text, std_re,
                                                              [](const char* t, std::cmatch& m, const std::regex& r) { return std::regex_search(t, m, r); })};
    EXPECT(real_b == std_b);         // POSIX-longest bounds match the oracle
    EXPECT(real_re.posix_longest()); // the teeth: it ran on REAL's linear engine, not silently on std
  }
}

TEST(posix_ere_untranslatable_returns_nullopt)
{
  // The translator declines (→ std fallback) exactly on the constructs the two grammars read differently: an
  // ECMAScript-ism (\d — literal/undefined in ERE), an ambiguous brace, and an unknown/collating class.
  namespace d = real::compat::detail;
  EXPECT(!d::translate_ere("a\\d").has_value());
  EXPECT(!d::translate_ere("a{").has_value());
  EXPECT(!d::translate_ere("[[:foo:]]").has_value());
  EXPECT(!d::translate_ere("[[.a.]]").has_value());
  // and it accepts the translatable ones (a strict brace, a POSIX class, common productions)
  EXPECT(d::translate_ere("a{2,3}").has_value());
  EXPECT(d::translate_ere("[[:alpha:]]+").has_value());
  EXPECT(d::translate_ere("(a|b)*c").has_value());
}

TEST(posix_ere_is_leftmost_longest_not_first)
{
  // The distinguishing case: leftmost-first would give [0,1) (the `a` branch), POSIX gives [0,2) (the longer).
  const rc::regex real_re {"a|ab", rc::regex_constants::extended};
  rc::cmatch      m;
  EXPECT(rc::regex_search("ab", m, real_re));
  EXPECT(m.position(0) == 0 && m.length(0) == 2); // longest, per POSIX
}

TEST(posix_ere_iteration_equals_std)
{
  // The full sequence of match bounds under iteration equals std::sregex_iterator over std::regex(extended) —
  // each occurrence is leftmost-longest, driven on REAL's linear engine.
  struct testcase { const char* pat; const char* text; };
  const testcase cases[] {
    {.pat = "a|ab", .text = "ab xab yab"}, {.pat = "(ab|a)(c|bc)", .text = "abc abbc"},
    {.pat = "[[:digit:]]+", .text = "a12 b345 c6"}, {.pat = "aa*|a", .text = "aa aaa a"},
  };
  for (const testcase& c : cases) {
    const std::string s       {c.text};
    const rc::regex   real_re {c.pat, rc::regex_constants::extended};
    const std::regex  std_re  {c.pat, std::regex::extended};
    EXPECT(real_re.posix_longest() && real_re.uses_real_traversal()); // teeth: iterate on REAL, not std
    auto ri                   {rc::sregex_iterator(s.begin(), s.end(), real_re)};
    auto si                   {std::sregex_iterator(s.begin(), s.end(), std_re)};
    for (; ri != rc::sregex_iterator() && si != std::sregex_iterator(); ++ri, ++si) {
      EXPECT_EQ(ri->position(0), si->position(0));
      EXPECT_EQ(ri->length(0), si->length(0));
    }
    EXPECT_EQ(ri == rc::sregex_iterator(), si == std::sregex_iterator()); // same count
  }
}

TEST(posix_ere_nullable_iteration_delegates_to_std_but_matches)
{
  // A nullable ERE (`x*` can match empty) keeps its search on REAL-longest, but its iterate/replace delegate to
  // std (the empty-match traversal differs, exactly as on the ECMAScript path) — bounds still equal std.
  const std::string s       {"xxxy xx"};
  const rc::regex   real_re {"x*y|x*", rc::regex_constants::extended};
  const std::regex  std_re  {"x*y|x*", std::regex::extended};
  EXPECT(real_re.posix_longest() && !real_re.uses_real_traversal()); // search on REAL, iterate on std
  auto ri                   {rc::sregex_iterator(s.begin(), s.end(), real_re)};
  auto si                   {std::sregex_iterator(s.begin(), s.end(), std_re)};
  for (; ri != rc::sregex_iterator() && si != std::sregex_iterator(); ++ri, ++si) {
    EXPECT_EQ(ri->position(0), si->position(0));
    EXPECT_EQ(ri->length(0), si->length(0));
  }
  EXPECT_EQ(ri == rc::sregex_iterator(), si == std::sregex_iterator());
}

TEST(posix_ere_replace_equals_std)
{
  const std::string s       {"ab xab yab 12"};
  const rc::regex   real_re {"a|ab|[[:digit:]]+", rc::regex_constants::extended};
  const std::regex  std_re  {"a|ab|[[:digit:]]+", std::regex::extended};
  EXPECT(real_re.uses_real_traversal()); // teeth: replace runs on REAL
  EXPECT_EQ(rc::regex_replace(s, real_re, std::string("<$&>")),
            std::regex_replace(s, std_re, std::string("<$&>")));
  EXPECT_EQ(rc::regex_replace(s, real_re, std::string("X"), rc::regex_constants::format_first_only),
            std::regex_replace(s, std_re, std::string("X"), std::regex_constants::format_first_only));
}
