// Prefilter: activation tests (is the right acceleration *selected*? —
// historically, silent mis-selection was the worst kind of bug: everything
// correct, just slow), equivalence tests (hints never change results), and
// a throughput smoke test.
#include <algorithm>
#include <array>
#include <chrono>
#include <string>
#include <string_view>

#include <sciforge/test/framework.hpp>
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
  // A single ASCII class, or an ASCII-mode \w+/\d+, is matched by a plain scan loop. Greedy only, no
  // captures: the lazy variant and grouped forms must stay on the VM.
  EXPECT(hints_of("[0-9a-f]+").greedy_class_loop >= 0);
  EXPECT(hints_of("\\w+", real::flags::ascii).greedy_class_loop >= 0);
  EXPECT(hints_of("\\d+", real::flags::ascii | real::flags::icase).greedy_class_loop >= 0);
  // In text mode \w+ and \d+ are code-point predicates (klass_cp), not a single ASCII class, so the
  // scan-loop fast path does not apply (still linear on the general VM).
  EXPECT_EQ(hints_of("\\w+").greedy_class_loop, -1);
  EXPECT_EQ(hints_of("\\d+").greedy_class_loop, -1);
  EXPECT_EQ(hints_of("\\w+?", real::flags::ascii).greedy_class_loop, -1); // lazy: different result
  EXPECT_EQ(hints_of("(\\w)+").greedy_class_loop, -1);
  EXPECT_EQ(hints_of("\\w*").greedy_class_loop, -1);                      // nullable
  EXPECT_EQ(hints_of("\\w+x").greedy_class_loop, -1);
}

TEST(trailing_lookaround_class_loop_activation)
{
  // P3c: trailing-LA arms trailing_lookaround + trailing_la_class only — greedy_class_loop stays
  // −1 so the pure [a-z]+ call site remains a single compare (x86 −20 % when it shared the gate).
  EXPECT(hints_of("[a-z]+(?=[a-z])").trailing_lookaround >= 0);
  EXPECT(hints_of("[a-z]+(?=[a-z])").trailing_la_class >= 0);
  EXPECT_EQ(hints_of("[a-z]+(?=[a-z])").greedy_class_loop, -1);
  EXPECT(hints_of("[a-z]+(?![a-z])").trailing_lookaround >= 0);
  EXPECT(hints_of("[0-9]+(?![0-9])").trailing_lookaround >= 0);
  EXPECT(hints_of("[a-z]+(?=[a-z]{2})").trailing_lookaround >= 0); // multi-op LA sub still eligible
  // Pure class+ still on the original gate only:
  EXPECT(hints_of("[a-z]+").greedy_class_loop >= 0);
  EXPECT_EQ(hints_of("[a-z]+").trailing_lookaround, -1);
  // Declines (general VM):
  EXPECT_EQ(hints_of("(?=[a-z])[a-z]+").trailing_lookaround, -1);  // leading
  EXPECT_EQ(hints_of("(?=[a-z])[a-z]+").greedy_class_loop, -1);
  EXPECT_EQ(hints_of("(a+)(?=b)").trailing_lookaround, -1);        // capturing group
  EXPECT_EQ(hints_of("foo(?=bar)").trailing_lookaround, -1);       // literal body, not class+
  EXPECT_EQ(hints_of("\\d+(?=px)").trailing_lookaround, -1);       // klass_cp body (text-mode \\d)
}

TEST(class_loop_fast_path_results_match_python_semantics)
{
  const real::regex rx("\\w+");
  EXPECT_EQ(rx.search("  héllo ")[0], "héllo"sv); // \w is Unicode: é is a word char (klass_cp)
  EXPECT_EQ(rx.search("a_1 b")[0], "a_1"sv);
  EXPECT_EQ(rx.search("  é ")[0], "é"sv);         // é alone is a word run
  EXPECT(rx.fullmatch("abc"));
  EXPECT(!rx.fullmatch("abc "));
  EXPECT(!rx.match(" abc"));
  EXPECT_EQ(rx.find_all("a bb 𝄞 ccc").size(), 3U);
}

