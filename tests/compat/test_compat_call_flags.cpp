//! `match_continuous` and `match_prev_avail` on one regex_search / regex_match call run on REAL: a match anchored at
//! `first`, and a region search from `first` over a view that starts one character before it. Each witness below
//! says what the standard says ([re.matchflag]); a pattern of REAL's superset (a lookbehind, which std's grammar
//! rejects) pins that the call did not fall back to std, which would throw instead of answering.
#include <cstddef>
#include <cstdio>
#include <deque>
#include <iterator>
#include <list>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

#include <sciforge/test/framework.hpp>
#include "real/compat/std/regex.hpp"

namespace rc = real::compat;
namespace rx = real::compat::regex_constants;

namespace {

  // Position of the match relative to `first`, -1 for none; the call is on [s.begin() + from, s.end()).
  long search_at(const std::string&  s,
                 std::size_t         from,
                 const rc::regex&    re,
                 rx::match_flag_type f)
  {
    rc::smatch m;
    const auto first {s.begin() + static_cast<std::ptrdiff_t>(from)};
    return rc::regex_search(first, s.end(), m, re, f) ? static_cast<long>(m.position(0)) : -1L;
  }

  long std_search_at(const std::string&                    s,
                     std::size_t                           from,
                     const std::regex&                     re,
                     std::regex_constants::match_flag_type f)
  {
    std::smatch m;
    const auto  first {s.begin() + static_cast<std::ptrdiff_t>(from)};
    return std::regex_search(first, s.end(), m, re, f) ? static_cast<long>(m.position(0)) : -1L;
  }

  // Every subject over {a, b, space, \n} up to four characters.
  std::vector<std::string> short_subjects()
  {
    std::vector<std::string> subjects;
    const char               alphabet[] {'a', 'b', ' ', '\n'};
    for (unsigned len {0}, total {1}; len <= 4; ++len, total *= 4U) {
      for (unsigned k {0}; k < total; ++k) {
        std::string s;
        for (unsigned i {0}, x {k}; i < len; ++i, x /= 4U) {
          s += alphabet[x % 4U];
        }
        subjects.push_back(s);
      }
    }
    return subjects;
  }

  // One pattern against the host std over every subject, start and flag set.
  struct agreement
  {
    std::size_t compared       {0};
    std::size_t diverged       {0};
    std::size_t libcxx_caret   {0}; // libc++ ignoring prev_avail for `^`
    std::size_t libcxx_at_last {0}; // libc++'s `\b` never holding on an attempt that starts at last

    void add(const char                    * p,
             const rc::regex&                re,
             const std::regex&               ref,
             const std::vector<std::string>& subjects)
    {
      const rx::match_flag_type                   ours[]   {rx::match_continuous, rx::match_prev_avail,
                                                            rx::match_continuous | rx::match_prev_avail};
      const std::regex_constants::match_flag_type theirs[] {
        std::regex_constants::match_continuous, std::regex_constants::match_prev_avail,
        std::regex_constants::match_continuous | std::regex_constants::match_prev_avail};
      for (const std::string& s : subjects) {
        for (std::size_t from {0}; from <= s.size(); ++from) {
          for (std::size_t f {0}; f < 3; ++f) {
            const bool prev {(ours[f] & rx::match_prev_avail) != 0U};
            if (from == 0 && prev) {
              continue; // prev_avail promises a character before first
            }
            ++compared;
            const long got  {search_at(s, from, re, ours[f])};
            const long want {std_search_at(s, from, ref, theirs[f])};
            if (got == want) {
              continue;
            }
#if defined(_LIBCPP_VERSION)
            if (p[0] == '^' && prev) {
              ++libcxx_caret;
              continue;
            }
            const long last {static_cast<long>(s.size() - from)};
            if (std::string_view {p}.find("\\b") != std::string_view::npos || std::string_view {p}.find("\\B") != std::string_view::npos) {
              if (got == last || want == last) {
                ++libcxx_at_last;
                continue;
              }
            }
#endif
            std::printf("/%s/ on \"%s\" from %zu flags %zu: %ld, std %ld\n", p, s.c_str(), from, f, got, want);
            ++diverged;
          }
        }
      }
    }
  };
} // namespace

