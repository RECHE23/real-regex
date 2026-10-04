//! `match_not_null` on one regex_search / regex_match call runs on REAL: the leftmost position where a non-empty
//! match starts, and there the match the priority order prefers among the non-empty ones, as libstdc++ and libc++
//! both do. A pattern that cannot match empty ignores the flag; a nullable capturing group under a quantifier keeps
//! std, whose last iteration differs. A lookbehind, which std's grammar rejects, pins that a call stayed on REAL.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <regex>
#include <string>
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
              std::size_t         from,
              const rc::regex&    re,
              rx::match_flag_type f)
  {
    rc::smatch m;
    if (!rc::regex_search(s.begin() + static_cast<std::ptrdiff_t>(from), s.end(), m, re, f)) {
      return span {};
    }
    return span {.position = static_cast<long>(m.position(0)), .length = static_cast<long>(m.length(0))};
  }

  span std_search(const std::string&                    s,
                  std::size_t                           from,
                  const std::regex&                     re,
                  std::regex_constants::match_flag_type f)
  {
    std::smatch m;
    if (!std::regex_search(s.begin() + static_cast<std::ptrdiff_t>(from), s.end(), m, re, f)) {
      return span {};
    }
    return span {.position = static_cast<long>(m.position(0)), .length = static_cast<long>(m.length(0))};
  }

  // libc++ ignores match_prev_avail for `^` and never lets `\b` hold on an attempt at last: no oracle there.
  bool std_honors_prev_avail()
  {
    const std::string ba {"ba"};
    const std::string a  {"a"};
    return !std::regex_search(ba.begin() + 1, ba.end(), std::regex {"^a"}, std::regex_constants::match_prev_avail)
           && std::regex_search(a.end(), a.end(), std::regex {R"(\b)"}, std::regex_constants::match_prev_avail);
  }
} // namespace

// The witnesses libstdc++ and libc++ agree on.
TEST(compat_not_null_takes_the_first_non_empty_match)
{
  EXPECT(search("ab", 0, rc::regex {"x*|b"}, rx::match_not_null) == (span {1, 1}));
  EXPECT(search("aa", 0, rc::regex {"a*?"}, rx::match_not_null) == (span {0, 1}));
  EXPECT(search("ab", 0, rc::regex {"(?:|ab|a)"}, rx::match_not_null) == (span {0, 2}));
  EXPECT(search("ab", 0, rc::regex {"(?=a)|b"}, rx::match_not_null) == (span {1, 1}));
  EXPECT(search("abc", 0, rc::regex {"x*"}, rx::match_not_null) == span {});
  EXPECT(!rc::regex_match(std::string {}, rc::regex {"a*"}, rx::match_not_null));
  EXPECT(rc::regex_match(std::string {"aa"}, rc::regex {"a*"}, rx::match_not_null));
  // With match_continuous: non-empty and at first.
  EXPECT(search("ab", 0, rc::regex {"x*|a"}, rx::match_not_null | rx::match_continuous) == (span {0, 1}));
  EXPECT(search("ab", 0, rc::regex {"x*|b"}, rx::match_not_null | rx::match_continuous) == span {});
  // Past the lazy DFA's threshold: the rule is the VM's, never a DFA's.
  const std::string long_subject {std::string(1000, 'a') + "b"};
  EXPECT(search(long_subject, 0, rc::regex {"x*|b"}, rx::match_not_null) == (span {1000, 1}));
  EXPECT(search(long_subject, 0, rc::regex {"a*?"}, rx::match_not_null) == (span {0, 1}));
}

// On REAL: the lookbehind would make std throw. A pattern that cannot match empty ignores the flag.
TEST(compat_not_null_stays_on_real)
{
  EXPECT(search("ab", 0, rc::regex {"(?<=a)b?"}, rx::match_not_null) == (span {1, 1}));
  EXPECT(search("ab", 0, rc::regex {"(?<=a)x*"}, rx::match_not_null) == span {});
  EXPECT(search("ab", 0, rc::regex {"(?<=a)b"}, rx::match_not_null) == (span {1, 1}));
  EXPECT(search("ab", 1, rc::regex {"(?<=a)b?"}, rx::match_not_null | rx::match_prev_avail) == (span {0, 1}));
  EXPECT(rc::regex_search(std::string {"ab"}, rc::regex {"(?<=a)b?"}, rx::match_not_null));
}