TEST(codepoint_class_fast_path_activation)
{
  // `.` and negated classes `[^…]` compile to the 16-instruction UTF-8 codepoint
  // block; when that block is the *whole* pattern (optionally a greedy `+`), the
  // engine takes a per-codepoint scan. The hint comes from a marker the compiler
  // records at emission — analyze_program never re-recognizes the bytecode shape.
  EXPECT(hints_of(".").codepoint_class_ascii >= 0);
  EXPECT(!hints_of(".").codepoint_class_plus);                           // a single codepoint
  EXPECT(hints_of(".+").codepoint_class_ascii >= 0);
  EXPECT(hints_of(".+").codepoint_class_plus);                           // the greedy `+` loop
  EXPECT(hints_of("[^x]").codepoint_class_ascii >= 0);                   // negated class: same block
  EXPECT(hints_of("[^x]+").codepoint_class_plus);
  EXPECT(hints_of(".", real::flags::dotall).codepoint_class_ascii >= 0); // still a fast path

  // The hint never changes results: `.` is one codepoint, never a partial byte.
  const real::regex dot(".");
  EXPECT_EQ(dot.find_all("a𝄞b").size(), 3U); // 'a', the 4-byte 𝄞, 'b'
}

TEST(codepoint_class_hint_requires_whole_pattern)
{
  // A codepoint block that is not the whole pattern must not get the hint, even
  // when the program is the same size as a qualifying one. `a.` is 20 instructions
  // — exactly `.+`'s size — but the block starts at offset 2, so the marker's
  // offset check rejects it; `.a` keeps offset 1 but fails the trailing-shape check.
  EXPECT_EQ(hints_of("a.").codepoint_class_ascii, -1);  // block not at the start
  EXPECT_EQ(hints_of(".a").codepoint_class_ascii, -1);  // trailing byte, not save/+
  EXPECT_EQ(hints_of("(.)").codepoint_class_ascii, -1); // captured: extra saves
  EXPECT_EQ(hints_of("a|.").codepoint_class_ascii, -1); // an alternation branch
  EXPECT_EQ(hints_of(".*").codepoint_class_ascii, -1);  // nullable: no consuming fast path
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
      real::detail::pike_state         s1;
      real::detail::pike_state         s2;
      std::vector<std::size_t>         r1;
      std::vector<std::size_t>         r2;
      const real::detail::program_view pv1 {with.view()};    // the VM borrows the view — keep it alive
      const real::detail::program_view pv2 {without.view()};
      real::detail::pike_vm            vm1(pv1, s1);
      real::detail::pike_vm            vm2(pv2, s2);
      const bool                       m1 = vm1.run(text, 0, real::detail::run_mode::search, r1);
      const bool                       m2 = vm2.run(text, 0, real::detail::run_mode::search, r2);
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

TEST(prefilter_find_prefix_literal)
{
  // find_prefix locates the required literal prefix as a substring (std::string_view::find,
  // i.e. memchr/memcmp); the engine then verifies from that candidate. "aax" in a run of 'a's.
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

TEST(prefilter_find_prefix_in_run)
{
  // The literal prefix "axa" is found as a substring inside a long run of 'a's.
  std::string text(200, 'a');
  text[99]  = 'a';
  text[100] = 'x';
  text[101] = 'a';
  real::regex rx("axa");
  auto        match = rx.search(text);
  EXPECT(match.matched());
  EXPECT_EQ(match.start(), 99U);
}

TEST(prefilter_find_prefix_distinct_first_byte)
{
  // The literal prefix "xaa" begins with a byte distinct from the surrounding run.
  std::string text(200, 'a');
  text[99]  = 'x';
  text[100] = 'a';
  text[101] = 'a';
  real::regex rx("xaa"); // literal prefix found as a substring at index 99
  auto        match = rx.search(text);
  EXPECT(match.matched());
  EXPECT_EQ(match.start(), 99U);
}

TEST(prefilter_find_prefix_candidate_before_pos)
{
  // A literal occurrence found by find_prefix can land before the scan position; the engine
  // skips it and continues. "aax" in "xaax" matches at index 1.
  std::string text = "xaax";
  real::regex rx("aax");
  auto        match = rx.search(text);
  EXPECT(match.matched());
  EXPECT_EQ(match.start(), 1U);
}

TEST(literal_prefilter_throughput_smoke)
{
  // The literal prefilter must scan a miss in O(n), not O(n²) per-position rescan.
  // Wall-clock ratios flaked twice on shared CI (median-of-7 still saw runner noise). Work is
  // now a compile-gated deterministic counter (REAL_TEST_INSTRUMENT on the test binary only):
  // each find_prefix / find_byte bills remaining haystack once; O(n) → large/small ≈ 8×,
  // quadratic restart → ≈ 64×. Margin 25× unchanged — no deserrage, noise-immune.
  const real::regex rx {"needle\\d?$"}; // $ keeps it off the lazy-DFA route → prefilter path
  const auto        work {[&](std::size_t n) -> std::uint64_t {
                            std::string text(n, 'a');
                            text += "needle";
                            real::detail::prefilter_work_units() = 0;
                            EXPECT(rx.search(text).matched());
                            return real::detail::prefilter_work_units();
                          }};

  (void) work(1 << 10);                      // warmup (first-call path setup); discarded
  const std::uint64_t small {work(1 << 20)}; // 1 MB miss
  const std::uint64_t large {work(8 << 20)}; // 8 MB miss — 8× the bytes

  const std::string check   {std::string(8 << 20, 'a') + "needle"};
  EXPECT_EQ(rx.search(check).start(), static_cast<std::size_t>(8 << 20));
  // O(n) → ~8×; O(n²) → ~64×. 25× bites quadratic, absorbs constant per-search overhead.
  EXPECT(large < small * 25);
  // Determinism pin: re-run large — same work count (not wall time).
  EXPECT_EQ(work(8 << 20), large);
}

TEST(prefilter_works_in_constexpr_too)
{
  // These helpers exercise the prefilter (including find_prefix) at constexpr time.
  // The regex construction/destruction ends up using std::string (in dynamic_storage::pattern_text).
  // On libstdc++ shipped with GCC <15 the string dtor is not a valid constant expression,
  // causing "not a constant expression" errors during ct evaluation.
  // We only force the constant evaluation (via static_assert on the helper) on compilers
  // where it is known to work. Runtime EXPECT runs on all.
  auto needle_ct = [] {
                     const real::regex rx("needle");
                     return rx.search("a long constexpr haystack with a needle inside").start() == 33;
                   };
  auto prefix_ct = [] {
                     const real::regex rx("aax");
                     // "aaaax": find_prefix locates "aax", match at pos 2.
                     return rx.search("aaaax").start() == 2;
                   };

#if !defined(__GNUC__) || defined(__clang__) || __GNUC__ >= 15
  static_assert(needle_ct());
  static_assert(prefix_ct());
  const bool ok_val        = needle_ct();
  const bool ok_prefix_val = prefix_ct();
#else
  const bool ok_val        = needle_ct();
  const bool ok_prefix_val = prefix_ct();
#endif
  EXPECT(ok_val);
  EXPECT(ok_prefix_val);
}

// --- find_prefix on word / punctuation / prose literals --------------------

TEST(prefilter_find_prefix_word)
{
  // The whole literal "query" is found as a substring in 'e'-heavy text.
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

TEST(prefilter_find_prefix_with_punctuation)
{
  // A literal prefix containing punctuation ("foo{bar") is found as a substring.
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

TEST(prefilter_find_prefix_prose)
{
  // A realistic literal in mixed prose; the prefilter only affects speed, never results
  // (already proven by hints_never_change_results).
  std::string hay =
    "The quick brown fox jumps over the lazy dog. "
    "Pack my box with five dozen liquor jugs. "
    "How vexingly quick daft zebras jump! ";
  // Place "quiz" at a known offset.
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
  // Trailing `$` (non-wb): still not exact — replay is for word-boundary wraps only (B1).
  EXPECT_EQ(static_cast<int>(hints_of("needle$").exact_literal_len), 0);
  // Trailing/leading `\b`: exact_literal + O(1) boundary check via replay.
  EXPECT_EQ(static_cast<int>(hints_of("\\bword\\b").exact_literal_len), 4);
  EXPECT_EQ(static_cast<int>(hints_of("\\bword\\b").wb_lead), 1);
  EXPECT_EQ(static_cast<int>(hints_of("\\bword\\b").wb_trail), 1);
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

TEST(enveloping_group_class_loop_fast_path)
{
  // A class-loop / cp-class-loop wrapped in ONE capturing group ((\w+), ([a-z]+)) takes the
  // scan-loop fast path, and the group's span equals the whole match (start(1)==start(0) etc). The
  // detection fires (greedy hint set with a group slot) only for that strict shape.
  EXPECT(hints_of("(\\w+)").greedy_cp_class >= 0 && hints_of("(\\w+)").greedy_group_start == 2);
  EXPECT(hints_of("([a-z]+)").greedy_class_loop >= 0 && hints_of("([a-z]+)").greedy_group_start == 2);
  EXPECT(hints_of("(\\w)").greedy_cp_class >= 0 && hints_of("(\\w)").greedy_group_start == 2);
  // Strictly excluded (must stay on the general VM): lazy, a trailing atom, nesting, a second group,
  // and a non-capturing group is just the plain loop (no group slots).
  EXPECT_EQ(hints_of("(\\w+?)").greedy_cp_class, -1);
  EXPECT_EQ(hints_of("(\\w+)x").greedy_cp_class, -1);
  EXPECT_EQ(hints_of("((\\w+))").greedy_cp_class, -1);
  EXPECT_EQ(hints_of("(\\w+)(\\w+)").greedy_cp_class, -1);
  EXPECT_EQ(hints_of("(?:\\w+)").greedy_group_start, std::int16_t {-1});
  // The group span equals the whole match, across ascii / cp-class / a single code point / no match.
  const real::regex rw {"(\\w+)"};
  const real::regex ra {"([a-z]+)"};
  const real::regex rd {"(\\d+)"};
  const std::string s1 {"  héllo!"};
  const std::string s2 {"  abc"};
  const std::string s3 {"x 42 y"};
  const std::string s4 {"   "};
  const auto        m1 {rw.search(s1)};
  EXPECT(m1.matched() && m1.start(1) == m1.start(0) && m1.end(1) == m1.end(0) && m1[1] == "héllo"sv);
  const auto m2        {ra.search(s2)};
  EXPECT(m2.start(1) == m2.start(0) && m2.end(1) == m2.end(0));
  const auto m3        {rd.search(s3)};
  EXPECT(m3[1] == "42"sv && m3.start(1) == m3.start(0) && m3.end(1) == m3.end(0));
  EXPECT(!rw.search(s4)); // no word: no match, group unset
}

TEST(fixed_shape_internal_saves_fast_path)
{
  // A fixed-width byte/klass sequence with interleaved capturing saves ((\d{4})-(\d{2})-(\d{2}),
  // (a)(b)) takes the fixed-shape fast path and fills each group slot from its constant offset.
  EXPECT(hints_of("(\\d{4})-(\\d{2})-(\\d{2})", real::flags::ascii).fixed_shape);
  EXPECT(hints_of("([0-9]{4})-([0-9]{2})").fixed_shape); // explicit classes: fixed width in text mode
  EXPECT(hints_of("(a)(b)").fixed_shape);
  EXPECT(hints_of("(\\d{4})x", real::flags::ascii).fixed_shape);
  // Strictly excluded (stay on the general VM): a text \d is a variable-width code-point predicate, a
  // variable count {n,m}/+/*/? is a split, a nested group, and an alternation.
  EXPECT(!hints_of("(\\d{2})").fixed_shape);                                     // klass_cp: text shorthand, variable width
  EXPECT(!hints_of("(\\d{1,3})", real::flags::ascii).fixed_shape);               // variable count
  EXPECT(!hints_of("(\\d{1,3}\\.){3}\\d{1,3}", real::flags::ascii).fixed_shape); // ipv4 guard-rail
  EXPECT(!hints_of("(\\w+)").fixed_shape);                                       // '+': the class-loop path, not this fixed shape
  EXPECT(!hints_of("((\\d{2}))", real::flags::ascii).fixed_shape);               // nested groups
  EXPECT(!hints_of("(\\d{2})|(x)", real::flags::ascii).fixed_shape);             // alternation
  // The group slots equal the VM's, across the date shape, adjacent saves, a fixed+trailing shape, a
  // near-miss (single-digit month), and no match.
  const real::regex date {R"((\d{4})-(\d{2})-(\d{2}))", real::flags::ascii};
  const std::string s1   {"x 2026-07-02 y"};
  const auto        m1   {date.search(s1)};
  EXPECT(m1.matched() && m1[1] == "2026"sv && m1[2] == "07"sv && m1[3] == "02"sv);
  EXPECT(m1.start(1) == 2U && m1.end(1) == 6U && m1.start(3) == 10U && m1.end(3) == 12U);
  const real::regex adj {"(\\d{2})(\\d{2})", real::flags::ascii};
  const std::string s2  {"1234"};
  const auto        m2  {adj.search(s2)};
  EXPECT(m2[1] == "12"sv && m2[2] == "34"sv);
  const std::string s3  {"2026-7-02"};      // single-digit month: {2} fails
  EXPECT(!date.search(s3));
  const std::string s4  {"end 2026-07-02"}; // match at end of text
  EXPECT(date.search(s4).end(0) == s4.size());
}

// OPT rare-discriminant (URL `https?://…`): memchr a rare mid-byte whose offset is *not* fixed when an
// optional mono-byte (`s?`) sits before it. Must arm past the optional, prefer the disc over a weak
// first-byte/`http` prefix, and never steal fixed-offset rare_byte or `@` inner-literal cases.
TEST(rare_disc_url_is_armed)
{
  const auto url {hints_of(R"(https?://[^\s]+)")};
  EXPECT_EQ(url.rare_disc, static_cast<std::int16_t>(':'));
  EXPECT_EQ(static_cast<int>(url.rare_disc_prefix_len), 4);
  EXPECT_EQ(std::string_view(url.rare_disc_prefix.data(), url.rare_disc_prefix_len), "http"sv);
  EXPECT_EQ(url.rare_disc_opt, static_cast<std::int16_t>('s'));
  EXPECT_EQ(static_cast<int>(url.rare_disc_after_len), 2);
  EXPECT_EQ(std::string_view(url.rare_disc_after.data(), url.rare_disc_after_len), "//"sv);
  // Pure `http://` (no optional): disc still arms; offset is fixed but memchr+back-verify is fine.
  const auto plain {hints_of(R"(http://[^\s]+)")};
  EXPECT_EQ(plain.rare_disc, static_cast<std::int16_t>(':'));
  EXPECT_EQ(plain.rare_disc_opt, static_cast<std::int16_t>(-1));
  EXPECT_EQ(static_cast<int>(plain.rare_disc_prefix_len), 4);
  EXPECT_EQ(std::string_view(plain.rare_disc_after.data(), plain.rare_disc_after_len), "//"sv);
}

TEST(rare_disc_does_not_steal_rare_byte_or_inner_literal)
{
  // Date: fixed-offset `-` rare_byte remains the route; no URL-shaped disc.
  const auto date {hints_of("[0-9]{4}-[0-9]{2}-[0-9]{2}", real::flags::ascii)};
  EXPECT_EQ(date.rare_byte, static_cast<std::int16_t>('-'));
  EXPECT_EQ(date.rare_disc, static_cast<std::int16_t>(-1));
  // Email: `@` inner-literal stays armed; no rare_disc takeover.
  const auto email {hints_of(R"((\w+)@(\w+))")};
  EXPECT(email.inner_literal_len > 0);
  EXPECT_EQ(email.rare_disc, static_cast<std::int16_t>(-1));
  // Strong first-byte literal with no rare mid-byte: disc stays off.
  const auto lit {hints_of("foobar\\d+")};
  EXPECT_EQ(lit.rare_disc, static_cast<std::int16_t>(-1));
  EXPECT(lit.prefix_size >= 2);
}

// OPT rare-byte-at-fixed-offset: a required rare literal (the `-`/`@`) drives a memchr scan, backing up to
// the candidate start. The prefilter only filters — the VM verifies — so it must never miss a match nor
// invent one, at any position. These are the teeth for that soundness.
TEST(rare_byte_prefilter_finds_every_match)
{
  const real::regex date {"[0-9]{4}-[0-9]{2}-[0-9]{2}", real::flags::ascii};
  // matches at the very start, mid-text, back-to-back, and flush against the end
  EXPECT_EQ(date.search("2026-07-04 tail").start(), 0U);
  EXPECT_EQ(date.search("head 2026-07-04").end(), 15U);
  {
    const std::string run {"1111-11-112222-22-22"}; // two dates, no separator
    std::size_t       n   {0};
    for (const auto& m : date.find_iter(run)) {
      (void)m;
      ++n;
    }
    EXPECT_EQ(n, 2U);
  }
  // the rare byte present but the surrounding shape wrong -> the VM rejects (no false positive)
  EXPECT(!date.search("ab-cd-ef 12-34-56 xx--yy"));
  EXPECT(!date.search("--------"));
  // a rare byte after a fixed run: the `@` at offset 3
  const real::regex tag {"[0-9]{3}@[a-z]+", real::flags::ascii};
  EXPECT_EQ(tag.search("xx 123@abc yy")[0], "123@abc"sv);
  EXPECT(!tag.search("12@abc"));           // only two digits before @
  EXPECT(!tag.search("no at-sign here"));  // the rare byte absent -> instant reject
}

TEST(find_literal_memmem_wrapper)
{
  using real::detail::find_literal;
  constexpr auto no = real::npos;
  EXPECT(find_literal("a@b", 0, "@") == 1);               // single byte -> memchr path
  EXPECT(find_literal("xx key= yy", 0, "key=") == 3);     // multi-byte
  EXPECT(find_literal("hello", 0, "xyz") == no);          // absent
  EXPECT(find_literal("a@b@c", 2, "@") == 3);             // from an offset
  EXPECT(find_literal("kekey=", 0, "key=") == 2);         // lead matches, tail fails, then a real hit
  EXPECT(find_literal("ab", 0, "abc") == no);             // literal longer than the text
  EXPECT(find_literal("abc", 1, "") == 1);                // empty literal -> pos
  EXPECT(find_literal("x\xC3\xA9y", 0, "\xC3\xA9") == 1); // non-ASCII bytes in the literal
}

// --- FIX P0 #2 (O(n^2)): (?i)<literal> on a sparse-match haystack ----------------------------------
//
// An icase literal compiles to a small (2..4-member) first-byte set (no literal prefix -- see
// prefix_literal_is_extracted's icase note above), routed through next_candidate's small-set branch,
// which falls back to find_bytes_cascade past a 32-byte probe miss. find_bytes_cascade used to hand
// `memchr` the FULL remaining haystack on every such call; a set member that is rare or absent from a
// stretch of text (e.g. `(?i)cafe`'s {c, C} on all-lowercase prose) turned every rejected-candidate
// cascade call into an O(remaining-text) memchr scan for a byte that is never found there -- O(n)
// candidates x O(n) scan = O(n^2), same family as A2's unbounded-reach fix (tests above) but one level
// upstream: the candidate SEARCH itself, not the anchored walk once a candidate is found.

TEST(icase_literal_cascade_hints_never_change_results)
{
  // Same with/without-hints technique as hints_never_change_results, but targeting the corpus shapes
  // named in the fix review: sparse-match (the adversarial shape -- mostly filler, no uppercase
  // fold member anywhere), dense-match, no-match, a match at the very start/end (boundary), a non-ASCII
  // fold (café), and an empty haystack.
  const std::string_view filler {"the quick brown fox jumps over the lazy dog and words filler here "};
  std::string            sparse;
  for (int i = 0; i < 40; ++i) {
    sparse += filler;
    sparse += "cafe ";
  }
  std::string dense;
  for (int i = 0; i < 200; ++i) {
    dense += "cafe ";
  }
  std::string no_match;
  for (int i = 0; i < 40; ++i) {
    no_match += filler;
  }
  const std::string boundary_start {"cafe " + no_match};
  const std::string boundary_end   {no_match + "cafe"};
  std::string       fold_accented  {sparse};
  fold_accented += "CAF\xC3\x89 "; // CAFÉ, upper-cased incl. the accent
  fold_accented += sparse;

  const std::string_view patterns[] = {"(?i)cafe", "(?i)CAFE", "(?i)caf\xC3\xA9", "(?i)ABC"};
  const std::string      texts[]    = {sparse, dense, no_match, boundary_start, boundary_end, fold_accented, ""};

  for (const auto& pattern : patterns) {
    const auto with       = dynamic_storage::compile(pattern, real::flags::none);
    auto       without    = with;
    without.program.hints = {};
    for (const auto& text : texts) {
      real::detail::pike_state         s1;
      real::detail::pike_state         s2;
      std::vector<std::size_t>         r1;
      std::vector<std::size_t>         r2;
      const real::detail::program_view pv1 {with.view()};
      const real::detail::program_view pv2 {without.view()};
      real::detail::pike_vm            vm1(pv1, s1);
      real::detail::pike_vm            vm2(pv2, s2);
      const bool                       m1 = vm1.run(text, 0, real::detail::run_mode::search, r1);
      const bool                       m2 = vm2.run(text, 0, real::detail::run_mode::search, r2);
      EXPECT_EQ(m1, m2);
      EXPECT(r1 == r2);
    }
  }
}

TEST(icase_literal_cascade_finds_every_match_find_iter)
{
  // find_iter (not just the first search) over the exact bug shape: sparse, all-lowercase filler with
  // no uppercase fold member anywhere, checked against a hand-counted expected match count.
  const std::string_view filler {"the quick brown fox jumps over the lazy dog and words filler here "};
  std::string            text;
  constexpr int          reps   {50};
  for (int i = 0; i < reps; ++i) {
    text += filler;
    text += "cafe ";
  }
  const real::regex  rx {"(?i)cafe"};
  std::size_t        n  {0};
  for (const auto& m : rx.find_iter(text)) {
    EXPECT_EQ(m[0], "cafe"sv);
    ++n;
  }
  EXPECT_EQ(n, static_cast<std::size_t>(reps));
}

TEST(icase_literal_cascade_throughput_smoke)
{
  // THE gate for the fix itself: deterministic work counter (REAL_TEST_INSTRUMENT), not wall-clock --
  // literal_prefilter_throughput_smoke's exact method, applied to find_bytes_cascade. Pre-fix this
  // regresses hard (O(n^2): the ratio below would land near 16x, not under 8x) -- verified by hand
  // against the pre-fix tree during the fix's own diagnosis.
  const real::regex rx {"(?i)cafe"};
  const auto        work {[&](std::size_t n) -> std::uint64_t {
                            std::string text;
                            text.reserve(n + 128);
                            const std::string_view filler {
                              "the quick brown fox jumps over the lazy dog and words filler here more filler text words "};
                            while (text.size() < n) {
                              text += filler;
                              text += "cafe ";
                            }
                            real::detail::prefilter_work_units() = 0;
                            std::size_t matches {0};
                            for (const auto& m : rx.find_iter(text)) {
                              (void) m;
                              ++matches;
                            }
                            EXPECT(matches > 0);
                            return real::detail::prefilter_work_units();
                          }};
  (void) work(1 << 12);                      // warmup (first-call path setup); discarded
  const std::uint64_t small {work(1 << 18)}; // 256 KiB
  const std::uint64_t large {work(1 << 20)}; // 1 MiB -- 4x the bytes
  // O(n) -> ~4x; O(n^2) -> ~16x. 8x bites quadratic, absorbs constant per-search overhead.
  EXPECT(large < small * 8);
  // Determinism pin: re-run large -- same work count (not wall time).
  EXPECT_EQ(work(1 << 20), large);
}

// --- FIX (mono/multi split): a 1-member find_bytes_cascade call cannot exhibit the P0 #2 O(n^2)
// by construction -- one memchr's own cost already equals its progress, so windowing it is pure
// overhead (measured as a real x86 regression on stop-set-shaped patterns, e.g. [^\x01]+, once
// the general galloping fix landed). White-box on find_bytes_cascade directly (not through a full
// regex search) to isolate its own billing from any other prefilter mechanism's contribution.

TEST(mono_member_cascade_miss_is_single_unwindowed_pass)
{
  using real::detail::find_bytes_cascade;
  const std::string text (100000, 'a'); // the member never occurs: a genuine full-range miss
  const char        member {'\x01'};
  real::detail::prefilter_work_units() = 0;
  const std::size_t hit = find_bytes_cascade(text, 0, &member, 1);
  EXPECT_EQ(hit, real::npos);
  // Exactly the range scanned once -- not the ~1.9x a windowed re-scan bills on a full miss
  // (contrast: multi_member_cascade_miss_still_windows below, same shape, n=2).
  EXPECT_EQ(real::detail::prefilter_work_units(), static_cast<std::uint64_t>(text.size()));
}

TEST(mono_member_cascade_hit_bills_distance_only)
{
  using real::detail::find_bytes_cascade;
  std::string text (100000, 'a');
  text[12345] = '\x01';
  const char member {'\x01'};
  real::detail::prefilter_work_units() = 0;
  const std::size_t hit = find_bytes_cascade(text, 0, &member, 1);
  EXPECT_EQ(hit, 12345U);
  EXPECT_EQ(real::detail::prefilter_work_units(), 12345ULL); // distance to the hit, not the whole range
}

TEST(multi_member_cascade_miss_still_windows)
{
  // Contrast/regression pin: the SAME full-miss shape with n=2 must still pay the P0 #2 fix's own
  // geometric-doubling series -- the split must not have silently disabled galloping for the case
  // it exists to protect. seed=128, doubling, capped to the 1000-byte range: 128+256+512+1000 = 1896.
  using real::detail::find_bytes_cascade;
  const std::string text (1000, 'a');
  const char        members[2] {'\x01', '\x02'}; // neither occurs
  real::detail::prefilter_work_units() = 0;
  const std::size_t hit = find_bytes_cascade(text, 0, members, 2);
  EXPECT_EQ(hit, real::npos);
  EXPECT_EQ(real::detail::prefilter_work_units(), 1896ULL);
}

TEST(stop_set_class_loop_throughput_smoke_mono_member)
{
  // End-to-end (not white-box): [^\x01]+ over an all-'a' corpus routes through codepoint_class_plus
  // -> run_cascade_stop -> find_bytes_cascade with stop_set_size == 1 (confirmed via hints_of-style
  // inspection during the fix's own diagnosis). Deterministic work counter, not wall-clock -- proves
  // the mono-member bypass holds through the real dispatch path, not just the white-box call above.
  const real::regex rx {"[^\x01]+"};
  const auto        work {[&](std::size_t n) -> std::uint64_t {
                            const std::string text (n, 'a');
                            real::detail::prefilter_work_units() = 0;
                            const auto m = rx.search(text);
                            EXPECT(m.matched());
                            EXPECT_EQ(m.end(), n);
                            return real::detail::prefilter_work_units();
                          }};
  (void) work(1 << 12);                      // warmup
  const std::uint64_t small {work(1 << 18)}; // 256 KiB
  const std::uint64_t large {work(1 << 20)}; // 1 MiB -- 4x the bytes
  // O(n) -> ~4x; a lingering windowed re-scan would still be O(n) here too (single call either
  // way) but at a larger constant -- this pins the ratio stays tight to 4x, not just under a loose
  // quadratic-smoke margin.
  EXPECT(large < small * 5);
  EXPECT_EQ(work(1 << 20), large); // determinism pin
}

// The multi-byte substring search (find_prefix / find_literal) runs a two-byte SIMD block filter at run
// time and a scalar loop under constant evaluation. Both must equal `std::string_view::find` — an
// independent oracle, not either implementation — across every shape where a block-scan goes wrong:
// needle lengths straddling the 16-byte lane width, haystacks shorter than / equal to / longer than a
// block, a match at offset 0, at the last legal start, straddling a block boundary, `pos` walked over
// the whole subject, and needles whose lead byte equals its trail (the filter degenerates to one probe).
TEST(literal_search_equals_the_platform_find_everywhere)
{
  const std::array<std::string_view, 10> needles {
    "ab"sv, "aa"sv, "dog"sv, "aba"sv, "abcdefgh"sv, "aaaaaaaa"sv,
    "abcdefghijklmnop"sv,   // exactly one lane width
    "abcdefghijklmnopq"sv,  // one past it
    "xy"sv,                 // absent from every haystack below
    "abcdefghijklmnopqrst"sv
  };
  // Haystacks that put matches at 0, at the tail, and across the 16/32/48-byte block seams.
  std::vector<std::string> haystacks {
    "", "a", "ab", "aab", "aaaaaaaaaaaaaaaa", "aaaaaaaaaaaaaaaaa",
    "abcdefghijklmnopqrstuvwxyz", "dog", "the dog", "dogdogdog",
  };
  {
    // A match at every offset around each block seam, plus a long all-'a' run (max false-positive rate
    // for a lead-byte filter, so the verify path is hammered).
    // pad walks past 64 so every leg runs: the 4-block unrolled round, the single-block remainder, and
    // the scalar tail -- and the match lands at every offset relative to each of those seams.
    for (std::size_t pad = 0; pad <= 70; ++pad) {
      haystacks.push_back(std::string(pad, 'a') + "dog" + std::string(20, 'z'));
      haystacks.push_back(std::string(pad, 'z') + "ab");            // match flush at the very end
      haystacks.push_back("ab" + std::string(pad, 'z'));            // match at offset 0
    }
    for (std::size_t n : {63U, 64U, 65U, 79U, 80U, 81U, 127U, 128U, 129U, 200U}) {
      haystacks.emplace_back(n, 'a');                               // all-lead-byte: max verify pressure
      haystacks.push_back(std::string(n, 'a') + "b");               // hit only in the very last window
      haystacks.push_back(std::string(n / 2, 'a') + "b" + std::string(n / 2, 'a'));
    }
  }

  std::size_t checked {0};
  for (const std::string& hay : haystacks) {
    const std::string_view text {hay};
    for (const std::string_view needle : needles) {
      for (std::size_t pos = 0; pos <= text.size() + 1; ++pos) {
        // Oracle: the platform search, restricted to [pos, end) exactly like the primitives promise.
        std::size_t want {real::npos};
        if (pos <= text.size()) {
          const auto off {text.substr(pos).find(needle)};
          if (off != std::string_view::npos) {
            want = pos + off;
          }
        }
        const std::size_t got_literal {real::detail::find_literal(text, pos, needle)};
        const std::size_t got_prefix  {real::detail::find_prefix(text, pos, needle)};
        EXPECT_EQ(got_literal, want);
        EXPECT_EQ(got_prefix, want);
        ++checked;
      }
    }
  }
  EXPECT(checked > 20000U); // the cross product actually ran (a silently empty loop would "pass")
}

// The same primitives under constant evaluation take the scalar path (no intrinsics in a constexpr
// context), so the two legs are pinned against each other on shapes that cross the lane width.
TEST(literal_search_constexpr_leg_agrees)
{
  static_assert(real::detail::find_literal("the dog end"sv, 0, "dog"sv) == 4U);
  static_assert(real::detail::find_literal("aaaaaaaaaaaaaaaaaaab"sv, 0, "ab"sv) == 18U);
  static_assert(real::detail::find_literal("aaaaaaaaaaaaaaaaaaaa"sv, 0, "ab"sv) == real::npos);
  static_assert(real::detail::find_prefix("aaaaaaaaaaaaaaaaaaab"sv, 3, "ab"sv) == 18U);
  static_assert(real::detail::find_prefix("abcdefghijklmnopqrst"sv, 0, "pq"sv) == 15U);

  // Runtime must agree with each of those.
  EXPECT_EQ(real::detail::find_literal("the dog end"sv, 0, "dog"sv), 4U);
  EXPECT_EQ(real::detail::find_literal("aaaaaaaaaaaaaaaaaaab"sv, 0, "ab"sv), 18U);
  EXPECT_EQ(real::detail::find_literal("aaaaaaaaaaaaaaaaaaaa"sv, 0, "ab"sv), real::npos);
  EXPECT_EQ(real::detail::find_prefix("aaaaaaaaaaaaaaaaaaab"sv, 3, "ab"sv), 18U);
  EXPECT_EQ(real::detail::find_prefix("abcdefghijklmnopqrst"sv, 0, "pq"sv), 15U);
}