// match_continuous: the match starts at first, or there is none.
TEST(compat_continuous_anchors_the_search_at_first)
{
  const std::string s {"ba"};
  EXPECT_EQ(search_at(s, 0, rc::regex {"a"}, rx::match_continuous), -1L);
  EXPECT_EQ(search_at(s, 0, rc::regex {"b"}, rx::match_continuous), 0L);
  rc::smatch m;
  EXPECT(rc::regex_search(s, m, rc::regex {"a*"}, rx::match_continuous));
  EXPECT_EQ(m.length(0), 0L);
  const std::string abca {"abca"};
  EXPECT(rc::regex_search(abca, m, rc::regex {"a|b"}, rx::match_continuous));
  EXPECT_EQ(m.position(0), 0L);
  EXPECT(!m.prefix().matched);
  EXPECT_EQ(m.suffix().str(), std::string {"bca"});
  EXPECT(!rc::regex_search(abca.begin() + 2, abca.end(), rc::regex {"a|b"}, rx::match_continuous)); // 'c'
  // A whole-sequence match starts at first anyway.
  EXPECT(rc::regex_match(s, rc::regex {"ba"}, rx::match_continuous));
  // On REAL: std's grammar rejects the lookbehind.
  EXPECT_EQ(search_at(s, 0, rc::regex {"(?<!x)b"}, rx::match_continuous), 0L);
  EXPECT_EQ(search_at(s, 0, rc::regex {"(?<!x)a"}, rx::match_continuous), -1L);
}

// match_prev_avail: the character before first is the context of `\b`, `\B`, a multiline `^` and a lookbehind,
// and `^` outside multiline does not hold at first.
TEST(compat_prev_avail_reads_the_character_before_first)
{
  EXPECT_EQ(search_at("ba", 1, rc::regex {R"(\ba)"}, rx::match_prev_avail), -1L);
  EXPECT_EQ(search_at(" a", 1, rc::regex {R"(\ba)"}, rx::match_prev_avail), 0L);
  const auto ml {rc::regex_constants::ECMAScript | rc::regex_constants::multiline};
  EXPECT_EQ(search_at("b\na", 2, rc::regex {"^a", ml}, rx::match_prev_avail), 0L);
  EXPECT_EQ(search_at("b\ra", 2, rc::regex {"^a", ml}, rx::match_prev_avail), 0L);
  EXPECT_EQ(search_at("bba", 2, rc::regex {"^a", ml}, rx::match_prev_avail), -1L);
  // libstdc++ and the standard; libc++ ignores prev_avail for `^` here and matches.
  EXPECT_EQ(search_at("ba", 1, rc::regex {"^a"}, rx::match_prev_avail), -1L);
  const std::string ba {"ba"};
  EXPECT(rc::regex_match(ba.begin() + 1, ba.end(), rc::regex {R"(\Ba)"}, rx::match_prev_avail));
  EXPECT(!rc::regex_match(ba.begin() + 1, ba.end(), rc::regex {R"(\ba)"}, rx::match_prev_avail));
  // The flags it voids: not_bol and not_bow change nothing under it.
  EXPECT_EQ(search_at("b\na", 2, rc::regex {"^a", ml}, rx::match_prev_avail | rx::match_not_bol), 0L);
  EXPECT_EQ(search_at(" a", 1, rc::regex {R"(\ba)"}, rx::match_prev_avail | rx::match_not_bow), 0L);
  EXPECT_EQ(search_at("ba", 1, rc::regex {"(?<=b)a"}, rx::match_prev_avail | rx::match_not_bol | rx::match_not_bow), 0L);
  // On REAL: the lookbehind sees the context.
  EXPECT_EQ(search_at("ba", 1, rc::regex {"(?<=b)a"}, rx::match_prev_avail), 0L);
  EXPECT_EQ(search_at("ba", 1, rc::regex {"(?<!b)a"}, rx::match_prev_avail), -1L);
  // Without the flag the same lookbehind sees nothing before first.
  EXPECT_EQ(search_at("ba", 1, rc::regex {"(?<!b)a"}, rx::match_default), 0L);
}

// Both together: anchored at first, with its context.
TEST(compat_continuous_with_prev_avail)
{
  EXPECT_EQ(search_at(" ab", 1, rc::regex {R"(\ba)"}, rx::match_continuous | rx::match_prev_avail), 0L);
  EXPECT_EQ(search_at("xab", 1, rc::regex {R"(\ba)"}, rx::match_continuous | rx::match_prev_avail), -1L);
  EXPECT_EQ(search_at("xab", 1, rc::regex {R"(\Ba)"}, rx::match_continuous | rx::match_prev_avail), 0L);
  EXPECT_EQ(search_at("xab", 1, rc::regex {"(?<=x)ab"}, rx::match_continuous | rx::match_prev_avail), 0L);
}

