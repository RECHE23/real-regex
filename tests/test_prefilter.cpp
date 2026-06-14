// Prefilter: activation tests (is the right acceleration *selected*? —
// historically, silent mis-selection was the worst kind of bug: everything
// correct, just slow), equivalence tests (hints never change results), and
// a throughput smoke test.
#include <chrono>
#include <string>
#include <string_view>

#include "framework.hpp"
#include "real/real.hpp"

using namespace std::string_view_literals;
using real::detail::dynamic_storage;

namespace {

  real::detail::pattern_hints hints_of(std::string_view pattern,
                                       real::flags      f = real::flags::none)
  {
    return dynamic_storage::compile(pattern, f).program.hints;
  }
} // namespace

TEST(prefix_literal_is_extracted)
{
  const auto h = hints_of("foo\\d+");
  EXPECT_EQ(static_cast<int>(h.prefix_size), 3);
  EXPECT_EQ(std::string_view(h.prefix.data(), h.prefix_size), "foo"sv);
  EXPECT_EQ(h.single_first, static_cast<std::int16_t>('f'));
  EXPECT(h.first_bytes_valid);
  // icase letters compile to classes: no literal prefix anymore.
  EXPECT_EQ(static_cast<int>(hints_of("foo", real::flags::icase).prefix_size), 0);
  // The prefix crosses saves and assertions but stops at variability.
  EXPECT_EQ(static_cast<int>(hints_of("^abc?").prefix_size), 2);
  EXPECT_EQ(static_cast<int>(hints_of("(ab)cd").prefix_size), 4);
  EXPECT_EQ(static_cast<int>(hints_of("foo\\bbar").prefix_size), 6);
}

TEST(anchoring_is_detected)
{
  EXPECT(hints_of("\\Aabc").anchored_start);
  EXPECT(hints_of("^abc").anchored_start); // no multiline: text start
  EXPECT(!hints_of("^abc", real::flags::multiline).anchored_start);
  EXPECT(hints_of("^abc", real::flags::multiline).line_anchored);
  EXPECT(!hints_of("abc").anchored_start);
}

TEST(first_byte_set_is_computed)
{
  const auto h = hints_of("[ab]x|cy");
  EXPECT(h.first_bytes_valid);
  EXPECT(h.first_bytes.test('a'));
  EXPECT(h.first_bytes.test('b'));
  EXPECT(h.first_bytes.test('c'));
  EXPECT(!h.first_bytes.test('x'));
  EXPECT_EQ(h.single_first, std::int16_t {-1}); // not unique
  const auto digits = hints_of("\\d{4}");
  EXPECT(digits.first_bytes_valid);
  EXPECT(digits.first_bytes.test('7'));
  EXPECT(!digits.first_bytes.test('a'));
}

TEST(nullable_patterns_disable_byte_skipping)
{
  EXPECT(!hints_of("a*").first_bytes_valid); // empty match possible
  EXPECT(!hints_of("a|").first_bytes_valid);
  EXPECT(!hints_of("^", real::flags::multiline).first_bytes_valid);
}

TEST(class_loop_fast_path_activation)
{
  // \w+ / \d+ / [a-f]+ are matched by a plain scan loop. Greedy only, no
  // captures: the lazy variant and grouped forms must stay on the VM.
  EXPECT(hints_of("\\w+").greedy_class_loop >= 0);
  EXPECT(hints_of("[0-9a-f]+").greedy_class_loop >= 0);
  EXPECT(hints_of("\\d+", real::flags::icase).greedy_class_loop >= 0);
  EXPECT_EQ(hints_of("\\w+?").greedy_class_loop, -1); // lazy: different result
  EXPECT_EQ(hints_of("(\\w)+").greedy_class_loop, -1);
  EXPECT_EQ(hints_of("\\w*").greedy_class_loop, -1);  // nullable
  EXPECT_EQ(hints_of("\\w+x").greedy_class_loop, -1);
}

