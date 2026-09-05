// libc's own `regcomp`/`regexec` as a SECOND oracle for the POSIX grammars, independent of
// `std::regex`.
//
// test_posix_ere/bre/awk_grep_egrep pin the translators against `std::regex`'s POSIX modes, on
// hand-written tables of forty to seventy literals. That oracle is one implementation, and this
// project has already had to record that it is not a reliable one everywhere -- test_compat's
// word-boundary allowlist exists because libstdc++/libc++ disagree with the spec on `\b` inside a
// lookahead and on a bare `\B`. libc's regex is a different implementation of the same standard,
// present on every POSIX platform, and it is the reference POSIX behaviour rather than a rendering
// of it: leftmost-LONGEST, which is the property the translators exist to preserve.
//
// The construct set is deliberately the unambiguous POSIX core. Left out because libc
// implementations legitimately differ or because they are outside what the translators claim:
// back-references (this engine is linear and has none), equivalence `[=a=]` and collating `[.a.]`
// classes, REG_NEWLINE, and any locale-dependent class beyond ASCII -- the locale is forced to "C"
// for the duration so a machine's environment cannot decide what `[[:alpha:]]` means.
#include <string>
#include <utility>
#include <vector>

#include <sciforge/test/framework.hpp>

#if defined(_WIN32)

// A SKIP IS NOT A PASS, so it is stated rather than left as an absent test: MSVC has no <regex.h>,
// and the POSIX translators are still covered on this platform by the std::regex-oracle files.
TEST(posix_libc_oracle_unavailable_on_this_platform)
{
  EXPECT(true); // the message below is the point; see test_posix_ere.cpp for the leg that does run
}

#else

  #include <clocale>
  #include <regex.h>

  #include "real/compat/std/regex.hpp"

namespace rc = real::compat;

namespace {

  //! Every span of a match, group 0 first; `{-1,-1}` for a group that did not participate.
  using spans = std::vector<std::pair<long, long>>;

  //! The sentinel a caller must distinguish from "no match": libc declined the pattern, so there is
  //! no oracle answer for it at all.
  constexpr long declined {-2};

  spans libc_spans(const char* pattern,
                   const char* text,
                   int         cflags)
  {
    regex_t compiled {};
    if (regcomp(&compiled, pattern, cflags) != 0) {
      return {{declined, declined}};
    }
    const std::size_t       slots {compiled.re_nsub + 1};
    std::vector<regmatch_t> raw(slots);
    const int               rc    {regexec(&compiled, text, slots, raw.data(), 0)};
    regfree(&compiled);
    if (rc != 0) {
      return {};
    }
    spans out;
    out.reserve(slots);
    for (const auto& one : raw) {
      out.emplace_back(static_cast<long>(one.rm_so), static_cast<long>(one.rm_eo));
    }
    return out;
  }

  spans real_spans(const char                            * pattern,
                   const char                            * text,
                   rc::regex_constants::syntax_option_type f)
  {
    rc::regex  compiled {pattern, f};
    rc::cmatch found;
    if (!rc::regex_search(text, found, compiled)) {
      return {};
    }
    spans out;
    out.reserve(found.size());
    for (std::size_t group = 0; group < found.size(); ++group) {
      out.emplace_back(found[group].matched ? static_cast<long>(found.position(group)) : -1L,
                       found[group].matched
                         ? static_cast<long>(found.position(group) + found.length(group))
                         : -1L);
    }
    return out;
  }

  // POSIX ERE. Alternation carries the weight: leftmost-longest is what separates POSIX from every
  // Perl-family engine, and `a|ab` over "abc" is the one-line proof (POSIX says 0..2, a
  // leftmost-FIRST engine says 0..1).
  const std::vector<std::string>& ere_patterns()
  {
    static const std::vector<std::string> list {
      "a", "ab", "a|ab", "ab|a", "(a|ab)c", "a*", "a+", "a?", "ab*", "(ab)*",
      "[abc]", "[^abc]", "[a-c]+", "[[:digit:]]+", "[[:alpha:]]+", "[[:space:]]",
      "[[:alnum:]]+", "[[:punct:]]", "[[:upper:]]", "[[:lower:]]+",
      ".", ".*", "a.c", "^a", "a$", "^a$", "^", "$",
      "a{2}", "a{2,}", "a{2,3}", "(a)(b)", "(a)|(b)", "((a)b)", "(a*)(b*)",
      "x*", "(|a)", "()", "[0-9]{2,4}", "a|b|c", "(a|b)(c|d)",
    };
    return list;
  }

  // POSIX BRE: `\(` groups, `\{m,n\}` intervals, and no alternation at all -- `|` is an ordinary
  // character here, which is itself worth asserting.
  const std::vector<std::string>& bre_patterns()
  {
    static const std::vector<std::string> list {
      "a", "ab", "a*", "ab*", "a\\{2\\}", "a\\{2,3\\}", "\\(a\\)", "\\(ab\\)*",
      "\\(a\\)\\(b\\)", "[abc]", "[^abc]", "[a-c]*", "[[:digit:]]*", "[[:alpha:]]*",
      ".", ".*", "a.c", "^a", "a$", "^a$", "a|b", "x*", "\\(a*\\)\\(b*\\)",
    };
    return list;
  }

