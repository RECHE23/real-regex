// real::compat (std::regex drop-in, char path) — the differential harness is the gate.
//
// Primary oracle: ECMAScript semantics. Secondary oracle: std::regex (libstdc++/libc++), with an
// allowlist for its known deviations from the spec (real follows the spec there). Every case runs
// real::compat against std::regex and asserts the SAME verdict + span + group structure, except
// the allowlisted libstdc++ deviations, where the compat (spec) behavior is pinned instead.
//
// Both backends are exercised (real-backed AND the std fallback) so coverage is honest.
#include <regex>
#include <string>
#include <string_view>
#include <vector>

#include <sciforge/test/framework.hpp>
#include "real/std_compat.hpp"

namespace rc = real::compat;
using namespace std::string_view_literals;

namespace {

  // Run real::compat and std::regex on (pattern, subject); assert identical verdict + whole-match
  // span + per-group span. Returns false if either side failed to compile (caller decides).
  bool agree(const std::string& pat,
             const std::string& subj)
  {
    rc::regex  rre;
    std::regex sre;
    try {
      rre = rc::regex(pat);
    }
    catch (const std::exception&) {
      return false;
    }
    try {
      sre = std::regex(pat, std::regex::ECMAScript);
    }
    catch (const std::exception&) {
      return false;
    }
    rc::smatch  rm;
    std::smatch sm;
    const bool  rf {rc::regex_search(subj, rm, rre)};
    const bool  sf {std::regex_search(subj, sm, sre)};
    if (rf != sf) {
      return false;
    }
    if (!rf) {
      return true; // both no-match
    }
    if (rm.size() != sm.size() || rm.position(0) != sm.position(0) || rm.length(0) != sm.length(0)) {
      return false;
    }
    for (std::size_t g = 0; g < rm.size(); ++g) {
      if (rm[g].matched != sm[g].matched) {
        return false;
      }
      if (rm[g].matched && (rm.position(g) != sm.position(g) || rm.length(g) != sm.length(g))) {
        return false;
      }
    }
    return true;
  }
} // namespace

// A generic, application-neutral corpus of common idioms — forms only, exercising BOTH backends.
// Each entry pins the expected backend; spec-aligned entries also assert the std differential.
namespace {

  enum class Backend
  {
    real,    //!< real proves it -> linear, ReDoS-safe.
    fallback //!< real rejects -> std backend.
  };

  struct Entry
  {
    std::string pattern;
    std::string subject;
    Backend     backend;

    Entry(std::string p,
          std::string s,
          Backend     b)
      : pattern(std::move(p)), subject(std::move(s)), backend(b)
    {}
  };

  const std::vector<Entry>& corpus()
  {
    static const std::vector<Entry> c {
      // Anchored validations.
      {R"(^[\w.]+@[\w.]+\.[a-z]+$)", "alice.b@example.com", Backend::real},
      {R"(^\d{4}-\d{2}-\d{2}$)", "2026-06-30", Backend::real},
      {R"(^(\d{1,3}\.){3}\d{1,3}$)", "192.168.0.1", Backend::real},
      {R"(^[A-Z][a-z]+$)", "Hello", Backend::real},
      // Tokenizing.
      {R"(\w+)", "two words", Backend::real},
      {R"(\d+)", "item 42", Backend::real},
      {R"(\b\w+\b)", "a word", Backend::real},
      // Literals / classes.
      {"error", "an error here", Backend::real},
      {"[^a-zA-Z0-9]", "ab cd", Backend::real},
      {R"([\x00-\x1f])", "x\ty", Backend::real},
      // Alternation / optional / captures.
      {"(GET|POST|PUT|DELETE)", "POST /x", Backend::real},
      {R"(https?://\S+)", "see http://a.b/c now", Backend::real},
      {R"((\w+)@(\w+)\.(\w+))", "u@h.com", Backend::real},
      {R"(^(?:([^:]*)::)?(.*)$)", "ns::name", Backend::real},
      // Bounded quantifiers.
      {"a{2,4}", "aaaaa", Backend::real},
      {R"(\d{1,3})", "255", Backend::real},
      // ECMAScript identity escapes (REAL's `\< \>` anchors become literals under ecma).
      {R"(a\>b)", "a>b", Backend::real},
      {R"(\<tag)", "<tag>", Backend::real},
      // Fallback triggers (exercise the std backend).
      {R"((\w+)\s+\1)", "hi hi", Backend::fallback},      // backreference
      {R"((?=.{8,})\w+)", "abcdefgh", Backend::fallback}, // unbounded-width lookahead
    };
    return c;
  }
} // namespace

