// Arc B: `\b\w+\b` ≡ `\w+` (B-1) and `\b`-wrap on class loops (B-2).
#include <string>
#include <string_view>
#include <vector>

#include <sciforge/test/framework.hpp>
#include "real/real.hpp"

namespace {

  bool hit(const real::regex& re,
           std::string_view   t)
  {
    return static_cast<bool>(re.search(t));
  }

  std::size_t cnt(const real::regex& re,
                  std::string_view   t)
  {
    return re.count_matches(t);
  }
} // namespace

TEST(arc_b1_bw_equals_w_hints_and_counts)
{
  const real::regex bw {R"(\b\w+\b)"};
  const real::regex w  {R"(\w+)"};
  EXPECT_EQ(static_cast<int>(bw.raw_program().hints.greedy_cp_class), 0);
  EXPECT_EQ(static_cast<int>(bw.raw_program().hints.wb_lead), 0); // B-1 dropped
  EXPECT_EQ(static_cast<int>(bw.raw_program().hints.wb_trail), 0);

  const std::string text {
    "hello world a_b 123 x\n"
    "  foo bar_baz  \n"};
  EXPECT_EQ(cnt(bw, text), cnt(w, text));
  EXPECT(hit(bw, "hello"));
  EXPECT(hit(bw, "_under"));
  EXPECT(hit(bw, "9mix"));
}

TEST(arc_b1_lead_or_trail_only_also_simplifies)
{
  EXPECT_EQ(static_cast<int>(real::regex(R"(\b\w+)").raw_program().hints.greedy_cp_class), 0);
  EXPECT_EQ(static_cast<int>(real::regex(R"(\w+\b)").raw_program().hints.wb_lead), 0);
  EXPECT_EQ(static_cast<int>(real::regex(R"(\w+\b)").raw_program().hints.greedy_cp_class), 0);
}

TEST(arc_b1_not_word_boundary_maximal_not_simplified)
{
  // `\B` on a maximal `\w+` run is unsound for the skip-whole-run scanner — stay general.
  // (D1a arms single-atom `\B\w` instead; see test_wb_peel_resolve.)
  EXPECT_EQ(static_cast<int>(real::regex(R"(\B\w+\B)").raw_program().hints.greedy_cp_class), -1);
  EXPECT_EQ(static_cast<int>(real::regex(R"(\B\w+)").raw_program().hints.greedy_cp_class), -1);
}

TEST(d1a_B_single_atom_arms_cp_wrap)
{
  const real::regex re {R"(\B\w)"};
  EXPECT(re.raw_program().hints.greedy_cp_class >= 0);
  EXPECT_EQ(static_cast<int>(re.raw_program().hints.wb_lead), 2);
  EXPECT_EQ(cnt(re, "hello"), 4U);
  EXPECT_EQ(cnt(re, " a "), 0U); // sole letter after space is boundary-led
}

TEST(arc_b2_az_wrap_boundaries)
{
  const real::regex re {R"(\b[a-z]+\b)"};
  const auto&       h  {re.raw_program().hints};
  EXPECT_EQ(static_cast<int>(h.greedy_class_loop), 0);
  EXPECT_EQ(static_cast<int>(h.wb_lead), 1);
  EXPECT_EQ(static_cast<int>(h.wb_trail), 1);

  EXPECT(hit(re, " abc "));
  EXPECT(hit(re, "abc"));
  EXPECT(!hit(re, "9abc")); // 'a' follows word-char '9'
  EXPECT(!hit(re, "a_b"));  // no [a-z]+ run is a full word ('_' is word)
  EXPECT(!hit(re, "ab9"));
  EXPECT_EQ(cnt(re, "one two three"), 3U);
}

TEST(arc_b2_digit_wrap)
{
  const real::regex re {R"(\b\d+\b)"};
  EXPECT(hit(re, " 12 "));
  EXPECT(!hit(re, "12a"));
  EXPECT(!hit(re, "a12"));
  EXPECT_EQ(cnt(re, "1 22 333"), 3U);
}

TEST(arc_b_ascii_flag_word_simplifies_to_class_loop)
{
  const real::regex re {R"(\b\w+\b)", real::flags::ascii};
  EXPECT(re.raw_program().hints.greedy_class_loop >= 0);
  EXPECT_EQ(static_cast<int>(re.raw_program().hints.wb_lead), 0);
}

