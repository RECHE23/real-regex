// Arc B: `\b\w+\b` ≡ `\w+` (B-1) and `\b`-wrap on class loops (B-2).
#include <string>
#include <string_view>

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

TEST(arc_b1_not_word_boundary_not_simplified)
{
  // `\B` is never redundant with a maximal `\w` run — stay off the cp fast path.
  EXPECT_EQ(static_cast<int>(real::regex(R"(\B\w+\B)").raw_program().hints.greedy_cp_class), -1);
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