TEST(class_loop_fast_path_results_match_python_semantics)
{
  const real::regex rx("\\w+");
  EXPECT_EQ(rx.search("  héllo ")[0], "h"sv); // \w is ASCII: é stops it
  EXPECT_EQ(rx.search("a_1 b")[0], "a_1"sv);
  EXPECT(!rx.search("  é "));                 // no ASCII word byte at all
  EXPECT(rx.fullmatch("abc"));
  EXPECT(!rx.fullmatch("abc "));
  EXPECT(!rx.match(" abc"));
  EXPECT_EQ(rx.find_all("a bb 𝄞 ccc").size(), 3U);
}

TEST(optional_head_keeps_first_bytes)
{
  // a?b: first byte is 'a' or 'b'; no empty match. Skipping stays sound.
  const auto h = hints_of("a?b");
  EXPECT(h.first_bytes_valid);
  EXPECT(h.first_bytes.test('a'));
  EXPECT(h.first_bytes.test('b'));
  EXPECT(!h.first_bytes.test('c'));
}

TEST(line_anchored_candidates_scan_newlines)
{
  // ^[ab]… in multiline: no usable prefix or single first byte, so the
  // engine jumps from newline to newline.
  const auto h = hints_of("^[ab]+x", real::flags::multiline);
  EXPECT(h.line_anchored);
  EXPECT_EQ(static_cast<int>(h.prefix_size), 0);
  EXPECT_EQ(h.single_first, std::int16_t {-1});
  const real::regex rx("^[ab]+x", real::flags::multiline);
  EXPECT_EQ(rx.search("zz\nqq\nabx").start(), 6U);
  EXPECT(!rx.search("zz\nqq\nq abx"));
}

TEST(hints_never_change_results)
{
  // Same program, hints wiped: every search outcome must be identical.
  const std::string_view patterns[] = {
    "needle",
    "\\d{4}-\\d{2}",
    "(foo|bar)+",
    "^line",
    "x*",
    "\\bword\\b",
    "a?b",
    "[abc]+\\d",
    "z$",
    "(a)(b)?c",
  };
  const std::string_view texts[] = {
    "",
    "haystack with a needle in it",
    "2026-06-10 and foobar twice: foofoo barbar",
    "line one\nline two\nneedle 1234-56 word\n",
    "aaa bbb ccc abcabc7 zzz",
    "héllo wörld a?b ac abc z",
  };
  for (const auto& pattern : patterns) {
    const auto with       = dynamic_storage::compile(pattern, real::flags::multiline);
    auto       without    = with;
    without.program.hints = {};
    for (const auto& text : texts) {
      real::detail::pike_state s1;
      real::detail::pike_state s2;
      std::vector<std::size_t> r1;
      std::vector<std::size_t> r2;
      real::detail::pike_vm    vm1(with.view(), s1);
      real::detail::pike_vm    vm2(without.view(), s2);
      const bool               m1 = vm1.run(text, 0, real::detail::run_mode::search, r1);
      const bool               m2 = vm2.run(text, 0, real::detail::run_mode::search, r2);
      EXPECT_EQ(m1, m2);
      EXPECT(r1 == r2);
    }
  }
}

TEST(anchored_search_is_one_shot)
{
  // \A-anchored search must not scan the whole text.
  const std::string text(8 << 20, 'x'); // 8 MB of 'x'
  const real::regex rx("\\Ayes");
  const auto        begin = std::chrono::steady_clock::now();
  for (int i = 0; i < 1000; ++i) {
    EXPECT(!rx.search(text));
  }
  const auto elapsed = std::chrono::steady_clock::now() - begin;
  EXPECT(elapsed < std::chrono::seconds(1)); // ~µs each, generous bound
}

TEST(prefilter_rare_byte_literal)
{
  // Exercises the rare-byte anchor path in find_prefix for >=3 byte literal
  // (rare 'x' in mostly 'a's; 'x' is also rarest *in the literal* "aax").
  std::string text(1000, 'a');
  text[498] = 'a';
  text[499] = 'a';
  text[500] = 'x';
  std::string s = "aax";  // freq: a=2, x=1 → rarest 'x' at idx 2
  real::regex rx(s);
  auto        match = rx.search(text);
  EXPECT(match.matched());
  EXPECT_EQ(match.start(), 498U);
}

