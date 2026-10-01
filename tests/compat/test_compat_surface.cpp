//! The drop-in names what std names: code that swaps `std::` for `real::compat::` keeps compiling. Each member
//! below is pinned at compile time against its std counterpart, and the ones with behaviour of their own --
//! `assign`, the comparisons, `match_results::format` -- are compared with the host std.
#include <compare>
#include <cstddef>
#include <cstdio>
#include <initializer_list>
#include <iterator>
#include <regex>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <sciforge/test/framework.hpp>
#include "real/compat/std/regex.hpp"

namespace rc = real::compat;

namespace {

  // basic_regex: the class constants, the constructors and every assign / operator= std has.
  template <typename R>
  concept regex_surface = requires(R & m, const R& c, const std::string& str, const char* cs) {
    typename R::value_type;
    typename R::traits_type;
    typename R::string_type;
    typename R::flag_type;
    typename R::locale_type;
    R::icase;
    R::nosubs;
    R::optimize;
    R::collate;
    R::ECMAScript;
    R::basic;
    R::extended;
    R::awk;
    R::grep;
    R::egrep;
    R::multiline;
    R(cs);
    R(cs, std::size_t {1});
    R(str);
    R(str.begin(), str.end());
    R({'a', 'b'});
    c.mark_count();
    c.flags();
    m.swap(m);
    m.assign(c);
    m.assign(R {});
    m.assign(cs);
    m.assign(cs, std::size_t {1});
    m.assign(str);
    m.assign(str, R::icase);
    m.assign(str.begin(), str.end());
    m.assign({'a'});
    m = str;
    m = cs;
    m = {'a'};
  };
  static_assert(regex_surface<rc::regex>);
  static_assert(regex_surface<std::regex>);

  // sub_match: the comparisons with another sub_match, a string, a C string and a character, both ways.
  template <typename S>
  concept sub_match_surface = requires(const S& c, const std::string& str, const char* cs, char ch) {
    c.compare(c);
    c.compare(str);
    c.compare(cs);
    c == c;
    c != c;
    c < c;
    c <=> c;
    c == str;
    str == c;
    c < str;
    str < c;
    c == cs;
    cs == c;
    c < cs;
    cs < c;
    c == ch;
    ch == c;
    c < ch;
    ch < c;
  };
  static_assert(sub_match_surface<rc::ssub_match>);
  static_assert(sub_match_surface<std::ssub_match>);

  // match_results: the members and free functions std has.
  template <typename M>
  concept match_results_surface = requires(M & m, const M& c, std::string& out, const std::string& fmt) {
    typename M::allocator_type;
    c.max_size();
    c.get_allocator();
    m.swap(m);
    swap(m, m);
    c == c;
    c != c;
    c.format(fmt);
    c.format("$1");
    c.format(std::back_inserter(out), fmt);
    c.format(std::back_inserter(out), fmt.data(), fmt.data() + fmt.size());
  };
  static_assert(match_results_surface<rc::smatch>);
  static_assert(match_results_surface<std::smatch>);

  // The iterators and the error: the token iterator's field lists, the error codes and their constructor.
  template <typename T, typename R>
  concept token_surface = requires(T & t, const std::string& s, const R& re) {
    T(s.begin(), s.end(), re, 0);
    T(s.begin(), s.end(), re, std::vector<int> {1, 2});
    T(s.begin(), s.end(), re, {1, 2});
    T(s.begin(), s.end(), re, std::declval<const int (&)[2]>());
    t++;
    t->str();
  };
  static_assert(token_surface<rc::sregex_token_iterator, rc::regex>);
  static_assert(token_surface<std::sregex_token_iterator, std::regex>);
  // A temporary regex would dangle, for every field form, as std deletes them.
  static_assert(!std::is_constructible_v<rc::sregex_token_iterator, std::string::const_iterator, std::string::const_iterator,
                                         rc::regex &&, const int (&)[2]>);
  static_assert(!std::is_constructible_v<rc::sregex_token_iterator, std::string::const_iterator, std::string::const_iterator,
                                         rc::regex &&, std::initializer_list<int>>);
  static_assert(rc::regex_constants::error_paren == std::regex_constants::error_paren);
  static_assert(rc::regex_constants::error_stack == std::regex_constants::error_stack);

  int sign(std::strong_ordering o)
  {
    if (o < 0) {
      return -1;
    }
    return o > 0 ? 1 : 0;
  }

  int sign(int v)
  {
    if (v < 0) {
      return -1;
    }
    return v > 0 ? 1 : 0;
  }
} // namespace