  const std::vector<std::string>& subjects()
  {
    static const std::vector<std::string> list {
      "",   "a",     "b",       "ab",   "ba",    "abc",  "aab",  "aaa",   "abab",
      "cab", "a1b2",  "  a  ",  "A1b", "a|b",   "xyz",  "12345", "a.c",  "aXc",
      ".",  "abcabc", "banana", "a b", "AB",    "a{2}", "()",    "a*b",
    };
    return list;
  }

  //! One scoped switch to the C locale: `[[:alpha:]]` must not mean different things on two
  //! machines because their environments differ.
  struct c_locale_guard
  {
    std::string previous;

    c_locale_guard()
    {
      const char* current {std::setlocale(LC_ALL, nullptr)};
      previous = current != nullptr ? current : "";
      std::setlocale(LC_ALL, "C");
    }

    c_locale_guard(const c_locale_guard&)            = delete;
    c_locale_guard& operator=(const c_locale_guard&) = delete;
    c_locale_guard(c_locale_guard&&)                 = delete;
    c_locale_guard& operator=(c_locale_guard&&)      = delete;

    ~c_locale_guard()
    {
      if (!previous.empty()) {
        std::setlocale(LC_ALL, previous.c_str());
      }
    }
  };

  //! Compares one grammar end to end and returns {compared, declined, divergences}.
  struct tally
  {
    std::size_t              compared {};
    std::size_t              declined {};
    std::vector<std::string> failures;
  };

  tally sweep(const std::vector<std::string>&         patterns,
              int                                     cflags,
              rc::regex_constants::syntax_option_type option)
  {
    tally result;
    for (const auto& pattern : patterns) {
      for (const auto& subject : subjects()) {
        const spans want {libc_spans(pattern.c_str(), subject.c_str(), cflags)};
        if (!want.empty() && want.front().first == declined) {
          ++result.declined; // libc has no answer here, so neither has this comparison
          continue;
        }
        ++result.compared;
        spans got;
        try {
          got = real_spans(pattern.c_str(), subject.c_str(), option);
        }
        catch (const std::exception& err) {
          result.failures.push_back("pattern '" + pattern + "' subject '" + subject +
                                    "': libc accepts it, this engine refuses -- " + err.what());
          continue;
        }
        // Group 0 is the contract POSIX states; a libc that reports fewer slots than this engine
        // does is compared only over the slots it reported, so an extra capture cannot read as a
        // divergence in the whole-match bounds.
        const std::size_t common {want.size() < got.size() ? want.size() : got.size()};
        for (std::size_t group = 0; group < common; ++group) {
          if (want[group] != got[group]) {
            result.failures.push_back(
              "pattern '" + pattern + "' subject '" + subject + "' group " +
              std::to_string(group) + ": this engine (" + std::to_string(got[group].first) + "," +
              std::to_string(got[group].second) + "), libc (" + std::to_string(want[group].first) +
              "," + std::to_string(want[group].second) + ")");
          }
        }
        if (want.empty() != got.empty()) {
          result.failures.push_back("pattern '" + pattern + "' subject '" + subject +
                                    "': one side matched and the other did not");
        }
      }
    }
    return result;
  }

  void report(const char * label,
              const tally& result)
  {
    std::printf("%s: %zu comparisons, %zu declined by libc\n", label, result.compared,
                result.declined);
    for (std::size_t i = 0; i < result.failures.size() && i < 10; ++i) {
      std::printf("  %s\n", result.failures[i].c_str());
    }
    if (result.failures.size() > 10) {
      std::printf("  ... %zu more\n", result.failures.size() - 10);
    }
  }
} // namespace

TEST(posix_ere_bounds_equal_libc_regcomp)
{
  const c_locale_guard locale;
  const tally          result {sweep(ere_patterns(), REG_EXTENDED, rc::regex_constants::extended)};
  report("posix ERE vs libc", result);
  EXPECT(result.compared > 0); // a sweep that compared nothing is not a green
  EXPECT(result.failures.empty());
}

TEST(posix_bre_bounds_equal_libc_regcomp)
{
  const c_locale_guard locale;
  const tally          result {sweep(bre_patterns(), 0, rc::regex_constants::basic)};
  report("posix BRE vs libc", result);
  EXPECT(result.compared > 0);
  EXPECT(result.failures.empty());
}

TEST(posix_leftmost_longest_is_what_libc_says)
{
  // The property the translators exist for, asserted on its own rather than only as one row of a
  // sweep: POSIX takes the LONGEST leftmost match, so `a|ab` over "abc" is 0..2. A leftmost-first
  // engine answers 0..1 and would pass every other case in this file.
  const c_locale_guard locale;
  const struct
  {
    const char* pattern;
    const char* subject;
    long        begin;
    long        end;
  } cases[] {
    {"a|ab", "abc", 0, 2},
    {"ab|a", "abc", 0, 2},
    {"(a|ab)(c|bcd)", "abcd", 0, 4},
    {"a*", "aaa", 0, 3},
    {"(a|ab)c", "abc", 0, 3},
  };
  for (const auto& one : cases) {
    const spans want {libc_spans(one.pattern, one.subject, REG_EXTENDED)};
    const spans got  {real_spans(one.pattern, one.subject, rc::regex_constants::extended)};
    EXPECT(!want.empty());
    EXPECT(!got.empty());
    EXPECT(want.front().first == one.begin);   // libc still says what this test claims it says
    EXPECT(want.front().second == one.end);
    EXPECT(got.front().first == one.begin);
    EXPECT(got.front().second == one.end);
  }
  EXPECT(sizeof(cases) / sizeof(cases[0]) == 5); // a deleted row must fail, not shrink
}

#endif // _WIN32
