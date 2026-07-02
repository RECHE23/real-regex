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
#include <tuple>
#include <utility>
#include <vector>

#include <sciforge/test/framework.hpp>
#include "real/std_compat.hpp"

namespace rc = real::compat;
using namespace std::string_view_literals;

// ptrdiff_t: position/length are ptrdiff_t; long narrows on MSVC-LLP64 under /WX.
using span_pair = std::pair<std::ptrdiff_t, std::ptrdiff_t>;

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

TEST(compat_byte_class_matches_std)
{
  // Bloquant A (U2-fix): a bytes-path class member >= 0x80 is a RAW BYTE, exactly like
  // std::basic_regex<char> — never a re-encoded UTF-8 code point. Each pattern stays on the real
  // backend and agrees with std::regex byte-for-byte on high-byte subjects.
  const auto raw {[](std::initializer_list<int> values) {
                    std::string s;
                    for (const int v : values) {
                      s += static_cast<char>(v);
                    }
                    return s;
                  }};
  for (const char* pat : {R"([\x80-\xff]+)", R"([\xe9])", R"([^\x00-\x7f])", R"([\xc3\xa9]+)"}) {
    EXPECT(rc::regex(pat).uses_real());
    EXPECT(agree(pat, raw({0xC3, 0xA9})));
    EXPECT(agree(pat, raw({0xFF, 0x80, 0x41})));
    EXPECT(agree(pat, std::string("abc")));
    EXPECT(agree(pat, raw({0xE9})));
  }
}