TEST(compat_corpus_backend_and_differential)
{
  std::size_t fallback_count {0};
  for (const Entry& e : corpus()) {
    const rc::regex re(e.pattern);
    EXPECT_EQ(re.uses_real(), e.backend == Backend::real); // pins the expected backend
    if (re.uses_real()) {
      // No fallback: this case agrees with std (both spec-aligned here).
      EXPECT(agree(e.pattern, e.subject));
    }
    else {
      ++fallback_count;
      // Fallback runs std under the hood, so it is std by construction; assert it still matches.
      EXPECT(rc::regex_search(e.subject, re));
      EXPECT(std::regex_search(e.subject, std::regex(e.pattern, std::regex::ECMAScript)));
    }
  }
  // q3 — fallback rate over this generic corpus. Two of the entries (backref, unbounded lookahead)
  // route to std; the rest stay on real. A high rate would mean the screen is too wide.
  EXPECT_EQ(fallback_count, 2U);
  EXPECT(fallback_count * 5U < corpus().size()); // < 20% fallback
}

TEST(compat_libstdcxx_deviations_are_allowlisted)
{
  // libstdc++ deviations from the ECMAScript spec; real::compat follows the spec (Option B).

  // Lookbehind: ES2018 has it; libstdc++ rejects it. real::compat accepts and matches.
  {
    const rc::regex re(R"((?<=a)b)");
    EXPECT(re.uses_real());
    rc::smatch        m;
    const std::string subj {"ab"};
    EXPECT(rc::regex_search(subj, m, re));
    EXPECT_EQ(m.position(0), 1);
    EXPECT_EQ(m.length(0), 1);
  }

  // POSIX class: ECMAScript has no POSIX classes — `[[:alpha:]]+` / `[[:digit:]]+` are literal char
  // classes (Option B, spec-primary). libstdc++ applies a non-portable POSIX extension; libc++ does
  // not. real::compat stays on real and follows the spec; the deviation is allowlisted here.
  {
    const rc::regex re("[[:digit:]]+");
    EXPECT(re.uses_real());
    EXPECT(!rc::regex_search(std::string("123"), re)); // spec: no [,:,d,i,g,t before a `]`
    EXPECT(rc::regex_search(std::string("d]]"), re));  // spec: 'd' in class, ']]' is `]+`
    EXPECT(rc::regex("[[:alpha:]]+").uses_real());     // also stays on real (no textual screen)
  }

  // Identity escapes `\A` / `\Z`: ECMAScript Annex B treats them as the literal A / Z; libstdc++
  // rejects them. real::compat follows the spec (literal), a benign superset over std.
  {
    const rc::regex re(R"(\Aabc)");
    EXPECT(re.uses_real());
    EXPECT(rc::regex_search(std::string("xAabc"), re)); // `\A` == literal 'A', matches mid-string
    EXPECT(!rc::regex_search(std::string("abc"), re));  // NOT a start-anchor
  }
}

TEST(compat_backend_selection)
{
  // ECMAScript-representable -> real backend (linear, ReDoS-safe).
  EXPECT(rc::regex("a+b").uses_real());
  EXPECT(rc::regex("[a-z]+\\d*").uses_real());
  EXPECT(rc::regex(R"((?=foo)\w+)").uses_real()); // lookahead is real-able

  // Backreference -> real rejects -> std fallback (std ECMAScript supports backrefs).
  const rc::regex back(R"((a)\1)");
  EXPECT(!back.uses_real());
  rc::smatch        m;
  const std::string subj {"aa"};
  EXPECT(rc::regex_search(subj, m, back)); // fallback path produces the match
  EXPECT_EQ(m.length(0), 2);

  // A POSIX grammar option forces the std backend up front.
  EXPECT(!rc::regex("a+", rc::regex_constants::extended).uses_real());
}

TEST(compat_match_results_offsets_and_groups)
{
  // Off-by-one / npos guard on the offset->iterator fill.
  const rc::regex   re(R"((\d{4})-(\d{2})-(\d{2}))");
  rc::smatch        m;
  const std::string subj {"date: 2026-06-30 end"};
  EXPECT(rc::regex_search(subj, m, re));
  EXPECT_EQ(m.size(), 4U);             // group 0 + 3 captures
  EXPECT_EQ(m.position(0), 6);         // "2026-06-30" starts at index 6
  EXPECT_EQ(m.length(0), 10);
  EXPECT_EQ(m.str(1), "2026");
  EXPECT_EQ(m.str(2), "06");
  EXPECT_EQ(m.str(3), "30");
  EXPECT_EQ(m.str(), "2026-06-30");
  EXPECT_EQ(m.prefix().str(), "date: ");
  EXPECT_EQ(m.suffix().str(), " end");

  // A non-participating optional group: matched=false, zero length.
  const rc::regex   opt("(a)(b)?(c)");
  rc::smatch        mo;
  const std::string ac {"ac"};
  EXPECT(rc::regex_search(ac, mo, opt));
  EXPECT(mo[1].matched);
  EXPECT(!mo[2].matched);
  EXPECT(mo[3].matched);
  EXPECT_EQ(mo.length(2), 0);
}