// The results' marks, prefix and suffix are the caller's, the context excluded; the calls without results agree.
TEST(compat_call_flags_fill_the_results_from_first)
{
  const std::string s     {"xa(b)c"};
  const rc::regex   re    {R"(\B(\()(b))"};
  rc::smatch        m;
  const auto        first {s.begin() + 2};
  EXPECT(!rc::regex_search(first, s.end(), m, re, rx::match_prev_avail)); // 'a' then '(': a boundary
  const rc::regex groups  {R"((b)\)(c))"};
  EXPECT(rc::regex_search(s.begin() + 3, s.end(), m, groups, rx::match_continuous | rx::match_prev_avail));
  EXPECT_EQ(m.position(0), 0L);
  EXPECT_EQ(m.str(1), std::string {"b"});
  EXPECT_EQ(m.str(2), std::string {"c"});
  EXPECT(!m.prefix().matched);
  EXPECT(m.prefix().first == s.begin() + 3);
  EXPECT(!m.suffix().matched);
  EXPECT(rc::regex_search(s.begin() + 3, s.end(), groups, rx::match_continuous | rx::match_prev_avail));
  rc::smatch later;
  EXPECT(rc::regex_search(s.begin() + 1, s.end(), later, rc::regex {"(?<=a)."}, rx::match_prev_avail));
  EXPECT_EQ(later.position(0), 1L);
  EXPECT(later.prefix().matched);
  EXPECT_EQ(later.prefix().str(), std::string {"a"});
}

// A non-contiguous range copies from the character before first under prev_avail.
TEST(compat_call_flags_on_a_non_contiguous_range)
{
  const std::list<char>                              ba {'b', 'a'};
  const std::deque<char>                             sp {' ', 'a', 'b'};
  rc::match_results<std::list<char>::const_iterator> lm;
  EXPECT(!rc::regex_search(std::next(ba.begin()), ba.end(), lm, rc::regex {R"(\ba)"}, rx::match_prev_avail));
  EXPECT(rc::regex_search(std::next(ba.begin()), ba.end(), lm, rc::regex {"(?<=b)a"}, rx::match_prev_avail));
  EXPECT(lm[0].first == std::next(ba.begin()));
  EXPECT(!lm.prefix().matched);
  rc::match_results<std::deque<char>::const_iterator> dm;
  EXPECT(rc::regex_search(std::next(sp.begin()), sp.end(), dm, rc::regex {R"(\bab)"},
                          rx::match_prev_avail | rx::match_continuous));
  EXPECT(dm[0].first == std::next(sp.begin()));
  EXPECT(dm[0].second == sp.end());
  EXPECT(!rc::regex_search(std::next(sp.begin()), sp.end(), rc::regex {"b"}, rx::match_continuous));
  EXPECT(rc::regex_search(std::next(sp.begin()), sp.end(), rc::regex {R"(\ba)"}, rx::match_prev_avail));
}

// Flags REAL does not take on a call stay on std: a POSIX leftmost-longest search under continuous, and not_bol
// without prev_avail.
TEST(compat_call_flags_reach_std_where_real_has_no_form)
{
  const std::string s     {"abc"};
  const rc::regex   posix {"a|ab", rc::regex_constants::extended};
  const std::regex  ref   {"a|ab", std::regex::extended};
  rc::smatch        m;
  std::smatch       want;
  EXPECT_EQ(rc::regex_search(s, m, posix, rx::match_continuous),
            std::regex_search(s, want, ref, std::regex_constants::match_continuous));
  EXPECT_EQ(m.length(0), want.length(0));
  EXPECT_EQ(search_at("ab", 0, rc::regex {"^a"}, rx::match_not_bol), -1L);
}

// The host std agrees: every pattern of the table over every subject on {a, b, space, \n} up to four characters,
// every start, under continuous, prev_avail and both. Two libc++ defects are counted apart, on libc++ only: it
// ignores prev_avail for a multiline `^` (for one outside multiline, see the witnesses above), and its `\b` never
// holds on an attempt that starts at last, where the character before is a word character.
TEST(compat_call_flags_agree_with_std)
{
  const char* const patterns[] {"a", "b", "ab", "a*", "a+", "b|a", "a|ab", R"(\b)", R"(\B)", R"(\ba)", R"(\Ba)",
                                R"(a\b)", R"(a\B)", R"(\b\w+)", "$", "a$", "(a)(b)?", "(?:a|b)*", R"(\s)", "(?=a)",
                                "(?!a)", "a(?=b)"};
  const char* const              ml_patterns[] {"^", "^a", "^$", "a$", "$", "^\\s", "^(a|b)"};
  const std::vector<std::string> subjects      {short_subjects()};
  agreement                      tally;
  for (const char* const p : patterns) {
    tally.add(p, rc::regex {p}, std::regex {p}, subjects);
  }
  for (const char* const p : ml_patterns) {
    tally.add(p, rc::regex {p, rc::regex_constants::ECMAScript | rc::regex_constants::multiline},
              std::regex {p, std::regex::ECMAScript | std::regex::multiline}, subjects);
  }
  EXPECT(tally.compared > 100000U);
  EXPECT_EQ(tally.diverged, std::size_t {0});
#if defined(_LIBCPP_VERSION)
  EXPECT_EQ(tally.libcxx_caret, std::size_t {4440});
  EXPECT_EQ(tally.libcxx_at_last, std::size_t {960});
#else
  EXPECT_EQ(tally.libcxx_caret, std::size_t {0});
  EXPECT_EQ(tally.libcxx_at_last, std::size_t {0});
#endif
}
