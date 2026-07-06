// Prefilter: activation tests (is the right acceleration *selected*? —
// historically, silent mis-selection was the worst kind of bug: everything
// correct, just slow), equivalence tests (hints never change results), and
// a throughput smoke test.
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
  // records at emission — analyze_program no longer re-recognizes the bytecode shape.
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
  // The literal prefilter must scan a miss in O(n) — memchr speed — not the O(n²) a per-position rescan
  // would cost. An absolute wall-clock bound is machine- and instrumentation-dependent (the coverage and
  // sanitize builds run 5-20x slower than a bare native one), so this is a *scaling* test instead: an
  // 8x-longer miss must take on the order of 8x longer, never ~64x. Both measurements run the identical
  // instrumented code, so however slow the build is cancels in the ratio; only an O(n²) regression blows
  // it past the margin. Best-of-5 per size damps shared-runner noise (a g++-14 CI runner once perturbed the
  // 1 MB min enough to blow the ratio; the min-of-5 is harder to knock off a clean run — the 25x anti-O(n^2)
  // margin stays put, since real scaling is ~8x with tight spread, not something to loosen for noise).
  const real::regex rx {"needle\\d?$"}; // $ keeps it off the lazy-DFA route -> the prefilter path (test intent)
  const auto        best_of {[&](std::size_t n) {
                               std::string text(n, 'a');
                               text += "needle";
                               const auto once {[&] {
                                                  const auto begin {std::chrono::steady_clock::now()};
                                                  bool       ok {true};
                                                  for (int i = 0; i < 10; ++i) {
                                                    ok = ok && rx.search(text).matched();
                                                  }
                                                  EXPECT(ok);
                                                  return std::chrono::steady_clock::now() - begin;
                                                }};
                               auto best {once()};
                               for (int k = 0; k < 4; ++k) { // best of 5
                                 best = std::min(best, once());
                               }
                               return best;
                             }};

  const auto small        {best_of(1 << 20)}; // 1 MB miss
  const auto large        {best_of(8 << 20)}; // 8 MB miss — 8x the bytes

  const std::string check {std::string(8 << 20, 'a') + "needle"};
  EXPECT_EQ(rx.search(check).start(), static_cast<std::size_t>(8 << 20));
  // O(n) makes large ~8x small; an O(n²) regression makes it ~64x. A 25x margin bites the quadratic while
  // absorbing the constant per-search overhead and any residual noise.
  EXPECT(large.count() < small.count() * 25);
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