// The flag judges the whole match, never a lookahead's own body: `(?=)a` matches "a", which is not empty. libstdc++
// and libc++ both answer no match here, applying the flag to the empty lookahead, so std is no oracle for it.
TEST(compat_not_null_spares_an_empty_lookahead_body)
{
  EXPECT(search("aaa", 0, rc::regex {"(?=)a"}, rx::match_not_null) == (span {0, 1}));
  EXPECT(search("ab", 0, rc::regex {"(?=a*)a"}, rx::match_not_null) == (span {0, 1}));
  EXPECT(search("ab", 0, rc::regex {"(?=)"}, rx::match_not_null) == span {}); // the whole match is still empty
}

// A pattern that cannot match empty ignores the flag, and keeps its search: the lazy DFA, which the search that
// refuses empty matches, a VM walk, never takes (`\w+\d{5}` over 1 MB: 2.1 ms against 26.6 ms, arm64, 2026-10-01).
TEST(compat_not_null_on_a_pattern_that_cannot_match_empty_keeps_the_dfa)
{
  const std::string text(4096, 'a');
  const rc::regex   re {R"(\w+\d{5})"};
  real::detail::tally(real::detail::counter::dfa_leases_taken) = 0;
  EXPECT(search(text, 0, re, rx::match_not_null) == span {});
  EXPECT(real::detail::tally(real::detail::counter::dfa_leases_taken).load() > 0U);
  real::detail::tally(real::detail::counter::dfa_leases_taken) = 0;
  EXPECT(search(text, 0, rc::regex {"x*|b"}, rx::match_not_null) == span {});
  EXPECT_EQ(real::detail::tally(real::detail::counter::dfa_leases_taken).load(), std::uint64_t {0}); // the control: the VM walk takes none
}

// A nullable capturing group under a quantifier keeps std: its last iteration is std's, not REAL's.
TEST(compat_not_null_on_a_nullable_captured_repeat_is_std)
{
  const std::string s {"aa"};
  rc::smatch        m;
  std::smatch       want;
  EXPECT_EQ(rc::regex_search(s, m, rc::regex {"(|a)*"}, rx::match_not_null),
            std::regex_search(s, want, std::regex {"(|a)*"}, std::regex_constants::match_not_null));
  EXPECT_EQ(m.length(0), want.length(0));
  EXPECT_EQ(m.str(1), want.str(1));
}

// The host std agrees over every subject on {a, b, space} up to four characters, every start, under not_null
// alone and with continuous and prev_avail.
TEST(compat_not_null_agrees_with_std)
{
  const char* const patterns[] {"x*", "a*", "a*?", "b*|a", "x*|b", "(?:|ab|a)", "(?=a)|b", "a?b?", R"(\b)", R"(\B|a)",
                                R"(\b\w*)", "(a)?(b)?", "(?:a|)+?", "a{0,2}", "(?!a)", "$|a", "(?:b|)a?"};
  std::vector<std::string> subjects;
  const char               alphabet[] {'a', 'b', ' '};
  for (unsigned len {0}, total {1}; len <= 4; ++len, total *= 3U) {
    for (unsigned k {0}; k < total; ++k) {
      std::string s;
      for (unsigned i {0}, x {k}; i < len; ++i, x /= 3U) {
        s += alphabet[x % 3U];
      }
      subjects.push_back(s);
    }
  }
  const rx::match_flag_type                   ours[]   {rx::match_not_null, rx::match_not_null | rx::match_continuous,
                                                        rx::match_not_null | rx::match_prev_avail};
  const std::regex_constants::match_flag_type theirs[] {
    std::regex_constants::match_not_null, std::regex_constants::match_not_null | std::regex_constants::match_continuous,
    std::regex_constants::match_not_null | std::regex_constants::match_prev_avail};
  const bool  prev_oracle {std_honors_prev_avail()};
  std::size_t compared    {0};
  std::size_t diverged    {0};
  for (const char* const p : patterns) {
    const rc::regex  re  {p};
    const std::regex ref {p};
    for (const std::string& s : subjects) {
      for (std::size_t from {0}; from <= s.size(); ++from) {
        for (std::size_t f {0}; f < 3; ++f) {
          if (f == 2 && (from == 0 || !prev_oracle)) {
            continue; // prev_avail needs a character before first, and a std that honors it
          }
          const span got  {search(s, from, re, ours[f])};
          const span want {std_search(s, from, ref, theirs[f])};
          if (!(got == want)) {
            std::printf("/%s/ on \"%s\" from %zu flags %zu: (%ld,%ld), std (%ld,%ld)\n", p, s.c_str(), from, f,
                        got.position, got.length, want.position, want.length);
            ++diverged;
          }
          ++compared;
        }
      }
    }
  }
  EXPECT(compared > 10000U);
  EXPECT_EQ(diverged, std::size_t {0});
}