TEST(compat_surface_regex_constants_and_assign)
{
  static_assert(rc::regex::icase == rc::regex_constants::icase);
  static_assert(rc::regex::multiline == rc::regex_constants::multiline);
  EXPECT(rc::regex_search(std::string {"xAb"}, rc::regex {"ab", rc::regex::icase}));
  rc::regex re {"a"};
  re = std::string {"b+"};
  EXPECT(rc::regex_match(std::string {"bb"}, re));
  re = "c";
  EXPECT(rc::regex_match(std::string {"c"}, re));
  re = {'d', '?'};
  EXPECT(rc::regex_match(std::string {""}, re));
  const std::string pattern {"(e)(f)"};
  re.assign(pattern.begin(), pattern.end());
  EXPECT_EQ(re.mark_count(), std::size_t {2});
  re.assign("ghi", 2, rc::regex::icase);
  EXPECT(rc::regex_match(std::string {"GH"}, re));
  EXPECT(re.flags() == rc::regex::icase);
  // An invalid pattern leaves the regex as it was, as std's assign does.
  bool threw {false};
  try {
    re.assign("(");
  }
  catch (const rc::regex_error&) {
    threw = true;
  }
  EXPECT(threw);
  EXPECT(rc::regex_match(std::string {"gh"}, re));
  EXPECT(re.flags() == rc::regex::icase);
  EXPECT_EQ(re.mark_count(), std::size_t {0});
  EXPECT(rc::regex_search(std::string {"xGH"}, re, rc::regex_constants::match_not_eol)); // on std: its pattern too
  // assign keeps the policy: a backreference stays rejected under strict and falls back under fallback.
  rc::regex fallback {"a", rc::regex_constants::ECMAScript, rc::policy::fallback};
  fallback.assign(R"((a)\1)");
  EXPECT(fallback.uses_fallback());
  EXPECT(rc::regex_match(std::string {"aa"}, fallback));
  rc::regex strict   {"a"};
  bool      rejected {false};
  try {
    strict.assign(R"((a)\1)");
  }
  catch (const rc::regex_error&) {
    rejected = true;
  }
  EXPECT(rejected);
  EXPECT(strict.uses_real());
  rc::regex copy;
  copy.assign(strict);
  EXPECT(rc::regex_match(std::string {"a"}, copy));
  const rc::regex braced {{'x', 'y'}};
  EXPECT(rc::regex_match(std::string {"xy"}, braced));
}

// The comparisons order as std's do, over every pair of short texts.
TEST(compat_surface_sub_match_comparisons_as_std)
{
  const std::string            subject {"abba ab b a"};
  const rc::regex              words   {R"(\w+|\s)"};
  const std::regex             ref     {R"(\w+|\s)"};
  std::vector<rc::ssub_match>  ours;
  std::vector<std::ssub_match> theirs;
  for (rc::sregex_iterator it {subject.begin(), subject.end(), words}, end; it != end; ++it) {
    ours.push_back((*it)[0]);
  }
  for (std::sregex_iterator it {subject.begin(), subject.end(), ref}, end; it != end; ++it) {
    theirs.push_back((*it)[0]);
  }
  EXPECT_EQ(ours.size(), theirs.size());
  const std::string probes[] {"", "a", "ab", "abba", "b", " ", "abc"};
  std::size_t       diverged {0};
  for (std::size_t i {0}; i < ours.size() && i < theirs.size(); ++i) {
    for (std::size_t j {0}; j < ours.size() && j < theirs.size(); ++j) {
      diverged += sign(ours[i] <=> ours[j]) != sign(theirs[i].compare(theirs[j])) ? 1U : 0U;
      diverged += (ours[i] < ours[j]) != (theirs[i] < theirs[j]) ? 1U : 0U;
    }
    for (const std::string& p : probes) {
      diverged += (ours[i] < p) != (theirs[i] < p) ? 1U : 0U;
      diverged += (p < ours[i]) != (p < theirs[i]) ? 1U : 0U;
      diverged += (ours[i] == p.c_str()) != (theirs[i] == p.c_str()) ? 1U : 0U;
      diverged += (ours[i] < p.c_str()) != (theirs[i] < p.c_str()) ? 1U : 0U;
      diverged += sign(ours[i].compare(p.c_str())) != sign(theirs[i].compare(p.c_str())) ? 1U : 0U;
    }
    for (const char ch : {'a', 'b', ' ', 'c'}) {
      diverged += (ours[i] == ch) != (theirs[i] == ch) ? 1U : 0U;
      diverged += (ours[i] < ch) != (theirs[i] < ch) ? 1U : 0U;
      diverged += (ch < ours[i]) != (ch < theirs[i]) ? 1U : 0U;
    }
  }
  EXPECT_EQ(diverged, std::size_t {0});
}