TEST(compat_icase_ascii_byte_escape_matches_std)
{
  // CF-fix: a `\xHH` escape with value < 0x80 is an ASCII character, so under icase it folds (ASCII
  // fold, bytes path) exactly like std::basic_regex<char> -- `\x4b` == `K` matches k/K.
  for (const char* pat : {R"(\x4b)", R"(hello)", R"([a-f]+)"}) {
    const rc::regex re(pat, rc::regex_constants::icase);
    EXPECT(re.uses_real());
    const std::regex sre(pat, std::regex::ECMAScript | std::regex::icase);
    for (const std::string& s : {std::string("k"), std::string("K"), std::string("HELLO"),
                                 std::string("abcDEF"), std::string("xyz")}) {
      rc::smatch  rm;
      std::smatch sm;
      const bool  rf {rc::regex_search(s, rm, re)};
      const bool  sf {std::regex_search(s.cbegin(), s.cend(), sm, sre)};
      EXPECT_EQ(rf, sf);
      if (rf && sf) {
        EXPECT_EQ(rm.position(0), sm.position(0));
        EXPECT_EQ(rm.length(0), sm.length(0));
      }
    }
  }
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

namespace {

  // `\0`+digit is a legacy octal escape on real (Annex B) that real screens to std (a both-accept
  // divergence otherwise). std itself is platform-variant here: libstdc++/libc++ accept it (Annex B
  // lenient), MSVC-std rejects it (error_escape, strict). compat defers to the LOCAL std on both
  // sides — it throws iff std throws; if both accept, it uses std (not real) and matches std.
  void expect_defers_to_local_std_on_escape(const std::string& pat)
  {
    const auto rejects {[](auto make) {
                          try {
                            make();
                            return false;
                          }
                          catch (const std::regex_error&) {
                            return true;
                          }
                        }};
    const bool std_rejects    {rejects([&] { (void)std::regex(pat, std::regex::ECMAScript); })};
    const bool compat_rejects {rejects([&] { (void)rc::regex(pat); })};
    EXPECT_EQ(compat_rejects, std_rejects); // throws iff std throws (== the local std)
    if (!std_rejects) {
      const rc::regex re(pat);
      EXPECT(!re.uses_real()); // screened to std, not real
      const std::string nul {std::string("a") + '\0' + "0b"};
      EXPECT_EQ(rc::regex_search(nul, re),
                std::regex_search(nul.cbegin(), nul.cend(), std::regex(pat, std::regex::ECMAScript)));
    }
  }
} // namespace

TEST(compat_backend_selection)
{
  // ECMAScript-representable -> real backend (linear, ReDoS-safe).
  EXPECT(rc::regex("a+b").uses_real());
  EXPECT(rc::regex("[a-z]+\\d*").uses_real());
  EXPECT(rc::regex(R"((?=foo)\w+)").uses_real()); // lookahead is real-able

  // The nullable() introspection accessor (sister of uses_real(); documented in COMPATIBILITY): a
  // pattern that can match empty is nullable, so replace/iterate route to std -- uses_real_traversal()
  // is real AND non-nullable.
  const rc::regex non_null("a+b");
  EXPECT(!non_null.nullable());
  EXPECT(non_null.uses_real_traversal());        // real + non-nullable -> real traversal
  const rc::regex can_be_empty("a*");
  EXPECT(can_be_empty.uses_real());
  EXPECT(can_be_empty.nullable());               // matches empty
  EXPECT(!can_be_empty.uses_real_traversal());   // nullable -> replace/iterate defer to std

  // Backreference -> real rejects -> std fallback (std ECMAScript supports backrefs).
  const rc::regex back(R"((a)\1)");
  EXPECT(!back.uses_real());
  rc::smatch        m;
  const std::string subj {"aa"};
  EXPECT(rc::regex_search(subj, m, back)); // fallback path produces the match
  EXPECT_EQ(m.length(0), 2);

  // A POSIX grammar option forces the std backend up front.
  EXPECT(!rc::regex("a+", rc::regex_constants::extended).uses_real());

  // `\0`+digit is screened to std (a both-accept divergence otherwise). std is platform-variant on
  // it (libstdc++/libc++ accept, MSVC-std rejects), so compat defers to the local std: throw iff std
  // throws; if both accept, it uses std (not real) and matches std.
  expect_defers_to_local_std_on_escape(R"(\00)");  // octal NUL on real -> std (accept on Linux, reject on MSVC)
  expect_defers_to_local_std_on_escape(R"(\012)"); // octal newline on real -> std
  EXPECT(rc::regex(R"(\0)").uses_real());          // \0 alone is NUL on both -> stays on real (platform-stable)
  EXPECT(rc::regex(R"(\0x)").uses_real());         // \0 then non-digit literal -> stays on real
}

namespace {

  // search [lo, hi) with a match flag on BOTH backends; assert identical verdict + whole-match span.
  bool mf_search_agrees(const std::string&                            pat,
                        std::string::const_iterator                   lo,
                        std::string::const_iterator                   hi,
                        rc::regex_constants::match_flag_type          rf,
                        std::regex_constants::match_flag_type         sf)
  {
    const rc::regex  rre(pat);
    const std::regex sre(pat, std::regex::ECMAScript);
    rc::smatch       rm;
    std::smatch      sm;
    const bool       rok {rc::regex_search(lo, hi, rm, rre, rf)};
    const bool       sok {std::regex_search(lo, hi, sm, sre, sf)};
    if (rok != sok) {
      return false;
    }
    return !rok || (rm.position(0) == sm.position(0) && rm.length(0) == sm.length(0));
  }
} // namespace

TEST(compat_match_flags)
{
  namespace mc = rc::regex_constants;
  namespace sc = std::regex_constants;

  // A constraining flag must route to std and match it exactly (not be accepted-then-ignored).
  const std::string a1 {"xabc"};
  EXPECT(mf_search_agrees("^abc", a1.begin(), a1.end(), mc::match_not_bol, sc::match_not_bol));
  const std::string a2 {"abcx"};
  EXPECT(mf_search_agrees("abc$", a2.begin(), a2.end(), mc::match_not_eol, sc::match_not_eol));

  // match_continuous: the match must begin at the first character.
  const std::string c1 {"abcabc"};
  EXPECT(mf_search_agrees("abc", c1.begin(), c1.end(), mc::match_continuous, sc::match_continuous));
  const std::string c2 {"xabc"}; // no anchored match at 0 -> both fail
  EXPECT(mf_search_agrees("abc", c2.begin(), c2.end(), mc::match_continuous, sc::match_continuous));

  // not_bow / not_eow on word boundaries.
  const std::string w1 {"abc"};
  EXPECT(mf_search_agrees(R"(\babc)", w1.begin(), w1.end(), mc::match_not_bow, sc::match_not_bow));

  // not_null on a nullable pattern (routes to std, which respects the flag).
  const std::string n1; // empty subject
  EXPECT(mf_search_agrees("a*", n1.begin(), n1.end(), mc::match_not_null, sc::match_not_null));
  const std::string n2 {"b"};
  EXPECT(mf_search_agrees("a*", n2.begin(), n2.end(), mc::match_not_null, sc::match_not_null));

  // match_prev_avail: scanning a sub-range where the char before `lo` exists; \b sees it.
  const std::string pv {"a word"};
  EXPECT(mf_search_agrees(R"(\bword)", pv.begin() + 2, pv.end(), mc::match_prev_avail, sc::match_prev_avail));

  // Multi-flag combo routes to std too.
  const std::string m1 {"abc"};
  EXPECT(mf_search_agrees("^abc$", m1.begin(), m1.end(),
                          mc::match_not_bol | mc::match_not_eol, sc::match_not_bol | sc::match_not_eol));

  // match_default and the match_any hint keep the real backend (and still agree with std).
  const std::string d1 {"x abc y"};
  EXPECT(mf_search_agrees(R"(\w+)", d1.begin(), d1.end(), mc::match_default, sc::match_default));
  EXPECT(mf_search_agrees(R"(\w+)", d1.begin(), d1.end(), mc::match_any, sc::match_any));
  EXPECT(rc::detail::real_honors(mc::match_default));
  EXPECT(rc::detail::real_honors(mc::match_any));
  EXPECT(!rc::detail::real_honors(mc::match_continuous));
  EXPECT(!rc::detail::real_honors(mc::match_not_bol));

  // regex_match with a flag (whole-sequence) also honors-or-falls-back.
  const rc::regex   word(R"(\w+)");
  const std::string subj {"abc"};
  rc::smatch        wm;
  EXPECT(rc::regex_match(subj, wm, word, mc::match_default));  // real path
  EXPECT(rc::regex_match(subj, wm, word, mc::match_not_null)); // std path, non-empty still matches

  // Iterator: a constraining flag routes the iterator to std; the span sequence equals std's.
  const std::string                  it1 {"abc abc"};
  std::vector<span_pair>             got;
  std::vector<span_pair>             ref;
  for (rc::sregex_iterator it(it1.begin(), it1.end(), word, mc::match_continuous), e; it != e; ++it) {
    got.emplace_back(it->position(0), it->length(0));
  }
  const std::regex sword(R"(\w+)", std::regex::ECMAScript);
  for (std::sregex_iterator it(it1.begin(), it1.end(), sword, sc::match_continuous), e; it != e; ++it) {
    ref.emplace_back(it->position(0), it->length(0));
  }
  EXPECT(got == ref);
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

TEST(compat_regex_replace)
{
  // ECMAScript format expansion + differential vs std::regex_replace.
  const std::vector<std::tuple<std::string, std::string, std::string>> cases {
    {"o", "foo bar", "0"}, {R"((\w+)@(\w+))", "a@b", "$2.$1"}, {"\\d+", "x12y34", "#"},
    {"(a)(b)", "ab", "$&-$1-$2"}, {"l", "hello", "[$`|$']"}, {"a", "banana", "X"},
    {R"((\d{4})-(\d{2}))", "2026-06", "$2/$1"}, {"x", "axbxc", "$$"},
    {"x*", "abc", "-"}, // nullable -> lazy std fallback
  };
  for (const auto& [pat, subj, fmt] : cases) {
    const rc::regex   re(pat);
    const std::string got {rc::regex_replace(subj, re, fmt)};
    const std::string ref {std::regex_replace(subj, std::regex(pat, std::regex::ECMAScript), fmt)};
    EXPECT_EQ(got, ref);
  }

  // Nullable pattern uses the lazy std backend for replace (the empty-match traversal differs).
  EXPECT(rc::regex("a*").uses_real());            // real-backed for search/match (S1)
  EXPECT(!rc::regex("a*").uses_real_traversal()); // but NOT for replace/iterate (nullable)

  // flags: format_first_only, format_no_copy.
  EXPECT_EQ(rc::regex_replace(std::string("a b c"), rc::regex("\\w"), "X",
                              rc::regex_constants::format_first_only),
            std::string("X b c"));
  EXPECT_EQ(rc::regex_replace(std::string("a1b2c3"), rc::regex("\\d"), "<$&>",
                              rc::regex_constants::format_no_copy),
            std::string("<1><2><3>"));

  // const char* fmt + output-iterator overloads.
  EXPECT_EQ(rc::regex_replace(std::string("foo"), rc::regex("o"), "0"), std::string("f00"));
  std::string       sink;
  const std::string src {"a-b-c"};
  rc::regex_replace(std::back_inserter(sink), src.begin(), src.end(), rc::regex("-"), std::string("+"));
  EXPECT_EQ(sink, std::string("a+b+c"));

  // Std-backed pattern (backref) routes regex_replace to std::regex_replace.
  const rc::regex back(R"((a)\1)");
  EXPECT(!back.uses_real());
  EXPECT_EQ(rc::regex_replace(std::string("aa b aa"), back, "X"), std::string("X b X"));

  // Edge cases vs std: $NN two-digit group, trailing $ (literal), non-participating group, no match.
  for (const auto& [pat, subj, fmt] :
       std::vector<std::tuple<std::string, std::string, std::string>> {
    {"(a)(b)(c)(d)(e)(f)(g)(h)(i)(j)(k)", "abcdefghijk", "$10>$11"},
    {"a", "a", "end$"},
    {"(a)(b)?(c)", "ac", "[$1$2$3]"},
    {"x", "yyy", "Z"},
  }) {
    EXPECT_EQ(rc::regex_replace(subj, rc::regex(pat), fmt),
              std::regex_replace(subj, std::regex(pat, std::regex::ECMAScript), fmt));
  }
}

TEST(compat_regex_iterator)
{
  // The span SEQUENCE (not just the first match) must equal std::sregex_iterator — the
  // empty-match advancement is the risk, gated by the nullable->std routing.
  const std::vector<std::pair<std::string, std::string>> cases {
    {R"(\w+)", "two  words here"}, {R"(\d+)", "a1b22c333"}, {"(a)(b)", "abab xab"},
    {"o", "foo boo"}, {"x*", "abc"}, {R"(\b\w)", "a bc def"}, {".", "ab"},
    {R"((\d{4})-(\d{2}))", "2026-06 1999-12"}, {"", "ab"}, {"a*", "baab"},
  };
  for (const auto& [pat, subj] : cases) {
    const rc::regex                    re(pat);
    std::vector<span_pair>             got;
    std::vector<span_pair>             ref;
    for (rc::sregex_iterator it(subj.begin(), subj.end(), re), e; it != e; ++it) {
      got.emplace_back(it->position(0), it->length(0));
    }
    const std::regex sre(pat, std::regex::ECMAScript);
    for (std::sregex_iterator it(subj.begin(), subj.end(), sre), e; it != e; ++it) {
      ref.emplace_back(it->position(0), it->length(0));
    }
    EXPECT(got == ref);
  }

  // Groups are exposed on the yielded match_results.
  const rc::regex          g("(\\w)(\\w)");
  const std::string        s {"abcd"};
  std::vector<std::string> g1;
  for (rc::sregex_iterator it(s.begin(), s.end(), g), e; it != e; ++it) {
    g1.push_back(it->str(1));
  }
  EXPECT_EQ(g1.size(), 2U);
  EXPECT_EQ(g1[0], "a");
  EXPECT_EQ(g1[1], "c");

  // cregex_iterator over a C string — real path (\d is non-nullable).
  const char*     cstr {"a1b2"};
  const rc::regex digit(R"(\d)");
  std::size_t     n    {0};
  for (rc::cregex_iterator it(cstr, cstr + 4, digit), e; it != e; ++it) {
    ++n;
  }
  EXPECT_EQ(n, 2U);

  // cregex_iterator std path: a nullable real-backed pattern routes the iterator to std, whose
  // empty-match advance is ECMAScript's. The span sequence must still equal std::cregex_iterator's.
  const rc::regex digits(R"(\d*)");
  EXPECT(!digits.uses_real_traversal());
  std::vector<span_pair> cgot;
  std::vector<span_pair> cref;
  for (rc::cregex_iterator it(cstr, cstr + 4, digits), e; it != e; ++it) {
    cgot.emplace_back(it->position(0), it->length(0));
  }
  const std::regex sdigits(R"(\d*)", std::regex::ECMAScript);
  for (std::cregex_iterator it(cstr, cstr + 4, sdigits), e; it != e; ++it) {
    cref.emplace_back(it->position(0), it->length(0));
  }
  EXPECT(cgot == cref);

  // Nullable pattern iterates via the std backend (empty-match traversal differs).
  EXPECT(!rc::regex("x*").uses_real_traversal());
}

namespace {

  // The token SEQUENCE (str + matched flag) from real::compat must equal std::sregex_token_iterator's.
  bool tokens_agree(const std::string&      pat,
                    const std::string&      subj,
                    const std::vector<int>& fields)
  {
    const rc::regex                           rre(pat);
    const std::regex                          sre(pat, std::regex::ECMAScript);
    std::vector<std::pair<std::string, bool>> got;
    std::vector<std::pair<std::string, bool>> ref;
    for (rc::sregex_token_iterator it(subj.begin(), subj.end(), rre, fields), e; it != e; ++it) {
      got.emplace_back(it->str(), it->matched);
    }
    for (std::sregex_token_iterator it(subj.begin(), subj.end(), sre, fields), e; it != e; ++it) {
      ref.emplace_back(it->str(), it->matched);
    }
    return got == ref;
  }
} // namespace

TEST(compat_regex_token_iterator)
{
  // Whole-match (field 0) tokenizing.
  EXPECT(tokens_agree(R"(\w+)", "two  words here", {0}));

  // Split on a separator (field -1): the prefix-between-matches semantics, incl. the trailing suffix.
  EXPECT(tokens_agree(",", "a,b,c", {-1}));                // a | b | c (trailing suffix "c" non-empty)
  EXPECT(tokens_agree(",", "a,b,", {-1}));                 // a | b | (trailing EMPTY suffix dropped by std)
  EXPECT(tokens_agree(",", ",a,b", {-1}));                 // leading empty field | a | b
  EXPECT(tokens_agree(",", "a,,b", {-1}));                 // a | EMPTY between adjacent | b
  EXPECT(tokens_agree(",", ",,", {-1}));                   // empty | empty (then empty suffix dropped)
  EXPECT(tokens_agree(R"(\s+)", "  x  y  ", {-1}));        // leading + trailing whitespace splits
  EXPECT(tokens_agree("x", "abc", {-1}));                  // no match: the whole string is one token
  EXPECT(tokens_agree("x", "", {-1}));                     // fuzzer-found: no match on empty -> one
  EXPECT(tokens_agree("a", "", {-1}));                     // empty token with matched=true (std rule)

  // Field list, cycled per match — and a non-participating group must yield an empty token.
  EXPECT(tokens_agree(R"((\w)(\w))", "abcd", {1, 2}));     // a,b,c,d
  EXPECT(tokens_agree(R"((\d)|([a-z]))", "1a2b", {1, 2})); // alternation: each match fills one of two groups
  EXPECT(tokens_agree(R"((\w+)=(\w+))", "k1=v1 k2=v2", {1, 2, -1}));

  // The std backend (nullable pattern) routes through the same wrapper.
  EXPECT(!rc::regex(R"(\w*)").uses_real_traversal());
  EXPECT(tokens_agree(R"(\w+)", "alpha beta", {0}));

  // cregex_token_iterator over a C string (split).
  const char             * cstr {"x:y:z"};
  const rc::regex          colon(":");
  std::vector<std::string> parts;
  for (rc::cregex_token_iterator it(cstr, cstr + 5, colon, -1), e; it != e; ++it) {
    parts.push_back(it->str());
  }
  EXPECT_EQ(parts.size(), 3U);
  EXPECT_EQ(parts[0], "x");
  EXPECT_EQ(parts[2], "z");

  // An empty field list behaves as the whole-match field {0}.
  const std::string        ws {"a b"};
  const rc::regex          word(R"(\w+)");
  std::vector<std::string> empty_subs_tokens;
  for (rc::sregex_token_iterator it(ws.begin(), ws.end(), word, std::vector<int> {}), e; it != e;
       ++it) {
    empty_subs_tokens.push_back(it->str());
  }
  EXPECT_EQ(empty_subs_tokens.size(), 2U);
  EXPECT_EQ(empty_subs_tokens[0], "a");

  // Two non-end iterators over the same input compare equal until one advances (full ==/!= path).
  rc::sregex_token_iterator a(ws.begin(), ws.end(), word, 0);
  rc::sregex_token_iterator b(ws.begin(), ws.end(), word, 0);
  EXPECT(a == b);
  ++a;
  EXPECT(a != b);

  // Incrementing an end iterator is a harmless no-op (defensive guard).
  rc::sregex_token_iterator end_it;
  ++end_it;
  EXPECT(end_it == rc::sregex_token_iterator {});
}

TEST(compat_wregex_always_std)
{
  // The real backend is eligible only for char + default traits; everything else is always std.
  static_assert(rc::detail::real_eligible<char, std::regex_traits<char>>);
  static_assert(!rc::detail::real_eligible<wchar_t, std::regex_traits<wchar_t>>);
  static_assert(!rc::detail::real_eligible<char, std::regex_traits<wchar_t>>); // custom/other traits

  // wregex is constructed straight on std (uses_real() is always false) and matches std::wregex.
  const std::vector<std::pair<std::wstring, std::wstring>> cases {
    {LR"((\w+)@(\w+))", L"foo@bar baz@qux"},
    {LR"(\d{4}-\d{2}-\d{2})", L"on 2026-06-30 ok"},
    {LR"([A-Za-z]+)", L"Hello World"},
  };
  for (const auto& [pat, subj] : cases) {
    const rc::wregex  wr(pat);
    EXPECT(!wr.uses_real()); // always std for wchar_t
    const std::wregex sw(pat, std::regex::ECMAScript);
    rc::wsmatch       wm;
    std::wsmatch      sm;
    EXPECT_EQ(rc::regex_search(subj, wm, wr), std::regex_search(subj, sm, sw));
    EXPECT_EQ(wm.size(), sm.size()); // a wsmatch fills via fill_from_std
    EXPECT_EQ(wm.position(0), sm.position(0));
    EXPECT_EQ(wm.length(0), sm.length(0));

    // Iterator span sequence parity.
    std::vector<span_pair> wspans;
    std::vector<span_pair> sspans;
    for (rc::wsregex_iterator it(subj.begin(), subj.end(), wr), e; it != e; ++it) {
      wspans.emplace_back(it->position(0), it->length(0));
    }
    for (std::wsregex_iterator it(subj.begin(), subj.end(), sw), e; it != e; ++it) {
      sspans.emplace_back(it->position(0), it->length(0));
    }
    EXPECT(wspans == sspans);
  }

  // Token split + replace parity on the wide path.
  const std::wstring        csv {L"a,bb,ccc"};
  const rc::wregex          comma(L",");
  std::vector<std::wstring> wparts;
  for (rc::wsregex_token_iterator it(csv.begin(), csv.end(), comma, -1), e; it != e; ++it) {
    wparts.push_back(it->str());
  }
  EXPECT_EQ(wparts.size(), 3U);
  EXPECT(wparts[2] == L"ccc");
  EXPECT(rc::regex_replace(csv, comma, std::wstring(L";"))
         == std::regex_replace(csv, std::wregex(L","), std::wstring(L";")));

  // regex_match on the wide path.
  EXPECT(rc::regex_match(std::wstring(L"abcd"), rc::wregex(LR"(\w+)")));
  EXPECT(!rc::regex_match(std::wstring(L"ab cd"), rc::wregex(LR"(\w+)")));
}

TEST(compat_nosubs_routes_to_std)
{
  // nosubs makes std report only group 0; real reports every group -> a structural divergence,
  // so nosubs routes to std. Verify the backend AND that m.size()==1 exactly like std.
  const rc::regex re(R"((\w+)@(\w+))", rc::regex_constants::nosubs);
  EXPECT(!re.uses_real()); // forced to std by the option screen

  rc::smatch        m;
  std::smatch       sm;
  const std::string subj {"user@host"};
  const std::regex  sre(R"((\w+)@(\w+))", std::regex::ECMAScript | std::regex::nosubs);
  EXPECT_EQ(rc::regex_search(subj, m, re), std::regex_search(subj, sm, sre));
  EXPECT_EQ(m.size(), sm.size()); // both expose only the whole match
  EXPECT_EQ(m.size(), 1U);
  EXPECT_EQ(m.position(0), sm.position(0));
  EXPECT_EQ(m.length(0), sm.length(0));
}

TEST(compat_posix_grammars_route_to_std)
{
  namespace rcc = rc::regex_constants;
  // grammar_forces_std routes POSIX grammars + collate to std up front; confirm the differential.
  struct Case
  {
    std::string                              pattern;
    std::string                              subject;
    rcc::syntax_option_type                  ropt;
    std::regex_constants::syntax_option_type sopt;
  };
  const std::vector<Case> cases {
    {.pattern = "[[:digit:]]+", .subject = "abc123", .ropt = rcc::extended, .sopt = std::regex_constants::extended},
    {.pattern = R"(\(ab\)*)", .subject = "ababab", .ropt = rcc::basic, .sopt = std::regex_constants::basic},
    {.pattern = "a+b", .subject = "aaab", .ropt = rcc::extended, .sopt = std::regex_constants::extended},
  };
  for (const Case& c : cases) {
    const rc::regex re(c.pattern, c.ropt);
    EXPECT(!re.uses_real()); // POSIX grammar -> std
    const std::regex  sre(c.pattern, c.sopt);
    rc::smatch        m;
    std::smatch       sm;
    EXPECT_EQ(rc::regex_search(c.subject, m, re), std::regex_search(c.subject, sm, sre));
    if (rc::regex_search(c.subject, m, re)) {
      EXPECT_EQ(m.position(0), sm.position(0));
      EXPECT_EQ(m.length(0), sm.length(0));
    }
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
  EXPECT_EQ(whole.compare(sm[1]), 0);           // compare(const sub_match&): sm[0] == sm[1] == "hello"
  rc::smatch        two;
  const std::string subj2 {"abc12"};
  const rc::regex   twore("([a-z]+)(\\d+)");
  EXPECT(rc::regex_search(subj2, two, twore));
  EXPECT(two[1].compare(two[2]) > 0);           // "abc" > "12" (sub_match vs sub_match ordering)
  EXPECT(two[2].compare(two[1]) < 0);
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
  // Invalid for real AND std -> compat::regex_error (a std::regex_error). Because the layer always
  // falls back to std before throwing, the thrown error wraps std's: .code() is std's exact code.
  const auto code_of {[](auto make) -> int {
                        try {
                          make();
                        }
                        catch (const std::regex_error& e) {
                          return static_cast<int>(e.code()); // inherited std::regex_error::code()
                        }
                        return -1; // did not throw
                      }};
  const int std_code    {code_of([] { (void)std::regex("(unclosed", std::regex::ECMAScript); })};
  const int compat_code {code_of([] { (void)rc::regex("(unclosed"); })};
  EXPECT(compat_code != -1);         // it threw
  EXPECT_EQ(compat_code, std_code);  // and reports std's exact code

  // .code() by category (not just unclosed-paren): WHEN compat throws (invalid for BOTH backends),
  // it must carry std's exact code. A pattern real accepts but std rejects is a benign real-superset
  // (compat does not throw) and is skipped — the throw path is exactly "std also rejected".
  for (const char* pat : {"(unclosed", "[unterminated", "a{2,1}", "a{", "*nostart", R"(a\)"}) {
    const int cc {code_of([pat] { (void)rc::regex(pat); })};
    if (cc == -1) {
      continue; // real accepted it (benign superset) -> compat did not throw
    }
    const int sc {code_of([pat] { (void)std::regex(pat, std::regex::ECMAScript); })};
    EXPECT_EQ(cc, sc); // same category code std reports (compat only throws when std also rejects)
  }
}

namespace {
  // The out-of-range / unmatched sub_match m[n]. real::compat is real-backed, so its match_results is
  // built from real's offsets (fill_from_real), not the local std — it anchors the sentinel at the
  // sequence END universally ({end, end, false}, position == length). std's sentinel is
  // platform-variant: libstdc++/libc++ anchor it at the end too, but MSVC leaves it singular
  // (position 0). So this asserts compat's CHOSEN end-anchored behaviour DIRECTLY (the regression
  // guard that caught the S7 singular-sentinel bug) and differs against std only on the invariants
  // that hold on every stdlib — .first/.second/.position are NOT compared against std.
  void expect_end_anchored_sentinel(const rc::smatch&           m,
                                    const std::smatch&          sm,
                                    std::string::const_iterator cend,
                                    std::size_t                 text_size,
                                    std::size_t                 n)
  {
    // (1) compat's chosen behaviour, asserted directly (not against a platform-variant std).
    EXPECT(m[n].first == cend);
    EXPECT(m[n].second == cend);
    EXPECT(!m[n].matched);
    EXPECT_EQ(m.position(n), static_cast<std::ptrdiff_t>(text_size));
    EXPECT_EQ(m.length(n), 0);
    EXPECT(m.str(n).empty());
    // (2) invariants shared with std on every platform.
    EXPECT_EQ(m[n].matched, sm[n].matched); // unmatched everywhere
    EXPECT_EQ(m.str(n), sm.str(n));         // empty everywhere
  }
} // namespace

TEST(compat_match_results_out_of_range)
{
  // S7-1: operator[]/position/length/str for n >= size() (and after a failed match) must never be
  // out-of-bounds and must be end-anchored ({end, end, false}, position == length). Asserted directly
  // (regression guard for the S7 singular-sentinel bug); the std differential is invariant-only, since
  // std's own OOB sentinel is platform-variant (end-anchored on libstdc++/libc++, singular on MSVC).
  const std::string subj   {"hello"};     // length 5 -> a wrong position-0 sentinel is obvious
  const std::size_t n_text {subj.size()};

  // (a) Successful multi-group match: in-range non-participating group + out-of-range indices.
  const rc::regex  re(R"((\w)(\d)?)"); // group 2 optional -> non-participating on "h"
  const std::regex sre(R"((\w)(\d)?)", std::regex::ECMAScript);
  rc::smatch       m;
  std::smatch      sm;
  EXPECT(rc::regex_search(subj, m, re));
  EXPECT(std::regex_search(subj, sm, sre));
  EXPECT_EQ(m.size(), 3U);
  expect_end_anchored_sentinel(m, sm, subj.cend(), n_text, 2); // non-participating in-range
  for (const std::size_t n : {m.size(), m.size() + 1, m.size() + 5}) {
    expect_end_anchored_sentinel(m, sm, subj.cend(), n_text, n); // out-of-range
  }

  // (b) After a FAILED search: m[0] and out-of-range indices are the same end-anchored sentinel.
  const rc::regex  nore(R"((x)(y))");
  const std::regex snore(R"((x)(y))", std::regex::ECMAScript);
  rc::smatch       fm;
  std::smatch      sfm;
  EXPECT(!rc::regex_search(subj, fm, nore));
  EXPECT(!std::regex_search(subj, sfm, snore));
  for (const std::size_t n : {std::size_t {0}, std::size_t {1}, std::size_t {7}}) {
    expect_end_anchored_sentinel(fm, sfm, subj.cend(), n_text, n);
  }
}

namespace {

  // Compare a token field-selector list compat vs std::sregex_token_iterator (str + matched).
  bool token_fields_agree(const std::string&      pat,
                          const std::string&      subj,
                          const std::vector<int>& fields)
  {
    const rc::regex                           rre(pat);
    const std::regex                          sre(pat, std::regex::ECMAScript);
    std::vector<std::pair<std::string, bool>> got;
    std::vector<std::pair<std::string, bool>> ref;
    for (rc::sregex_token_iterator it(subj.begin(), subj.end(), rre, fields), e; it != e; ++it) {
      got.emplace_back(it->str(), it->matched);
    }
    for (std::sregex_token_iterator it(subj.begin(), subj.end(), sre, fields), e; it != e; ++it) {
      ref.emplace_back(it->str(), it->matched);
    }
    return got == ref;
  }
} // namespace

TEST(compat_token_field_selectors)
{
  // B2: field selectors incl. out-of-range and mixed with -1 (no guessing — differential vs std).
  EXPECT(token_fields_agree(R"((\w)(\w))", "abcd", {2}));      // in-range group 2
  EXPECT(token_fields_agree(R"((\w)(\w))", "abcd", {1, 2}));   // list
  EXPECT(token_fields_agree(R"((\w)(\w))", "abcd", {5}));      // out-of-range -> unmatched tokens
  EXPECT(token_fields_agree(R"((\w)(\w))", "abcd", {1, 5}));   // mixed valid + out-of-range
  EXPECT(token_fields_agree(",", "a,b", {1, -1}));             // valid-group + split, WITH matches
  EXPECT(token_fields_agree(",", "a,b", {-1, 1}));             // split + group, order matters

  // Deep token-OOB (via set_field's operator[]): the sub_match tokens for an out-of-range field {5}
  // must match std byte-for-byte (first/second offsets + matched), not just str — the S7 sentinel.
  {
    const std::string                         subj {"abcd"};
    const rc::regex                           rre(R"((\w)(\w))");
    const std::regex                          sre(R"((\w)(\w))", std::regex::ECMAScript);
    // ptrdiff_t (not long): distance() is ptrdiff_t, which narrows to 32-bit long on MSVC-LLP64 under /WX.
    std::vector<std::tuple<std::ptrdiff_t, std::ptrdiff_t, bool>> got;
    std::vector<std::tuple<std::ptrdiff_t, std::ptrdiff_t, bool>> ref;
    for (rc::sregex_token_iterator it(subj.begin(), subj.end(), rre, 5), e; it != e; ++it) {
      got.emplace_back(std::distance(subj.cbegin(), it->first), std::distance(subj.cbegin(), it->second),
                       it->matched);
    }
    for (std::sregex_token_iterator it(subj.begin(), subj.end(), sre, 5), e; it != e; ++it) {
      ref.emplace_back(std::distance(subj.cbegin(), it->first), std::distance(subj.cbegin(), it->second),
                       it->matched);
    }
    EXPECT(got == ref);
  }

  // NO-match + a list containing -1: the standard ([re.tokiter.cnstr] "one of the elements is -1")
  // yields exactly ONE whole-sequence token. libstdc++ conforms; libc++ has a bug (checks only
  // subs[0], dropping the token for {1,-1}), so this is pinned explicitly, not differential.
  for (const std::vector<int>& fields : {std::vector<int> {1, -1}, std::vector<int> {-1, 1}}) {
    const std::string                         nomatch {"a,b"};
    const rc::regex                           none("x");
    std::vector<std::pair<std::string, bool>> toks;
    for (rc::sregex_token_iterator it(nomatch.begin(), nomatch.end(), none, fields), e; it != e;
         ++it) {
      toks.emplace_back(it->str(), it->matched);
    }
    EXPECT_EQ(toks.size(), 1U);         // exactly one token (no field cycling past the end)
    EXPECT(toks[0].first == "a,b");     // the whole sequence
    EXPECT(toks[0].second);             // matched == true
  }

  // A field < -1 is undefined in std; compat is SAFE (yields an unmatched token, never OOB).
  const std::string                         subj {"abcd"};
  const rc::regex                           two(R"((\w)(\w))");
  std::vector<std::pair<std::string, bool>> neg;
  for (rc::sregex_token_iterator it(subj.begin(), subj.end(), two, std::vector<int> {-2}), e; it != e;
       ++it) {
    neg.emplace_back(it->str(), it->matched);
  }
  for (const auto& [str, matched] : neg) {
    EXPECT(!matched); // safe unmatched tokens, no crash / OOB
    EXPECT(str.empty());
  }
}

namespace {
  // Detects whether regex_search(<StrRef>, m, re, flag) is well-formed for the given value category
  // (std::forward preserves lvalue/rvalue-ness of the string argument).
  template <typename StrRef>
  concept FlagSearchable = requires (StrRef s, rc::smatch& m, const rc::regex& re) {
    rc::regex_search(std::forward<StrRef>(s), m, re, rc::regex_constants::match_default);
  };
  template <typename StrRef>
  concept FlagMatchable = requires (StrRef s, rc::smatch& m, const rc::regex& re) {
    rc::regex_match(std::forward<StrRef>(s), m, re, rc::regex_constants::match_default);
  };
} // namespace

TEST(compat_rvalue_with_flags_rejected_at_compile_time)
{
  // B3: matching a temporary string with a match flag must be =delete'd (else the match_results
  // would dangle). The lvalue form stays callable; the rvalue form is rejected.
  static_assert(FlagSearchable<std::string&>);    // lvalue: OK
  static_assert(!FlagSearchable<std::string &&>); // rvalue + flag: deleted
  static_assert(FlagMatchable<std::string&>);
  static_assert(!FlagMatchable<std::string &&>);
  EXPECT(true);                                   // compile-time assertions above are the test
}

TEST(compat_replace_dollar_zero_and_sed_route_to_std)
{
  // B4: `$0` is platform-variant, so it routes to std (compat == std, never silently dropped).
  const rc::regex   word(R"(\w+)");
  EXPECT(word.uses_real_traversal()); // real-eligible: the screen (not the pattern) is what routes it
  const std::regex  sword(R"(\w+)", std::regex::ECMAScript);
  const std::string subj {"ab cd"};
  EXPECT_EQ(rc::regex_replace(subj, word, std::string("<$0>")),
            std::regex_replace(subj, sword, std::string("<$0>")));
  // $$ is NOT $0 and stays on the real expander (still equals std).
  EXPECT_EQ(rc::regex_replace(subj, word, std::string("<$$>")),
            std::regex_replace(subj, sword, std::string("<$$>")));

  // B5: format_sed uses POSIX replacement syntax (`&` = whole match); the ECMAScript expander would
  // mis-read it, so it routes to std.
  EXPECT_EQ(rc::regex_replace(subj, word, std::string("<&>"), rc::regex_constants::format_sed),
            std::regex_replace(subj, sword, std::string("<&>"), std::regex_constants::format_sed));

  // Multi-digit $NN (fuzzer-found): `$` takes up to 2 digits greedily; a reference to a group that
  // does not exist expands to empty (not a fall-back to $N + literal). Differential vs std.
  const rc::regex   two(R"((a)(b))");
  const std::regex  stwo(R"((a)(b))", std::regex::ECMAScript);
  const std::string ab {"ab"};
  for (const char* f : {"$15x", "$99", "$12", "$1x", "$3", "[$1|$2]", "x$1"}) {
    EXPECT_EQ(rc::regex_replace(ab, two, std::string(f)), std::regex_replace(ab, stwo, std::string(f)));
    // Also on a groupless pattern (every $N is out of range -> empty).
    EXPECT_EQ(rc::regex_replace(subj, word, std::string(f)), std::regex_replace(subj, sword, std::string(f)));
  }
}

TEST(compat_replace_honors_match_flags)
{
  // S6-B1: regex_replace on a real-backed pattern must honor constraining match flags (route to std)
  // — not silently ignore them. Differential vs std::regex_replace for each such flag.
  namespace mc = rc::regex_constants;
  namespace sc = std::regex_constants;
  const rc::regex  word(R"(\w+)");
  const std::regex sword(R"(\w+)", std::regex::ECMAScript);
  EXPECT(word.uses_real_traversal()); // real-eligible; the flag (not the pattern) is what routes it

  struct FlagPair
  {
    mc::match_flag_type r;
    sc::match_flag_type s;
  };
  const std::vector<FlagPair> flags {
    {.r = mc::match_not_bol, .s = sc::match_not_bol},
    {.r = mc::match_not_eol, .s = sc::match_not_eol},
    {.r = mc::match_continuous, .s = sc::match_continuous},
    {.r = mc::match_prev_avail, .s = sc::match_prev_avail},
    {.r = mc::match_not_null, .s = sc::match_not_null},
    {.r = mc::match_not_bol | mc::match_not_eol, .s = sc::match_not_bol | sc::match_not_eol},
  };
  for (const std::string& subj : {std::string("ab cd ef"), std::string("^start end$")}) {
    for (const FlagPair& f : flags) {
      EXPECT_EQ(rc::regex_replace(subj, word, std::string("<$&>"), f.r),
                std::regex_replace(subj, sword, std::string("<$&>"), f.s));
    }
    // format_first_only / format_no_copy DO stay on real; still equal to std.
    EXPECT_EQ(rc::regex_replace(subj, word, std::string("<$&>"), mc::format_first_only),
              std::regex_replace(subj, sword, std::string("<$&>"), sc::format_first_only));
    EXPECT_EQ(rc::regex_replace(subj, word, std::string("<$&>"), mc::format_no_copy),
              std::regex_replace(subj, sword, std::string("<$&>"), sc::format_no_copy));
  }
}

TEST(compat_ready_after_failed_match)
{
  // B6: after a failed search/match std leaves ready()==true, size()==0. Ours must too.
  const rc::regex   re("abc"); // real-backed
  rc::smatch        m;
  const std::string subj {"zzz"};
  EXPECT(!rc::regex_search(subj, m, re));
  EXPECT(m.ready());
  EXPECT_EQ(m.size(), 0U);
  EXPECT(m.empty());

  rc::smatch m2;
  EXPECT(!rc::regex_match(subj, m2, re));
  EXPECT(m2.ready());
  EXPECT_EQ(m2.size(), 0U);

  // Same on the std backend (a fallback pattern).
  const rc::regex   back(R"((a)\1)"); // backref -> std
  EXPECT(!back.uses_real());
  rc::smatch        m3;
  const std::string subj2 {"xyz"};
  EXPECT(!rc::regex_search(subj2, m3, back));
  EXPECT(m3.ready());
  EXPECT_EQ(m3.size(), 0U);
}

TEST(compat_late_std_error_is_homogeneous)
{
  // A pattern real ACCEPTS but std REJECTS (a *real superset*) runs search/match on real. It reaches
  // std only via a constraining flag / nullable replace-iterate, where the std build fails at runtime
  // — that must surface as a compat::regex_error (homogeneous with the ctor path), never a raw
  // std::regex_error and never a silent wrong result. The exact real-superset set is stdlib-dependent
  // (libc++ rejects `\A`/`\a`; libstdc++ differs), so we find one at runtime.
  const auto std_rejects {[](const std::string& p) {
                            try {
                              (void)std::regex(p, std::regex::ECMAScript);
                              return false;
                            }
                            catch (const std::regex_error&) {
                              return true;
                            }
                          }};
  std::string super; // real-accepted (literal), std-rejected
  for (const char* cand : {R"(\A)", R"(\Z)", R"(\a)"}) {
    if (rc::regex(cand).uses_real() && std_rejects(cand)) {
      super = cand;
      break;
    }
  }
  if (super.empty()) {
    // No real-superset escape on this stdlib (libstdc++ accepts `\A`/`\Z`/`\a`; libc++ rejects them),
    // so the late-std-throw path is not reachable here — nothing to assert. The wrap is exercised on
    // the libc++ build (the coverage build), where such patterns exist.
    return;
  }

  const rc::regex   re(super);
  EXPECT(re.uses_real());
  const std::string txt {"xAZy"};
  EXPECT(rc::regex_search(txt, re)); // flag-free search runs on real, no std needed

  bool       threw_compat {false};
  rc::smatch m;
  try {
    (void)rc::regex_search(txt, m, re, rc::regex_constants::match_not_bol); // flag -> std -> throws
  }
  catch (const rc::regex_error&) {                                          // the compat type specifically (derives from std::regex_error)
    threw_compat = true;
  }
  EXPECT(threw_compat);

  // Nullable real-superset (super + `*`): no std backend is built at construction (since S6b the
  // std engine is lazy, per operation), so search still runs on real, and replace surfaces the
  // wrapped error only when it actually builds and uses the std engine.
  const rc::regex nullable_super(super + "*");
  EXPECT(nullable_super.uses_real());
  EXPECT(rc::regex_search(txt, nullable_super)); // real path, fine (nullable matches empty)
  bool threw_compat2 {false};
  try {
    (void)rc::regex_replace(txt, nullable_super, std::string("x"));
  }
  catch (const rc::regex_error&) {
    threw_compat2 = true;
  }
  EXPECT(threw_compat2);
}

TEST(compat_all_std_only_paths_throw_compat_error)
{
  // S6-S2: EVERY std-only construction path (POSIX-screen, wide/custom-traits) must throw a
  // compat::regex_error on an invalid pattern — not a raw std::regex_error leaking out.
  const auto throws_compat {[](auto make) {
                              try {
                                make();
                              }
                              catch (const rc::regex_error&) { // the compat type specifically (derives from std::regex_error)
                                return true;
                              }
                              catch (...) {
                                return false; // a non-compat throw is the bug this pins
                              }
                              return false;
                            }};

  // POSIX grammar screen -> emplace_std with an invalid pattern.
  EXPECT(throws_compat([] { (void)rc::regex("(", rc::regex_constants::extended); }));
  EXPECT(throws_compat([] { (void)rc::regex("a{", rc::regex_constants::extended); }));
  // Wide / always-std path.
  EXPECT(throws_compat([] { (void)rc::wregex(L"("); }));
  EXPECT(throws_compat([] { (void)rc::wregex(L"[z-"); }));
}

TEST(compat_iterator_equality_conformance)
{
  // S6-S3: two non-end iterators are equal only for the same regex + sequence + current match — not
  // for a coincidental same position/length (regex_iterator) or same current token (token_iterator).
  const std::string s {"aa"};
  const rc::regex   ra("a");
  const rc::regex   rb("a"); // same pattern, DIFFERENT object

  rc::sregex_iterator ia(s.begin(), s.end(), ra);
  rc::sregex_iterator ia_copy(s.begin(), s.end(), ra);
  rc::sregex_iterator ib(s.begin(), s.end(), rb);
  EXPECT(ia == ia_copy);  // same regex + range + position
  EXPECT(ia != ib);       // different regex object -> not equal despite identical position/length
  ++ia_copy;
  EXPECT(ia != ia_copy);  // advanced -> different position

  // A': two iterators over the SAME regex + text but DIFFERENT match flags are not equal (std
  // distinguishes them on libc++ and libstdc++); same flags -> equal.
  rc::sregex_iterator idef(s.begin(), s.end(), ra, rc::regex_constants::match_default);
  rc::sregex_iterator idef2(s.begin(), s.end(), ra, rc::regex_constants::match_default);
  rc::sregex_iterator inbol(s.begin(), s.end(), ra, rc::regex_constants::match_not_bol);
  EXPECT(idef == idef2); // same flags
  EXPECT(idef != inbol); // different match flags -> not equal (conformance)

  // token_iterator: same current token but different field lists -> not equal.
  rc::sregex_token_iterator ta(s.begin(), s.end(), ra, 0);
  rc::sregex_token_iterator tb(s.begin(), s.end(), ra, std::vector<int> {0, -1});
  EXPECT_EQ(ta->str(), tb->str()); // both yield group 0 == "a" first
  EXPECT(ta != tb);                // but the field lists differ
  rc::sregex_token_iterator ta_copy(s.begin(), s.end(), ra, 0);
  EXPECT(ta == ta_copy);
}

// NB: the concurrent std_engine() thread-safety check (S6-S4) lives in tests/tsan_compat.cpp, run by
// `make tsan` under ThreadSanitizer. It is kept out of this suite because test_static.cpp overrides a
// non-atomic global operator-new counter (for the zero-allocation tests), which any concurrent
// allocation here would race — a test-harness artifact, not a library race. The standalone target
// links neither, so the check is clean and reproducible.

TEST(compat_replace_and_token_depth)
{
  // S6-S6: deeper differential coverage — complex formats x format_* combos, full token sequences,
  // mark_count() on the fallback, multi-group patterns.
  const rc::regex   multi(R"((\d{4})-(\d{2})-(\d{2}))");
  const std::regex  smulti(R"((\d{4})-(\d{2})-(\d{2}))", std::regex::ECMAScript);
  const std::string dates {"2026-06-30 and 1999-12-31"};
  for (const char* f : {"[$3/$2/$1]", "$1$2$3", "<$&:$`|$'>", "$1-$1", "x"}) {
    EXPECT_EQ(rc::regex_replace(dates, multi, std::string(f)),
              std::regex_replace(dates, smulti, std::string(f)));
    EXPECT_EQ(rc::regex_replace(dates, multi, std::string(f), rc::regex_constants::format_first_only),
              std::regex_replace(dates, smulti, std::string(f), std::regex_constants::format_first_only));
    EXPECT_EQ(rc::regex_replace(dates, multi, std::string(f), rc::regex_constants::format_no_copy),
              std::regex_replace(dates, smulti, std::string(f), std::regex_constants::format_no_copy));
  }

  // Full token sequence over the multi-group pattern, fields {1,3} and {-1}.
  for (const std::vector<int>& fields : {std::vector<int> {1, 3}, std::vector<int> {-1}}) {
    std::vector<std::string> got;
    std::vector<std::string> ref;
    for (rc::sregex_token_iterator it(dates.begin(), dates.end(), multi, fields), e; it != e; ++it) {
      got.push_back(it->str());
    }
    for (std::sregex_token_iterator it(dates.begin(), dates.end(), smulti, fields), e; it != e; ++it) {
      ref.push_back(it->str());
    }
    EXPECT(got == ref);
  }

  // mark_count() must match std on BOTH backends (real-backed multi-group, and a std fallback).
  EXPECT_EQ(multi.mark_count(), smulti.mark_count());
  const rc::regex   backref(R"((a)\1(b))"); // backref -> std fallback
  const std::regex  sbackref(R"((a)\1(b))", std::regex::ECMAScript);
  EXPECT(!backref.uses_real());
  EXPECT_EQ(backref.mark_count(), sbackref.mark_count());
}
