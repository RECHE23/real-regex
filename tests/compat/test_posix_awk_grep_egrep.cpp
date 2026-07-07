// PX1b tranche 2: the three remaining POSIX grammars on REAL's linear engine. awk is ERE plus the C-escapes
// (its \b is BACKSPACE, not a word boundary — the inverse of the ERE decline — plus \a \n \t \r \f \v, \/ \"
// and 1-3 digit octal); grep is BRE and egrep is ERE, each with a newline reading as a top-level alternation
// of the lines. Bounds pinned against std::regex(<grammar>) — the authoritative oracle, both std libraries
// agreeing on every escape and on the newline-alternation.
#include <regex>
#include <string>

#include <sciforge/test/framework.hpp>
#include "real/std/regex.hpp"

namespace rc = real::compat;

namespace {
  bool real_hit(const std::string&                      pat,
                const std::string&                      text,
                rc::regex_constants::syntax_option_type g)
  {
    const rc::regex re {pat, g};
    rc::smatch      m;
    return rc::regex_search(text, m, re);
  }

  // [start,end) of the leftmost match on both engines must agree (lvalue text — no dangling match).
  void same_bounds(const std::string&                      pat,
                   const std::string&                      text,
                   rc::regex_constants::syntax_option_type rg,
                   std::regex::flag_type                   sg)
  {
    const rc::regex  real_re {pat, rg};
    const std::regex std_re  {pat, sg};
    rc::smatch       rm;
    std::smatch      sm;
    const bool       rok {rc::regex_search(text, rm, real_re)};
    const bool       sok {std::regex_search(text, sm, std_re)};
    EXPECT_EQ(rok, sok);
    if (rok && sok) {
      EXPECT_EQ(rm.position(0), sm.position(0));
      EXPECT_EQ(rm.length(0), sm.length(0));
    }
    EXPECT(real_re.posix_longest()); // the teeth: it ran on REAL, not silently on std
  }
}

TEST(posix_awk_c_escapes_equal_std)
{
  namespace ff = rc::regex_constants;
  same_bounds("a\\bc", std::string("a\x08" "c"), ff::awk, std::regex::awk); // \b = BACKSPACE
  same_bounds("\\101", "A", ff::awk, std::regex::awk);                      // octal \101 = 'A'
  same_bounds("\\12", std::string("\x0a"), ff::awk, std::regex::awk);       // octal \12 = newline
  same_bounds("a\\nb", "a\nb", ff::awk, std::regex::awk);                   // \n
  same_bounds("\\/", "/", ff::awk, std::regex::awk);                        // \/ -> literal slash
  same_bounds("\\a", std::string("\x07"), ff::awk, std::regex::awk);        // \a = BEL
}

TEST(posix_awk_backslash_b_is_backspace_not_word_boundary)
{
  // THE awk trap, pinned: `\b` is BACKSPACE (0x08), not a word boundary — the inverse of the ERE decline,
  // where `\b` has no meaning and declines.
  const rc::regex re {"a\\bc", rc::regex_constants::awk};
  EXPECT(re.posix_longest());                                                    // ran on REAL
  EXPECT(real_hit("a\\bc", std::string("a\x08" "c"), rc::regex_constants::awk)); // matches "a<BS>c"
  EXPECT(!real_hit("a\\bc", "abc", rc::regex_constants::awk));                   // NOT "abc" (no boundary sense)
}

TEST(posix_grep_egrep_newline_is_top_level_alternation)
{
  namespace ff = rc::regex_constants;
  // A newline reads as a top-level alternation of the lines; "ab\na" on "ab" takes the longer "ab" branch —
  // the leftmost-longest acid that grep/BRE otherwise cannot express (BRE has no `|`).
  same_bounds("ab\na", "ab", ff::egrep, std::regex::egrep);  // [0,2)
  same_bounds("ab\na", "xa", ff::egrep, std::regex::egrep);  // the "a" branch, [1,2)
  same_bounds("ab\na", "ab", ff::grep, std::regex::grep);    // grep = BRE lines
  same_bounds("a\nb\nc", "b", ff::egrep, std::regex::egrep); // three branches
  same_bounds("a|b\nc", "c", ff::egrep, std::regex::egrep);  // an ERE line (with its own `|`) plus a newline
}

TEST(posix_egrep_is_redos_safe_and_declines_cleanly)
{
  namespace ff = rc::regex_constants;
  namespace d  = real::compat::detail;
  // (a+)+b in egrep runs on REAL's linear engine — std would blow up on a long non-matching run.
  const rc::regex   redos {"(a+)+b", ff::egrep};
  const std::string s(2000, 'a');                               // 2000 'a', no trailing 'b' — the ReDoS trigger
  rc::smatch        m;
  EXPECT(redos.posix_longest());
  EXPECT(!rc::regex_search(s, m, redos));                       // linear, no match, no catastrophic backtracking
  // Declines to std: an awk ECMAScript-ism, and an empty grep branch (a blank line).
  EXPECT(!d::translate_ere("a\\d", /*awk=*/ true).has_value());
  EXPECT(!d::translate_posix("ab\n\na", ff::grep).has_value()); // blank middle branch -> std
}

TEST(posix_all_five_grammars_are_redos_safe)
{
  // Every POSIX grammar's pathological pattern runs in LINEAR time on REAL; std::regex would blow up
  // catastrophically (libstdc++ exponential backtracking, libc++ a complexity throw). std's behaviour is
  // DOCUMENTED, not executed here — running it would hang or throw. The proof is that these all COMPLETE on a
  // large non-matching input: a backtracker cannot. Each routes to REAL (posix_longest) and returns no match.
  namespace ff = rc::regex_constants;
  const std::string s(50000, 'a'); // 50k 'a', no trailing 'b' — the ReDoS trigger
  struct tc { const char* pat; ff::syntax_option_type g; };
  const tc cases[] {
    {.pat = "(a+)+b", .g = ff::extended}, {.pat = R"(\(a*\)*b)", .g = ff::basic},
    {.pat = "(a+)+b", .g = ff::awk}, {.pat = R"(\(a*\)*b)", .g = ff::grep},
    {.pat = "(a+)+b", .g = ff::egrep},
  };
  for (const tc& c : cases) {
    const rc::regex re {c.pat, c.g};
    EXPECT(re.posix_longest());          // ran on REAL's linear engine, not delegated to std
    rc::smatch m;
    EXPECT(!rc::regex_search(s, m, re)); // completes with no match — a backtracker would hang here
  }
}
