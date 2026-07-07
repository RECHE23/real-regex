// PX1b tranche 1: POSIX Basic (BRE) grammar routed to REAL's linear engine with leftmost-longest bounds, the
// same mechanism as extended (PX1a). BRE's distinctive shape is pinned here: \( \) group and \{n\} quantify,
// while bare ( ) { } | + ? — and ^/$ off the ends — are LITERALS; a backreference \1-\9 (std's residual value)
// and the ECMAScript-isms decline to std. Bounds pinned against std::regex(basic), the authoritative oracle.
#include <regex>
#include <string>

#include <sciforge/test/framework.hpp>
#include "real/std/regex.hpp"

namespace rc = real::compat;

TEST(posix_bre_bounds_equal_std_and_route_to_real)
{
  struct testcase { const char* pat; const char* text; };
  const testcase cases[] {
    {.pat = R"(\(ab\)*)", .text = "ababab"}, {.pat = R"(a\{2,3\})", .text = "aaaa"},
    {.pat = "a|b", .text = "a|b"},                                // | is a LITERAL in BRE
    {.pat = "a+b", .text = "a+b"},                                // + is a literal
    {.pat = "a?b", .text = "a?b"},                                // ? is a literal
    {.pat = "^ab", .text = "ab"}, {.pat = "ab$", .text = "ab"},   // ends: anchors (libs agree)
    {.pat = "[[:digit:]]*", .text = "42x"}, {.pat = R"(a\{2\}b)", .text = "aab"},
    {.pat = "a.c", .text = "axc"},
  };
  for (const testcase& c : cases) {
    const rc::regex  real_re {c.pat, rc::regex_constants::basic};
    const std::regex std_re  {c.pat, std::regex::basic};
    rc::cmatch       rm;
    std::cmatch      sm;
    const bool       rok {rc::regex_search(c.text, rm, real_re)};
    const bool       sok {std::regex_search(c.text, sm, std_re)};
    EXPECT_EQ(rok, sok);
    if (rok && sok) {
      EXPECT_EQ(rm.position(0), sm.position(0));
      EXPECT_EQ(rm.length(0), sm.length(0));
    }
    EXPECT(real_re.posix_longest()); // the teeth: it ran on REAL, not silently on std
  }
}

TEST(posix_bre_iteration_and_replace_equal_std)
{
  const std::string s       {"aa aaa a"};
  const rc::regex   real_re {R"(a\{1,2\})", rc::regex_constants::basic}; // a{1,2}
  const std::regex  std_re  {R"(a\{1,2\})", std::regex::basic};
  EXPECT(real_re.uses_real_traversal());                                 // non-nullable -> iterate/replace on REAL too
  auto ri                   {rc::sregex_iterator(s.begin(), s.end(), real_re)};
  auto si                   {std::sregex_iterator(s.begin(), s.end(), std_re)};
  for (; ri != rc::sregex_iterator() && si != std::sregex_iterator(); ++ri, ++si) {
    EXPECT_EQ(ri->position(0), si->position(0));
    EXPECT_EQ(ri->length(0), si->length(0));
  }
  EXPECT_EQ(ri == rc::sregex_iterator(), si == std::sregex_iterator());
  EXPECT_EQ(rc::regex_replace(s, real_re, std::string("<$&>")),
            std::regex_replace(s, std_re, std::string("<$&>")));
}

TEST(posix_bre_declines_backref_and_ecmascript_isms)
{
  namespace d = real::compat::detail;
  // A backreference \1-\9 is std's residual value — decline (nullopt). ECMAScript shorthands and a non-strict
  // \{ decline too. The escaped-group / interval / literal forms translate.
  EXPECT(!d::translate_bre(R"(\(a\)\1)").has_value());  // backreference
  EXPECT(!d::translate_bre(R"(a\d)").has_value());      // \d — no BRE meaning
  EXPECT(!d::translate_bre(R"(a\{2)").has_value());     // unterminated interval
  EXPECT(!d::translate_bre("*ab").has_value());         // leading * is a literal in POSIX -> decline
  EXPECT(!d::translate_bre("a^b").has_value());         // medial ^ — libstdc++/libc++ disagree -> decline
  EXPECT(!d::translate_bre("a$b").has_value());         // medial $ — same disagreement -> decline
  EXPECT(d::translate_bre(R"(\(ab\)*)").has_value());   // group + quantifier
  EXPECT(d::translate_bre("a|b").has_value());          // | literal -> \|
  EXPECT(d::translate_bre("[[:alpha:]]+").has_value()); // POSIX class ('+' here is a literal)
  // The backreference pattern still WORKS via the std fallback (uses_real() false, bounds correct).
  const rc::regex re {R"(\(a\)\1)", rc::regex_constants::basic, rc::policy::fallback};
  EXPECT(!re.uses_real());                              // delegated to std, which backtracks the backref
  EXPECT(rc::regex_search("aa", re));
}