// Equality of results is std's: not ready, ready and empty, or the same text everywhere.
TEST(compat_surface_match_results_equality_as_std)
{
  const std::string        subjects[] {"ab", "xab", "b", "abab"};
  const char       *       patterns[] {"a(b)", "(a)?b", "q"};
  std::vector<rc::smatch>  ours;
  std::vector<std::smatch> theirs;
  ours.emplace_back();
  theirs.emplace_back();
  for (const std::string& s : subjects) {
    for (const char* const p : patterns) {
      ours.emplace_back();
      theirs.emplace_back();
      rc::regex_search(s, ours.back(), rc::regex {p});
      std::regex_search(s, theirs.back(), std::regex {p});
    }
  }
  std::size_t diverged {0};
  for (std::size_t i {0}; i < ours.size(); ++i) {
    for (std::size_t j {0}; j < ours.size(); ++j) {
      diverged += (ours[i] == ours[j]) != (theirs[i] == theirs[j]) ? 1U : 0U;
      diverged += (ours[i] != ours[j]) != (theirs[i] != theirs[j]) ? 1U : 0U;
    }
  }
  EXPECT_EQ(diverged, std::size_t {0});
  rc::smatch a {ours[1]};
  rc::smatch b {ours[2]};
  swap(a, b);
  EXPECT(a == ours[2]);
  EXPECT(b == ours[1]);
  a.swap(b);
  EXPECT(a == ours[1]);
  rc::smatch not_ready;
  a.swap(not_ready);
  EXPECT(!a.ready());
  EXPECT(not_ready.ready());
  EXPECT(not_ready == ours[1]);
  EXPECT(a.max_size() > 0U);
  EXPECT(a.get_allocator() == std::allocator<rc::ssub_match> {});
}

// format() expands as std's does, ECMAScript and sed, for every format over the characters with a meaning.
TEST(compat_surface_format_as_std)
{
  const char               alphabet[] {'$', '&', '`', '\'', '0', '1', '2', '\\', 'a'};
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
  const std::string subject    {"xaby"};
  const char*       patterns[] {"(a)(b)", "(a)(c)?b", "(q)?a"};
  std::size_t       compared   {0};
  std::size_t       diverged   {0};
  for (const char* const p : patterns) {
    rc::smatch  m;
    std::smatch ref;
    EXPECT(rc::regex_search(subject, m, rc::regex {p}));
    EXPECT(std::regex_search(subject, ref, std::regex {p}));
    for (const std::string& fmt : formats) {
      for (const auto flag : {std::regex_constants::format_default, std::regex_constants::format_sed}) {
        const auto        ours {flag == std::regex_constants::format_sed ? rc::regex_constants::format_sed
                                                                         : rc::regex_constants::format_default};
        const std::string got  {m.format(fmt, ours)};
        const std::string want {ref.format(fmt, flag)};
        if (got != want) {
          std::printf("/%s/ fmt [%s] sed %d: [%s], std [%s]\n", p, fmt.c_str(), static_cast<int>(flag != 0), got.c_str(), want.c_str());
          ++diverged;
        }
        ++compared;
      }
    }
  }
  EXPECT_EQ(compared, std::size_t {3} *7381U * 2U);
  EXPECT_EQ(diverged, std::size_t {0});
  rc::smatch m;
  rc::regex_search(subject, m, rc::regex {"(a)(b)"});
  EXPECT_EQ(m.format("[$2$1]"), std::string {"[ba]"});
  std::string       out;
  const std::string fmt {"<$&>"};
  m.format(std::back_inserter(out), fmt.data(), fmt.data() + fmt.size());
  EXPECT_EQ(out, std::string {"<ab>"});
}

// The C-array field list is the vector's; a code alone gives std's message.
TEST(compat_surface_token_array_and_error_code)
{
  const std::string subject  {"a1 b2 c3"};
  const rc::regex   re       {R"((\w)(\d))"};
  const int         fields[] {2, 1};
  std::string       from_array;
  std::string       from_vector;
  for (rc::sregex_token_iterator it {subject.begin(), subject.end(), re, fields}, end; it != end; ++it) {
    from_array += it->str();
  }
  for (rc::sregex_token_iterator it {subject.begin(), subject.end(), re, std::vector<int> {2, 1}}, end; it != end; ++it) {
    from_vector += it->str();
  }
  EXPECT_EQ(from_array, std::string {"1a2b3c"});
  EXPECT_EQ(from_array, from_vector);
  const rc::regex_error error {rc::regex_constants::error_paren};
  EXPECT(error.code() == std::regex_constants::error_paren);
  EXPECT_EQ(std::string {error.what()}, std::string {std::regex_error(std::regex_constants::error_paren).what()});
}
