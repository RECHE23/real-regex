//! `regex_replace` under `format_sed` runs on REAL with sed's rules: `&` the whole match, a backslash and a digit a
//! group, a backslash and any other character that character, `$` an ordinary character, and a final lone backslash
//! kept or dropped as the native std does (libstdc++ and libc++ keep it, MS STL drops it), so the host std is the
//! oracle. A pattern of REAL's superset
//! (a lookbehind, which std's grammar rejects) pins that the substitution did not fall back to std, which would
//! throw instead of answering.
#include <cstddef>
#include <cstdio>
#include <iterator>
#include <regex>
#include <string>
#include <vector>

#include <sciforge/test/framework.hpp>
#include "real/compat/std/regex.hpp"

namespace rc = real::compat;
namespace rx = real::compat::regex_constants;

namespace {

  std::string sed(const std::string&  s,
                  const char        * pattern,
                  const char        * fmt,
                  rx::match_flag_type extra = rx::format_default)
  {
    return rc::regex_replace(s, rc::regex {pattern}, std::string {fmt}, rx::format_sed | extra);
  }

  // Every format over the characters the rules give a meaning to, up to four long.
  std::vector<std::string> sed_formats()
  {
    const char               alphabet[] {'&', '\\', '0', '1', '2', '9', 'a', '$', '`'};
    std::vector<std::string> formats    {""};
    std::size_t              from       {0};
    for (int len {1}; len <= 4; ++len) {
      const std::size_t to {formats.size()};
      for (std::size_t i {from}; i < to; ++i) {
        for (const char c : alphabet) {
          formats.push_back(formats[i] + c);
        }
      }
      from = to;
    }
    return formats;
  }
} // namespace

TEST(compat_format_sed_rules)
{
  EXPECT_EQ(sed("xaby", "(a)(b)", "[&]"), std::string {"x[ab]y"});
  EXPECT_EQ(sed("xaby", "(a)(b)", R"([\0])"), std::string {"x[ab]y"});
  EXPECT_EQ(sed("xaby", "(a)(b)", R"(\2\1)"), std::string {"xbay"});
  EXPECT_EQ(sed("xaby", "(a)(b)", R"(\&\\)"), std::string {R"(x&\y)"});
  EXPECT_EQ(sed("xaby", "(a)(b)", R"(\n\t\x)"), std::string {"xntxy"});
  // A final lone backslash: kept by libstdc++ and libc++, dropped by MS STL; the native std decides.
  EXPECT_EQ(sed("xaby", "(a)(b)", "<\\"), std::string {real::compat::detail::std_sed_keeps_final_backslash() ? "x<\\y" : "x<y"});
  EXPECT_EQ(sed("xaby", "(a)(b)", "$&$1$0"), std::string {"x$ab$1$0y"});
  EXPECT_EQ(sed("xaby", "(a)(b)", R"(\12)"), std::string {"xa2y"});
  EXPECT_EQ(sed("xay", "(a)(b)?", R"([\2])"), std::string {"x[]y"});
  EXPECT_EQ(sed("xay", "(a)", R"([\5])"), std::string {"x[]y"});
  EXPECT_EQ(sed("aXa", "a", "<&>", rx::format_first_only), std::string {"<a>Xa"});
  EXPECT_EQ(sed("aXa", "a", "<&>", rx::format_no_copy), std::string {"<a><a>"});
  // An empty match: the traversal is REAL's, the expander sed's.
  EXPECT_EQ(sed("ab", "x*", "-&-"), std::string {"--a--b--"});
}

// On REAL: std's grammar rejects the lookbehind, so a fallback would throw.
TEST(compat_format_sed_stays_on_real)
{
  EXPECT_EQ(sed("ab", "(?<=a)b", R"([&\0])"), std::string {"a[bb]"});
  EXPECT_EQ(sed("ab", "(?<=a)b", "$0&"), std::string {"a$0b"}); // `$0` is no reason for std under sed
  std::string       out;
  const std::string s {"ab"};
  rc::regex_replace(std::back_inserter(out), s.begin(), s.end(), rc::regex {"(?<=a)(b)"}, std::string {R"(\1\1)"},
                    rx::format_sed);
  EXPECT_EQ(out, std::string {"abb"});
}

// The host std agrees on every format of the table, for every match of every pattern on every subject.
TEST(compat_format_sed_agrees_with_std)
{
  const char* const              patterns[] {"(a)(b)?", "(a)|(b)", "a", "((a)(b))", "(b)(a)*"};
  const char* const              subjects[] {"xaby", "ab", "", "ba", "a", "bab"};
  const std::vector<std::string> formats    {sed_formats()};
  std::size_t                    compared   {0};
  std::size_t                    diverged   {0};
  for (const char* const p : patterns) {
    const rc::regex  re  {p};
    const std::regex ref {p};
    for (const char* const s : subjects) {
      const std::string subject {s};
      for (const std::string& fmt : formats) {
        const std::string got  {rc::regex_replace(subject, re, fmt, rx::format_sed)};
        const std::string want {std::regex_replace(subject, ref, fmt, std::regex_constants::format_sed)};
        if (got != want) {
          std::printf("/%s/ on \"%s\" fmt [%s]: [%s], std [%s]\n", p, s, fmt.c_str(), got.c_str(), want.c_str());
          ++diverged;
        }
        ++compared;
      }
    }
  }
  EXPECT_EQ(compared, std::size_t {5} *6U * 7381U);
  EXPECT_EQ(diverged, std::size_t {0});
}
