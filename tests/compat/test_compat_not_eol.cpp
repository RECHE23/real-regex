//! `match_not_eol` and `match_not_eow` on one regex_search / regex_match call run on REAL, over a rewrite of the
//! pattern built once: under `not_eol` a `$` holds at no end of the sequence (in multiline, still before a line
//! terminator), under `not_eow` a `\b` holds at no end and a `\B` does. A lookbehind, which std's grammar rejects,
//! pins that a call stayed on REAL; a construct the rewrite does not read leaves the call to std.
#include <cstddef>
#include <cstdio>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

#include <sciforge/test/framework.hpp>
#include "real/compat/std/regex.hpp"

namespace rc = real::compat;
namespace rx = real::compat::regex_constants;

namespace {

  struct span
  {
    long position {-1};
    long length   {-1};

    bool operator==(const span&) const = default;
  };

  span search(const std::string&  s,
              const rc::regex&    re,
              rx::match_flag_type f)
  {
    rc::smatch m;
    if (!rc::regex_search(s, m, re, f)) {
      return span {};
    }
    return span {.position = static_cast<long>(m.position(0)), .length = static_cast<long>(m.length(0))};
  }

  span std_search(const std::string&                    s,
                  const std::regex&                     re,
                  std::regex_constants::match_flag_type f)
  {
    std::smatch m;
    if (!std::regex_search(s, m, re, f)) {
      return span {};
    }
    return span {.position = static_cast<long>(m.position(0)), .length = static_cast<long>(m.length(0))};
  }

  const auto ml {rc::regex_constants::ECMAScript | rc::regex_constants::multiline};
} // namespace

TEST(compat_not_eol_and_not_eow_witnesses)
{
  EXPECT(search("a", rc::regex {"a$"}, rx::match_not_eol) == span {});
  EXPECT(search("a\na", rc::regex {"a$", ml}, rx::match_not_eol) == (span {0, 1}));
  EXPECT(search("a\ra", rc::regex {"a$", ml}, rx::match_not_eol) == (span {0, 1}));
  EXPECT(search("ba", rc::regex {"a$", ml}, rx::match_not_eol) == span {});
  EXPECT(search("a", rc::regex {R"(a\B)"}, rx::match_not_eow) == (span {0, 1}));
  EXPECT(search("a", rc::regex {R"(a\b)"}, rx::match_not_eow) == span {});
  EXPECT(search("a b", rc::regex {R"(a\b)"}, rx::match_not_eow) == (span {0, 1}));
  EXPECT(rc::regex_match(std::string {"a"}, rc::regex {R"(a\B)"}, rx::match_not_eow));
  EXPECT(!rc::regex_match(std::string {"a"}, rc::regex {"a$"}, rx::match_not_eol));
  EXPECT(search("a", rc::regex {R"(a\b$)"}, rx::match_not_eol | rx::match_not_eow) == span {});
  // In a class, `$` and `\b` are characters; escaped, `\$` is one too.
  EXPECT(search("a$", rc::regex {R"(a[$])"}, rx::match_not_eol) == (span {0, 2}));
  EXPECT(search("a$", rc::regex {R"(a\$)"}, rx::match_not_eol) == (span {0, 2}));
}

// On REAL: the lookbehind would make std throw.
TEST(compat_not_eol_stays_on_real)
{
  EXPECT(search("xa", rc::regex {"(?<=x)a$"}, rx::match_not_eol) == span {});
  EXPECT(search("xa\n", rc::regex {"(?<=x)a$", ml}, rx::match_not_eol) == (span {1, 1}));
  EXPECT(search("xa", rc::regex {R"((?<=x)a\B)"}, rx::match_not_eow) == (span {1, 1}));
  EXPECT(search("xa", rc::regex {R"((?<=x)a\B)"}, rx::match_not_eow | rx::match_not_null) == (span {1, 1}));
}

// A POSIX grammar goes to std: the rewrite reads ECMAScript, and a POSIX pattern is written in another grammar.
TEST(compat_not_eol_on_a_posix_grammar_is_std)
{
  const rc::regex  bre {R"(\(a\)$)", rc::regex_constants::basic};
  const std::regex ref {R"(\(a\)$)", std::regex::basic};
  EXPECT(bre.uses_real());
  EXPECT(bre.end_engine(true, false) == nullptr);
  for (const char* const subject : {"a", "ba", "a\n", "(a)"}) {
    const std::string s {subject};
    rc::smatch        m;
    std::smatch       want;
    EXPECT_EQ(rc::regex_search(s, m, bre, rx::match_not_eol),
              std::regex_search(s, want, ref, std::regex_constants::match_not_eol));
  }
}