TEST(compat_regex_match_whole_sequence)
{
  // regex_match anchors the whole sequence (== std::regex_match == real fullmatch).
  EXPECT(rc::regex_match(std::string("aaa"), rc::regex("a+")));
  EXPECT(!rc::regex_match(std::string("aaab"), rc::regex("a+")));
  EXPECT(rc::regex_match(std::string("abc"), rc::regex("^abc$")));
  EXPECT(!rc::regex_match(std::string("abc\n"), rc::regex("abc")));   // ecma $: end-only
  rc::smatch        m;
  const std::string s {"2026"};
  EXPECT(rc::regex_match(s, m, rc::regex("(\\d)(\\d)(\\d)(\\d)")));
  EXPECT_EQ(m.str(1), "2");
  EXPECT_EQ(m.str(4), "6");

  // Differential vs std::regex_match.
  for (const auto& [pat, subj] : std::vector<std::pair<std::string, std::string>> {
    {"a+", "aaa"}, {"a+", "aaab"}, {"^a+$", "aaa"}, {".*", "abc"}, {"x?", ""}}) {
    const bool rmatch {rc::regex_match(subj, rc::regex(pat))};
    const bool smatch {std::regex_match(subj, std::regex(pat, std::regex::ECMAScript))};
    EXPECT_EQ(rmatch, smatch);
  }
}

TEST(compat_api_surface)
{
  // basic_regex ctors + accessors.
  const rc::regex r1("(a)(b)(c)");                          // const char*
  EXPECT_EQ(r1.mark_count(), 3U);
  EXPECT(r1.flags() == rc::regex_constants::ECMAScript);
  const std::string pat {"a+"};
  const rc::regex   r2(pat, rc::regex_constants::icase);    // std::string + flags
  EXPECT(r2.flags() == rc::regex_constants::icase);
  const rc::regex   r3("abcdef", 3);                        // (ptr, len) -> "abc"
  EXPECT(rc::regex_match(std::string("abc"), r3));
  const std::string itpat {"x[0-9]+"};
  const rc::regex   r4(itpat.begin(), itpat.end());         // iterator pair
  EXPECT(rc::regex_search(std::string("x42"), r4));

  // swap.
  rc::regex a("foo");
  rc::regex b("bar");
  a.swap(b);
  EXPECT(rc::regex_match(std::string("bar"), a));
  EXPECT(rc::regex_match(std::string("foo"), b));

  // sub_match API.
  const rc::regex   sre("(\\w+)");
  rc::smatch        sm;
  const std::string subj {"hello"};
  EXPECT(rc::regex_search(subj, sm, sre));
  const auto& whole      {sm[0]};
  EXPECT(whole.matched);
  EXPECT_EQ(whole.length(), 5);
  EXPECT_EQ(whole.str(), std::string("hello"));
  EXPECT(whole.view() == "hello"sv);
  EXPECT(whole == std::string("hello"));        // operator==(sub_match,string)
  EXPECT_EQ(whole.compare(std::string("hello")), 0);
  const std::string converted = sm[1];          // operator string_type
  EXPECT_EQ(converted, "hello");

  // match_results iteration + empty/ready on a fresh result.
  std::size_t group_count {0};
  for (const auto& g : sm) {
    EXPECT(g.matched);
    ++group_count;
  }
  EXPECT_EQ(group_count, sm.size());
  rc::smatch fresh;
  EXPECT(!fresh.ready());
  EXPECT(fresh.empty());

  // Free-function overloads: no-match-results, const char*, iterator pair.
  EXPECT(rc::regex_search(std::string("zzfoozz"), rc::regex("foo")));      // (string, re)
  EXPECT(!rc::regex_search(std::string("zzz"), rc::regex("foo")));
  rc::cmatch cm;
  EXPECT(rc::regex_search("xxabcyy", cm, rc::regex("abc")));               // (const char*, cmatch, re)
  EXPECT_EQ(cm.str(), "abc");
  const char* cstr {"needle"};
  EXPECT(rc::regex_search(cstr, rc::regex("needle")));                     // (const char*, re)
  EXPECT(rc::regex_match(cstr, rc::regex("needle")));                      // (const char*, re) match
  const std::string  seq {"12-34"};
  rc::smatch         im;
  EXPECT(rc::regex_search(seq.begin(), seq.end(), im, rc::regex("\\d+"))); // (iterators, m, re)
  EXPECT(rc::regex_search(seq.begin(), seq.end(), rc::regex("\\d+")));     // (iterators, re)
}

TEST(compat_invalid_for_both_throws_compat_error)
{
  // Invalid for real AND std -> compat::regex_error (a std::regex_error).
  bool threw {false};
  try {
    const rc::regex re("(unclosed");
    (void)re;
  }
  catch (const std::regex_error&) {
    threw = true;
  }
  EXPECT(threw);
}