TEST(prefilter_rare_byte_with_rare_in_middle)
{
  // Additional case for rare-byte logic: rarest byte in middle of prefix.
  // "axa" : 'x' is rarest in literal (a=2, x=1), at idx=1 (middle).
  // Text has the sequence with 'x' also rare in surrounding.
  std::string text(200, 'a');
  text[99]  = 'a';
  text[100] = 'x';
  text[101] = 'a';
  real::regex rx("axa");
  auto        match = rx.search(text);
  EXPECT(match.matched());
  EXPECT_EQ(match.start(), 99U);
}

TEST(prefilter_rare_byte_rare_first)
{
  // Bonify for rare prefilter: case where rarest byte is first in prefix
  // (rare_idx=0), to hit that branch in the selection loop.
  std::string text(200, 'a');
  text[99]  = 'x';
  text[100] = 'a';
  text[101] = 'a';
  real::regex rx("xaa");  // 'x' rarest in literal (freq x=1, a=2), at idx 0
  auto        match = rx.search(text);
  EXPECT(match.matched());
  EXPECT_EQ(match.start(), 99U);
}

TEST(prefilter_rare_byte_cand_before_pos)
{
  // Exercises the rare path when a rare byte is found but cand < pos
  // (skip and continue to next rare). For "aax" (rare 'x' at idx 2),
  // first 'x' at 0 gives cand underflow/large, skipped; next at 3 gives
  // cand=1, match.
  std::string text = "xaax";
  real::regex rx("aax");
  auto        match = rx.search(text);
  EXPECT(match.matched());
  EXPECT_EQ(match.start(), 1U);
}

TEST(literal_prefilter_throughput_smoke)
{
  // 8 MB miss: must run at memchr-like speed, far beyond VM stepping.
  std::string text(8 << 20, 'a');
  text += "needle";
  const real::regex  rx("needle\\d?");
  const auto         begin     = std::chrono::steady_clock::now();
  auto               match     = rx.search(text);
  for (int i = 1; i < 20; ++i) {
    match = rx.search(text);
  }
  const auto elapsed = std::chrono::steady_clock::now() - begin;
  EXPECT(match.matched());
  EXPECT_EQ(match.start(), static_cast<std::size_t>(8 << 20));
  EXPECT(elapsed < std::chrono::seconds(2)); // 160 MB scanned in total
}

TEST(prefilter_works_in_constexpr_too)
{
  // These helpers exercise the prefilter (including rare-byte path) at constexpr time.
  // The regex construction/destruction ends up using std::string (in dynamic_storage::pattern_text).
  // On libstdc++ shipped with GCC <15 the string dtor is not a valid constant expression,
  // causing "not a constant expression" errors during ct evaluation.
  // We only force the constant evaluation (via static_assert on the helper) on compilers
  // where it is known to work. Runtime EXPECT runs on all.
  auto needle_ct = [] {
                     const real::regex rx("needle");
                     return rx.search("a long constexpr haystack with a needle inside").start() == 33;
                   };
  auto rare_ct = [] {
                   const real::regex rx("aax");
                   // "aaaax" : match at pos 2 ("aax" with 'x' rarest in literal)
                   return rx.search("aaaax").start() == 2;
                 };

#if !defined(__GNUC__) || defined(__clang__) || __GNUC__ >= 15
  static_assert(needle_ct());
  static_assert(rare_ct());
  const bool ok_val      = needle_ct();
  const bool ok_rare_val = rare_ct();
#else
  const bool ok_val      = needle_ct();
  const bool ok_rare_val = rare_ct();
#endif
  EXPECT(ok_val);
  EXPECT(ok_rare_val);
}

// --- global frequency table refinement for rare anchor selection -----------