// A pattern with nothing the flags change keeps its own engine; one the rewrite does not read goes to std.
TEST(compat_not_eol_variants)
{
  const rc::regex plain            {"ab"};
  EXPECT(plain.end_engine(true, true) == &std::get<real::regex>(plain.engine()));
  const rc::regex nested           {"(?=a$)a"}; // the rewrite would nest a lookaround in one: std
  EXPECT(nested.end_engine(true, false) == nullptr);
  EXPECT(search("a", nested, rx::match_not_eol) == span {});
  EXPECT(search("a\n", rc::regex {"(?=a$)a", ml}, rx::match_not_eol) == (span {0, 1}));
  const rc::regex          dollar  {"a$"};
  const real::regex* const variant {dollar.end_engine(true, false)};
  EXPECT(variant != nullptr);
  EXPECT(variant != &std::get<real::regex>(dollar.engine()));
  EXPECT(dollar.end_engine(true, false) == variant); // built once
  rc::regex copy;
  copy = dollar;                                     // a copy carries the variant built so far, or builds its own
  EXPECT(copy.end_engine(true, false) != nullptr);
}

// The host std agrees over every subject on {a, space, \n} up to four characters, under each flag set. libc++'s
// `\b` never holds on an attempt that starts at last, flags or not: those disagreements are counted apart, on libc++
// only. (A `\b` inside a lookahead is left out: both std libraries misread it, flags or not.)
TEST(compat_not_eol_agrees_with_std)
{
  const char* const        patterns[] {"$", "a$", "^a$", R"(\b)", R"(\B)", R"(a\b)", R"(a\B)", R"(\ba)", R"(\Ba)",
                                       R"(\b$)", "a*$", "(a)$|b", R"((?=a$)a)", "a(?!$)", R"(\w+\b)", "[$]|a$"};
  std::vector<std::string> subjects;
  const char               alphabet[] {'a', ' ', '\n'};
  for (unsigned len {0}, total {1}; len <= 4; ++len, total *= 3U) {
    for (unsigned k {0}; k < total; ++k) {
      std::string s;
      for (unsigned i {0}, x {k}; i < len; ++i, x /= 3U) {
        s += alphabet[x % 3U];
      }
      subjects.push_back(s);
    }
  }
  const rx::match_flag_type                   ours[] {rx::match_not_eol, rx::match_not_eow,
                                                      rx::match_not_eol | rx::match_not_eow,
                                                      rx::match_not_eol | rx::match_not_null,
                                                      rx::match_not_eow | rx::match_continuous};
  const std::regex_constants::match_flag_type theirs[] {
    std::regex_constants::match_not_eol, std::regex_constants::match_not_eow,
    std::regex_constants::match_not_eol | std::regex_constants::match_not_eow,
    std::regex_constants::match_not_eol | std::regex_constants::match_not_null,
    std::regex_constants::match_not_eow | std::regex_constants::match_continuous};
  std::size_t compared       {0};
  std::size_t diverged       {0};
  std::size_t libcxx_at_last {0}; // libc++'s `\b` never holding on an attempt at last
  for (const bool multiline : {false, true}) {
    for (const char* const p : patterns) {
      const rc::regex  re  {p, multiline ? ml : rc::regex_constants::ECMAScript};
      const std::regex ref {p, multiline ? std::regex::ECMAScript | std::regex::multiline : std::regex::ECMAScript};
      for (const std::string& s : subjects) {
        for (std::size_t f {0}; f < 5; ++f) {
          const span got  {search(s, re, ours[f])};
          const span want {std_search(s, ref, theirs[f])};
          #if defined(_LIBCPP_VERSION)
          const long last {static_cast<long>(s.size())};
          if (!(got == want) && std::string_view {p}.find('\\') != std::string_view::npos
              && (got.position == last || want.position == last)) {
            ++libcxx_at_last;
            ++compared;
            continue;
          }
#endif
          if (!(got == want)) {
            std::printf("/%s/ ml %d on \"%s\" flags %zu: (%ld,%ld), std (%ld,%ld)\n", p, static_cast<int>(multiline),
                        s.c_str(), f, got.position, got.length, want.position, want.length);
            ++diverged;
          }
          ++compared;
        }
      }
    }
  }
  EXPECT_EQ(compared, std::size_t {2} *16U * 121U * 5U);
  EXPECT_EQ(diverged, std::size_t {0});
#if defined(_LIBCPP_VERSION)
  EXPECT_EQ(libcxx_at_last, std::size_t {6});
#else
  EXPECT_EQ(libcxx_at_last, std::size_t {0});
#endif
}