// Adversarial: \w ∪ {non-word CP} must not get B-1 bare simplify, nor B-2 maximal wrap
// (wrap would miss the word-bounded sub-run after a non-word class member).
TEST(arc_b1_guard_rejects_word_plus_emoji_superset)
{
  // U+1F600 GRINNING FACE — not \w. Pattern bytes: [\w😀]
  std::string pat = "\\b[\\w";
  pat += "\xF0\x9F\x98\x80";
  pat += "]+\\b";
  const real::regex re {pat};
  const auto&       h  {re.raw_program().hints};
  // Unarmed general path: no greedy_cp (superset under \b).
  EXPECT_EQ(static_cast<int>(h.greedy_cp_class), -1);

  std::string text = "\xF0\x9F\x98\x80"; // 😀
  text += "abc";
  // Correct: \b before 'a' (emoji is non-word), match "abc".
  const auto m = re.search(text);
  EXPECT(m.matched());
  EXPECT_EQ(m.start(), 4U);
  EXPECT_EQ(m.end(), text.size());
  EXPECT_EQ(m[0], "abc");
}

TEST(arc_b1_guard_rejects_word_plus_middot_superset)
{
  // U+00B7 MIDDLE DOT — not \w. Catalan-style quasi-\w class must not B-1 drop \b.
  std::string pat = "\\b[\\w";
  pat += "\xC2\xB7"; // ·
  pat += "]+\\b";
  const real::regex re {pat};
  const auto&       h  {re.raw_program().hints};
  EXPECT_EQ(static_cast<int>(h.greedy_cp_class), -1);

  // "·abc": middot is non-word → \b before 'a' → match "abc" only (not "·abc").
  std::string text = "\xC2\xB7";
  text += "abc";
  const auto m = re.search(text);
  EXPECT(m.matched());
  EXPECT_EQ(m.start(), 2U);
  EXPECT_EQ(m.end(), text.size());
  EXPECT_EQ(m[0], "abc");
}

TEST(arc_b1_exact_w_still_simplifies)
{
  const real::regex re {R"(\b\w+\b)"};
  EXPECT(re.raw_program().hints.greedy_cp_class >= 0);
  EXPECT_EQ(static_cast<int>(re.raw_program().hints.wb_lead), 0);
  EXPECT_EQ(static_cast<int>(re.raw_program().hints.wb_trail), 0);
  const std::string t {"hello world a_b 123"};
  EXPECT_EQ(cnt(re, t), cnt(real::regex(R"(\w+)"), t));
}

// --- wb-class-junction fix: `\b`/`\B` + a SINGLE code point (no `+`) at a ---
// --- multi-byte<->ASCII word junction (real::regex v2026.7.33 and earlier) --
//
// B-1 ("`\b` next to a full-`\w` MAXIMAL run is redundant") only holds when the match is
// guaranteed to start at a maximal run's own boundary -- true for `\b\w+` (tested above), false
// for bare `\b\w`, which may legally start on ANY word code point, including one immediately
// preceded by another word code point (mid-run). resolve_class_wb_hints used to drop the
// boundary check unconditionally whenever the class was exactly `\w`, regardless of whether a
// `+` loop was actually present -- so `\b\w` silently inherited B-1's redundancy guarantee
// without earning it, and claimed a boundary between two adjacent word code points wherever one
// side was multi-byte (the byte-level fast-path scan never even reached the correct,
// codepoint-aware word_before/word_after in assert_eval.hpp for that candidate). Fixed by gating
// the drop on an explicit `maximal_run` flag (see resolve_class_wb_hints/wb_redundant_for_full_word).
namespace {

  struct junction_case
  {
    std::string_view name;
    std::string      cp;       // the multi-byte code point, UTF-8 bytes
    bool             is_digit; // also word-eligible for \d (Unicode digits are word chars too)
  };

  std::vector<junction_case> junction_catalog()
  {
    return {
      {.name = "e_acute_2byte", .cp = "\xC3\xA9", .is_digit = false},           // é U+00E9
      {.name = "arabic_zero_2byte", .cp = "\xD9\xA0", .is_digit = true},        // ٠ U+0660
      {.name = "han_3byte", .cp = "\xE4\xB8\x87", .is_digit = false},           // 万 U+4E07
      {.name = "fullwidth_zero_3byte", .cp = "\xEF\xBC\x90", .is_digit = true}, // ０ U+FF10
      {.name = "dstruck_x_4byte", .cp = "\xF0\x9D\x95\x8F", .is_digit = false}, // 𝕏 U+1D54F
    };
  }
} // namespace