TEST(prefilter_rare_byte_global_freq_prefers_rare_letters)
{
  // 'q' has very low global freq in the table (excellent anchor).
  // Literal "query" on 'e'-heavy text: global selection picks 'q' for large skips.
  std::string text(400, 'e');
  text[222] = 'q';
  text[223] = 'u';
  text[224] = 'e';
  text[225] = 'r';
  text[226] = 'y';
  real::regex rx("query");
  auto        match = rx.search(text);
  EXPECT(match.matched());
  EXPECT_EQ(match.start(), 222U);
}

TEST(prefilter_rare_byte_global_freq_punctuation_anchor)
{
  // Punctuation like '{' scores very low globally (15) — great prefilter anchor.
  // Mix with common letters; global rarity should drive the choice.
  std::string text(250, 'a');
  text[77]  = 'f';
  text[78]  = 'o';
  text[79]  = 'o';
  text[80]  = '{';
  text[81]  = 'b';
  text[82]  = 'a';
  text[83]  = 'r';
  real::regex rx("foo{bar");
  auto        match = rx.search(text);
  EXPECT(match.matched());
  EXPECT_EQ(match.start(), 77U);
}

TEST(prefilter_rare_byte_global_freq_prose_like)
{
  // More realistic: literal with a rare letter inside on mixed "prose" hay.
  // Ensures no regression and that search still finds correctly (prefilter only
  // affects speed, never results — already proven by hints_never_change_results).
  std::string hay =
    "The quick brown fox jumps over the lazy dog. "
    "Pack my box with five dozen liquor jugs. "
    "How vexingly quick daft zebras jump! ";
  // Place "quiz" ( 'q' and 'z' are globally rare ) at a known offset.
  const std::string lit    = "quiz";
  const std::size_t insert = 47;
  hay.replace(insert, lit.size(), lit);
  real::regex rx(lit);
  auto        match = rx.search(hay);
  EXPECT(match.matched());
  EXPECT_EQ(match.start(), insert);
}

TEST(exact_literal_fastpath_hint_and_results)
{
  // Plain literal: prefix == entire match → exact set, fast replay used.
  EXPECT_EQ(static_cast<int>(hints_of("needle").exact_literal_len), 6);
  EXPECT_EQ(static_cast<int>(hints_of("a").exact_literal_len), 1);
  // With internal groups: saves crossed in tail, still pure literal bytes.
  EXPECT_EQ(static_cast<int>(hints_of("(ne)(ed)le").exact_literal_len), 6);
  // Leading assert (^ or \A) crossed before bytes: still exact (next_candidate
  // + replay handle the assert at cand).
  EXPECT_EQ(static_cast<int>(hints_of("^needle").exact_literal_len), 6);
  EXPECT_EQ(static_cast<int>(hints_of("\\Aabc").exact_literal_len), 3);
  // Trailing assert or post-byte condition: do not claim exact (must let VM
  // filter/reject cands and continue search).
  EXPECT_EQ(static_cast<int>(hints_of("needle$").exact_literal_len), 0);
  EXPECT_EQ(static_cast<int>(hints_of("\\bword\\b").exact_literal_len), 0);
  EXPECT_EQ(static_cast<int>(hints_of("ne\\d+").exact_literal_len), 0);

  // Fastpath must still produce correct results (including groups).
  real::regex plain("needle");
  auto        m1 = plain.search("hay needle in stack");
  EXPECT(m1.matched());
  EXPECT_EQ(m1.start(), 4U);
  EXPECT_EQ(m1[0], "needle"sv);

  real::regex grouped("(ne)(ed)le");
  auto        m2 = grouped.search("xxneedleyy");
  EXPECT(m2.matched());
  EXPECT_EQ(m2.start(), 2U);
  EXPECT_EQ(m2[1], "ne"sv);
  EXPECT_EQ(m2[2], "ed"sv);

  // Anchored literal success via fastpath.
  real::regex anchored("^abc");
  auto        m3 = anchored.search("abc def");
  EXPECT(m3.matched());
  EXPECT_EQ(m3.start(), 0U);
}