TEST(wb_class_junction_multibyte_then_ascii_word)
{
  const real::regex bw {R"(\b\w)"};
  const real::regex Bw {R"(\B\w)"};
  for (const auto& jc : junction_catalog()) {
    const std::string text   {jc.cp + "a"}; // multi-byte word code point, then ASCII word 'a'
    const std::size_t cp_len {jc.cp.size()};
    // \b\w: exactly one boundary-led word code point -- the very start of text. NOT at the
    // junction (both sides are word code points, no boundary there).
    const auto bw_matches {bw.find_all(text)};
    EXPECT_EQ(bw_matches.size(), 1U);
    if (bw_matches.size() == 1U) {
      EXPECT_EQ(bw_matches[0].start(), 0U);
      EXPECT_EQ(bw_matches[0].end(), cp_len);
    }
    // \B\w: the complement -- exactly one non-boundary-led word code point, at the junction
    // (start of 'a', preceded by a word code point).
    const auto Bw_matches {Bw.find_all(text)};
    EXPECT_EQ(Bw_matches.size(), 1U);
    if (Bw_matches.size() == 1U) {
      EXPECT_EQ(Bw_matches[0].start(), cp_len);
      EXPECT_EQ(Bw_matches[0].end(), cp_len + 1);
    }
  }
}

TEST(wb_class_junction_ascii_word_then_multibyte)
{
  const real::regex bw {R"(\b\w)"};
  const real::regex Bw {R"(\B\w)"};
  for (const auto& jc : junction_catalog()) {
    const std::string text       {"a" + jc.cp}; // ASCII word 'a', then multi-byte word code point
    const auto        bw_matches {bw.find_all(text)};
    EXPECT_EQ(bw_matches.size(), 1U);
    if (bw_matches.size() == 1U) {
      EXPECT_EQ(bw_matches[0].start(), 0U);
      EXPECT_EQ(bw_matches[0].end(), 1U);
    }
    const auto Bw_matches {Bw.find_all(text)};
    EXPECT_EQ(Bw_matches.size(), 1U);
    if (Bw_matches.size() == 1U) {
      EXPECT_EQ(Bw_matches[0].start(), 1U);
      EXPECT_EQ(Bw_matches[0].end(), text.size());
    }
  }
}

TEST(wb_class_junction_digit_variants)
{
  // Same junction shape, \d instead of \w, restricted to the digit-eligible catalog entries
  // (Unicode digits are also \w, but \d is the narrower, independently-recognized class --
  // is_unicode_word_subset_cp_class, not is_full_unicode_word_cp_class, so this exercises B-2's
  // "keep the wrap" branch rather than B-1's "drop" branch; both must be junction-safe).
  const real::regex bd {R"(\b\d)"};
  const real::regex Bd {R"(\B\d)"};
  for (const auto& jc : junction_catalog()) {
    if (!jc.is_digit) {
      continue;
    }
    const std::string  text_lead {jc.cp + "5"}; // multi-byte digit, then ASCII digit
    const std::size_t  cp_len    {jc.cp.size()};
    const auto         bd_lead   {bd.find_all(text_lead)};
    EXPECT_EQ(bd_lead.size(), 1U);
    if (bd_lead.size() == 1U) {
      EXPECT_EQ(bd_lead[0].start(), 0U);
      EXPECT_EQ(bd_lead[0].end(), cp_len);
    }
    const auto Bd_lead {Bd.find_all(text_lead)};
    EXPECT_EQ(Bd_lead.size(), 1U);
    if (Bd_lead.size() == 1U) {
      EXPECT_EQ(Bd_lead[0].start(), cp_len);
      EXPECT_EQ(Bd_lead[0].end(), cp_len + 1);
    }

    const std::string  text_trail {"5" + jc.cp}; // ASCII digit, then multi-byte digit
    const auto         bd_trail   {bd.find_all(text_trail)};
    EXPECT_EQ(bd_trail.size(), 1U);
    if (bd_trail.size() == 1U) {
      EXPECT_EQ(bd_trail[0].start(), 0U);
      EXPECT_EQ(bd_trail[0].end(), 1U);
    }
    const auto Bd_trail {Bd.find_all(text_trail)};
    EXPECT_EQ(Bd_trail.size(), 1U);
    if (Bd_trail.size() == 1U) {
      EXPECT_EQ(Bd_trail[0].start(), 1U);
      EXPECT_EQ(Bd_trail[0].end(), text_trail.size());
    }
  }
}

TEST(wb_class_junction_maximal_run_unaffected)
{
  // \b\w+ / \w+\b (the ACTUAL maximal-run shape B-1 targets) must still see straight through a
  // multi-byte/ASCII junction -- one run, no split at the boundary the single-code-point fix
  // above now correctly refuses to cross either.
  const real::regex bwp {R"(\b\w+)"};
  for (const auto& jc : junction_catalog()) {
    const std::string text {jc.cp + "a"};
    const auto        m    {bwp.find_all(text)};
    EXPECT_EQ(m.size(), 1U);
    if (m.size() == 1U) {
      EXPECT_EQ(m[0].start(), 0U);
      EXPECT_EQ(m[0].end(), text.size()); // whole run, both sides of the junction
    }
  }
}
