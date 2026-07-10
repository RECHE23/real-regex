// R2 coverage: peel_optional_lead/trail_wb + resolve_class_wb_hints branches.
// Lead-only / trail-only / \B unarm / peel-reject (non-wb assert) / group-end-save interleave.
// Oracle: hints + match spans (not fill-only).
#include <string>
#include <string_view>
#include <vector>

#include <sciforge/test/framework.hpp>

#include "real/core/program.hpp"
#include "real/engine/prefilter.hpp"
#include "real/real.hpp"

namespace {

  bool hit(const real::regex& re,
           std::string_view   t)
  {
    return static_cast<bool>(re.search(t));
  }
} // namespace

// --- peel: lead-only / trail-only on class-loop (B-2 subset) ----------------

TEST(wb_peel_lead_only_class_loop)
{
  // `\b[a-z]+` — lead peeled, no trail; B-2 keeps wrap (not full \w → no B-1 drop).
  const real::regex re   {R"(\b[a-z]+)"};
  const auto        prog {re.raw_program()};
  EXPECT(prog.hints.greedy_class_loop >= 0);
  EXPECT_EQ(static_cast<int>(prog.hints.wb_lead), 1);
  EXPECT_EQ(static_cast<int>(prog.hints.wb_trail), 0);

  EXPECT(hit(re, " abc"));
  EXPECT(hit(re, "abc"));
  EXPECT(!hit(re, "9abc")); // no lead boundary before 'a'
  EXPECT(hit(re, "abc9"));  // trail open — matches "abc"
}

TEST(wb_peel_trail_only_class_loop)
{
  const real::regex re   {R"([a-z]+\b)"};
  const auto        prog {re.raw_program()};
  EXPECT(prog.hints.greedy_class_loop >= 0);
  EXPECT_EQ(static_cast<int>(prog.hints.wb_lead), 0);
  EXPECT_EQ(static_cast<int>(prog.hints.wb_trail), 1);

  EXPECT(hit(re, "abc "));
  EXPECT(hit(re, "abc"));
  EXPECT(hit(re, "abc!"));
  EXPECT_EQ(re.count_matches("one two"), 2U);
}

TEST(wb_peel_lead_only_cp_digit)
{
  // Unicode `\b\d+` — subset → B-2 wrap lead only.
  const real::regex re   {R"(\b\d+)"};
  const auto        prog {re.raw_program()};
  EXPECT(prog.hints.greedy_cp_class >= 0);
  EXPECT_EQ(static_cast<int>(prog.hints.wb_lead), 1);
  EXPECT_EQ(static_cast<int>(prog.hints.wb_trail), 0);

  EXPECT(hit(re, " 42"));
  EXPECT(hit(re, "42"));
  EXPECT(!hit(re, "a42")); // '4' after word char
}

TEST(wb_peel_trail_only_cp_digit)
{
  const real::regex re   {R"(\d+\b)"};
  const auto        prog {re.raw_program()};
  EXPECT(prog.hints.greedy_cp_class >= 0);
  EXPECT_EQ(static_cast<int>(prog.hints.wb_lead), 0);
  EXPECT_EQ(static_cast<int>(prog.hints.wb_trail), 1);

  EXPECT(hit(re, "42 "));
  EXPECT(hit(re, "42"));
  EXPECT(!hit(re, "42a"));
}

// --- resolve: \B never arms class/cp maximal-run fast path ------------------

TEST(wb_resolve_B_unarms_class_loop)
{
  // `\B[a-z]+\B` — resolve returns false (\B); greedy_class_loop stays off.
  const real::regex re {R"(\B[a-z]+\B)"};
  EXPECT_EQ(static_cast<int>(re.raw_program().hints.greedy_class_loop), -1);
  // General path: non-boundaries both sides of "abc" inside "xabcy".
  EXPECT(hit(re, "xabcy"));
  const auto m = re.search("xabcy");
  EXPECT(m.matched());
  EXPECT_EQ(m[0], "abc");
}

TEST(wb_resolve_B_unarms_cp_loop)
{
  const real::regex re {R"(\B\d+\B)"};
  EXPECT_EQ(static_cast<int>(re.raw_program().hints.greedy_cp_class), -1);
  EXPECT(hit(re, "a12b"));
  EXPECT(!hit(re, " 12 "));
}

TEST(wb_resolve_mixed_b_and_B_unarms)
{
  // Lead \b + trail \B → still \B present → unarm.
  const real::regex re {R"(\b[a-z]+\B)"};
  EXPECT_EQ(static_cast<int>(re.raw_program().hints.greedy_class_loop), -1);
}

// --- peel-reject: non-wb assert at peel position → no fake wb wrap ----------

TEST(wb_peel_reject_caret_before_class_loop)
{
  // `^[a-z]+` — lead assert is ^ not \b; must not claim wb wrap.
  const real::regex re   {R"(^[a-z]+)"};
  const auto        prog {re.raw_program()};
  EXPECT_EQ(static_cast<int>(prog.hints.wb_lead), 0);
  EXPECT_EQ(static_cast<int>(prog.hints.wb_trail), 0);
  EXPECT(hit(re, "abc"));
  EXPECT(!hit(re, " abc"));
}

TEST(wb_peel_reject_dollar_after_class_loop)
{
  const real::regex re   {R"([a-z]+$)"};
  const auto        prog {re.raw_program()};
  EXPECT_EQ(static_cast<int>(prog.hints.wb_lead), 0);
  EXPECT_EQ(static_cast<int>(prog.hints.wb_trail), 0);
  EXPECT(hit(re, "abc"));
  EXPECT(!hit(re, "abc "));
}

// --- group-end-save interleave with trail peel (class + capture) ------------

TEST(wb_peel_group_end_save_then_trail_wb)
{
  // `([a-z]+)\b` — group-end save then trail \b; peel trail after group-end.
  const real::regex re   {R"(([a-z]+)\b)"};
  const auto        prog {re.raw_program()};
  EXPECT(prog.hints.greedy_class_loop >= 0);
  EXPECT(prog.hints.greedy_group_start >= 0);
  EXPECT(prog.hints.greedy_group_end >= 0);
  EXPECT_EQ(static_cast<int>(prog.hints.wb_trail), 1);

  const auto m = re.search(" foo ");
  EXPECT(m.matched());
  EXPECT_EQ(m[0], "foo");
  EXPECT_EQ(m[1], "foo");
  // Maximal [a-z]+ always ends at a boundary (non-letter or EOS), so trail \b holds on bare words.
  EXPECT(hit(re, "food"));
  EXPECT_EQ(re.count_matches("one two three"), 3U);
}

TEST(wb_peel_lead_wb_group_class_trail_wb)
{
  // `\b([a-z]+)\b` — lead peel, group, trail peel; B-2 keeps both wraps.
  const real::regex re   {R"(\b([a-z]+)\b)"};
  const auto        prog {re.raw_program()};
  EXPECT(prog.hints.greedy_class_loop >= 0);
  EXPECT_EQ(static_cast<int>(prog.hints.wb_lead), 1);
  EXPECT_EQ(static_cast<int>(prog.hints.wb_trail), 1);
  EXPECT(prog.hints.greedy_group_start >= 0);

  EXPECT(hit(re, " foo "));
  EXPECT(!hit(re, "9foo"));
  EXPECT(!hit(re, "foo9"));
  const auto m = re.search(" bar ");
  EXPECT(m.matched());
  EXPECT_EQ(m[1], "bar");
}

TEST(wb_peel_group_cp_digit_both_wb)
{
  // `\b(\d+)\b` — Unicode digit class + group + both peels.
  const real::regex re   {R"(\b(\d+)\b)"};
  const auto        prog {re.raw_program()};
  EXPECT(prog.hints.greedy_cp_class >= 0);
  EXPECT_EQ(static_cast<int>(prog.hints.wb_lead), 1);
  EXPECT_EQ(static_cast<int>(prog.hints.wb_trail), 1);

  const auto m = re.search(" 99 ");
  EXPECT(m.matched());
  EXPECT_EQ(m[0], "99");
  EXPECT_EQ(m[1], "99");
  EXPECT(!hit(re, "a99"));
  EXPECT(!hit(re, "99a"));
}

// --- fixed-shape / alternation peels (same helpers) -------------------------

TEST(wb_peel_fixed_shape_lead_only)
{
  const real::regex re   {R"(\b[0-9a-f]{4})"};
  const auto        prog {re.raw_program()};
  EXPECT(prog.hints.fixed_shape);
  EXPECT_EQ(static_cast<int>(prog.hints.wb_lead), 1);
  EXPECT_EQ(static_cast<int>(prog.hints.wb_trail), 0);
  EXPECT(hit(re, " dead"));
  EXPECT(!hit(re, "xdead"));
}

TEST(wb_peel_alternation_lead_and_trail)
{
  const real::regex re   {R"(\b(?:foo|bar)\b)"};
  const auto        prog {re.raw_program()};
  EXPECT(prog.hints.fixed_alternation);
  EXPECT_EQ(static_cast<int>(prog.hints.wb_lead), 1);
  EXPECT_EQ(static_cast<int>(prog.hints.wb_trail), 1);
  EXPECT(hit(re, " foo "));
  EXPECT(!hit(re, " food "));
  EXPECT(!hit(re, "xfoo"));
}

// --- resolve B-1 still drops full-word lead-only / trail-only ---------------

TEST(wb_resolve_b1_lead_only_drops_on_full_word)
{
  // `\b\w+` — full Unicode word + lead \b → B-1 drop (wb_lead 0).
  const real::regex re   {R"(\b\w+)"};
  const auto        prog {re.raw_program()};
  EXPECT(prog.hints.greedy_cp_class >= 0);
  EXPECT_EQ(static_cast<int>(prog.hints.wb_lead), 0);
  EXPECT_EQ(static_cast<int>(prog.hints.wb_trail), 0);
}

TEST(wb_resolve_b1_trail_only_drops_on_full_word)
{
  const real::regex re   {R"(\w+\b)"};
  const auto        prog {re.raw_program()};
  EXPECT(prog.hints.greedy_cp_class >= 0);
  EXPECT_EQ(static_cast<int>(prog.hints.wb_lead), 0);
  EXPECT_EQ(static_cast<int>(prog.hints.wb_trail), 0);
}

// --- direct unit tests of peel / resolve (branch coverage of helpers) -------

TEST(wb_peel_helpers_direct_lead_trail_absent_and_reject)
{
  using real::detail::instr;
  using real::detail::opcode;
  using real::detail::assert_kind;
  using real::detail::peel_optional_lead_wb;
  using real::detail::peel_optional_trail_wb;

  // Empty stream: peel is no-op success.
  std::uint8_t lead {9};
  std::size_t  p    {0};
  EXPECT(peel_optional_lead_wb({}, p, lead));
  EXPECT_EQ(static_cast<int>(lead), 0);
  EXPECT_EQ(p, 0U);

  // Lead \b then body marker.
  const instr stream_b[] {
    {.op = opcode::assert_position, .arg8 = static_cast<std::uint8_t>(assert_kind::word_boundary)},
    {.op = opcode::byte, .arg8 = 'a'},
  };
  p    = 0;
  lead = 0;
  EXPECT(peel_optional_lead_wb(stream_b, p, lead));
  EXPECT_EQ(static_cast<int>(lead), 1);
  EXPECT_EQ(p, 1U);

  // Lead \B.
  const instr stream_B[] {
    {.op = opcode::assert_position, .arg8 = static_cast<std::uint8_t>(assert_kind::not_word_boundary)},
  };
  p    = 0;
  lead = 0;
  EXPECT(peel_optional_lead_wb(stream_B, p, lead));
  EXPECT_EQ(static_cast<int>(lead), 2);
  EXPECT_EQ(p, 1U);

  // Non-wb assert (line_start) → reject.
  const instr stream_caret[] {
    {.op = opcode::assert_position, .arg8 = static_cast<std::uint8_t>(assert_kind::line_start)},
  };
  p    = 0;
  lead = 0;
  EXPECT(!peel_optional_lead_wb(stream_caret, p, lead));
  EXPECT_EQ(p, 0U); // not advanced

  // Trail helpers mirror lead.
  std::uint8_t trail {0};
  p = 0;
  EXPECT(peel_optional_trail_wb(stream_b, p, trail));
  EXPECT_EQ(static_cast<int>(trail), 1);
  p     = 0;
  trail = 0;
  EXPECT(!peel_optional_trail_wb(stream_caret, p, trail));
}

TEST(wb_resolve_helpers_direct_policy_matrix)
{
  using real::detail::resolve_class_wb_hints;
  using real::detail::wb_redundant_for_full_word;
  using real::detail::is_full_ascii_word_class;
  using real::detail::is_ascii_word_subset_class;

  std::uint8_t ol {9};
  std::uint8_t ot {9};

  // \B → unarm.
  EXPECT(!resolve_class_wb_hints(true, true, 2, 0, ol, ot));
  EXPECT(!resolve_class_wb_hints(true, true, 0, 2, ol, ot));
  EXPECT(!resolve_class_wb_hints(false, true, 1, 2, ol, ot));

  // Superset under \b → unarm.
  EXPECT(!resolve_class_wb_hints(false, false, 1, 1, ol, ot));

  // Bare (no wb) → arm, drop hints.
  EXPECT(resolve_class_wb_hints(false, true, 0, 0, ol, ot));
  EXPECT_EQ(static_cast<int>(ol), 0);
  EXPECT_EQ(static_cast<int>(ot), 0);

  // Full word + \b → B-1 drop.
  EXPECT(wb_redundant_for_full_word(1, 1));
  EXPECT(wb_redundant_for_full_word(1, 0));
  EXPECT(wb_redundant_for_full_word(0, 1));
  EXPECT(!wb_redundant_for_full_word(2, 1)); // \B never redundant
  EXPECT(!wb_redundant_for_full_word(1, 2));
  EXPECT(!wb_redundant_for_full_word(0, 0));
  EXPECT(resolve_class_wb_hints(true, true, 1, 1, ol, ot));
  EXPECT_EQ(static_cast<int>(ol), 0);
  EXPECT_EQ(static_cast<int>(ot), 0);

  // Subset + \b → keep wrap (B-2).
  EXPECT(resolve_class_wb_hints(false, true, 1, 0, ol, ot));
  EXPECT_EQ(static_cast<int>(ol), 1);
  EXPECT_EQ(static_cast<int>(ot), 0);
  EXPECT(resolve_class_wb_hints(false, true, 0, 1, ol, ot));
  EXPECT_EQ(static_cast<int>(ol), 0);
  EXPECT_EQ(static_cast<int>(ot), 1);
  EXPECT(resolve_class_wb_hints(false, true, 1, 1, ol, ot));
  EXPECT_EQ(static_cast<int>(ol), 1);
  EXPECT_EQ(static_cast<int>(ot), 1);

  // Full-ASCII word class identity: high bit set ⇒ not full word.
  real::detail::char_class full {real::detail::word_set()};
  EXPECT(is_full_ascii_word_class(full));
  EXPECT(is_ascii_word_subset_class(full));
  full.set(0x80);
  EXPECT(!is_full_ascii_word_class(full));
  EXPECT(!is_ascii_word_subset_class(full)); // high non-word byte

  // Empty class is not a non-empty word subset.
  real::detail::char_class empty {};
  EXPECT(!is_ascii_word_subset_class(empty));
  EXPECT(!is_full_ascii_word_class(empty));
}

TEST(wb_peel_trail_B_and_absent_direct)
{
  using real::detail::instr;
  using real::detail::opcode;
  using real::detail::assert_kind;
  using real::detail::peel_optional_trail_wb;

  // Trail \B.
  const instr stream_B[] {
    {.op = opcode::assert_position, .arg8 = static_cast<std::uint8_t>(assert_kind::not_word_boundary)},
    {.op = opcode::save, .arg16 = 1},
  };
  std::size_t  p     {0};
  std::uint8_t trail {0};
  EXPECT(peel_optional_trail_wb(stream_B, p, trail));
  EXPECT_EQ(static_cast<int>(trail), 2);
  EXPECT_EQ(p, 1U);

  // Absent trail assert (save 1 at p) — success, trail stays 0.
  p     = 1;
  trail = 9;
  EXPECT(peel_optional_trail_wb(stream_B, p, trail));
  EXPECT_EQ(static_cast<int>(trail), 0);
  EXPECT_EQ(p, 1U);

  // Empty stream at EOF.
  p     = 0;
  trail = 9;
  EXPECT(peel_optional_trail_wb({}, p, trail));
  EXPECT_EQ(static_cast<int>(trail), 0);
}

// --- Unicode word identity helpers (resolve inputs for klass_cp) -------------

TEST(wb_unicode_word_cover_and_subset_edges)
{
  using real::detail::word_ranges_cover_interval;
  using real::detail::is_unicode_word_subset_cp_class;
  using real::detail::is_full_unicode_word_cp_class;
  using real::detail::is_ascii_word_byte;
  using real::detail::word_ranges;
  using real::detail::code_range;
  using real::detail::cp_class;

  // lo > hi
  EXPECT(!word_ranges_cover_interval(U'z', U'a'));

  // Beyond every word range → !found.
  EXPECT(!word_ranges_cover_interval(0x40000, 0x40000));

  // Starts inside a range but extends past it (multi-range advance then gap).
  EXPECT(!word_ranges_cover_interval(0x30, 0x3A)); // '0'..':' — digits then non-word

  // Single full range still covers.
  EXPECT(word_ranges_cover_interval(0x30, 0x39));
  EXPECT(word_ranges_cover_interval(0x61, 0x7A));

  // Subset reject: non-word ASCII member.
  cp_class space_ascii {};
  space_ascii.ascii.set(static_cast<std::uint8_t>(' '));
  EXPECT(!is_unicode_word_subset_cp_class(space_ascii, {}));

  // Subset reject: range slice out of buffer.
  cp_class oob {};
  oob.ascii.set(static_cast<std::uint8_t>('a'));
  oob.range_begin = 0;
  oob.range_count = 1;
  EXPECT(!is_unicode_word_subset_cp_class(oob, {}));

  // Full-\w identity: count matches word_hi but buffer too short → OOB.
  std::size_t word_hi {0};
  for (const auto& word_range : word_ranges) {
    if (word_range.lo >= 0x80U) {
      ++word_hi;
    }
  }
  cp_class full_shape {};
  for (unsigned b = 0; b < 128U; ++b) {
    if (is_ascii_word_byte(static_cast<std::uint8_t>(b))) {
      full_shape.ascii.set(static_cast<std::uint8_t>(b));
    }
  }
  full_shape.range_begin = 0;
  full_shape.range_count = static_cast<std::uint32_t>(word_hi);
  EXPECT(!is_full_unicode_word_cp_class(full_shape, {}));

  // Right count, wrong range contents → mismatch reject.
  std::vector<code_range> bogus(word_hi, code_range {.lo = 0x80U, .hi = 0x80U});
  EXPECT(!is_full_unicode_word_cp_class(full_shape, bogus));

  // Canonical high ranges from the table → exact full word.
  std::vector<code_range> hi_ranges;
  hi_ranges.reserve(word_hi);
  for (const auto& word_range : word_ranges) {
    if (word_range.lo >= 0x80U) {
      hi_ranges.push_back(word_range);
    }
  }
  EXPECT(is_full_unicode_word_cp_class(full_shape, hi_ranges));
  EXPECT(is_unicode_word_subset_cp_class(full_shape, hi_ranges));
}

// --- resolve via compile: superset / peel-reject interleave -----------------

TEST(wb_resolve_ascii_superset_unarms)
{
  // Space is non-word → not a word subset under `\b` → class-loop fast path off.
  const real::regex re {R"(\b[a-z ]+\b)"};
  EXPECT_EQ(static_cast<int>(re.raw_program().hints.greedy_class_loop), -1);
  EXPECT(hit(re, " a b ")); // general path still matches
}

TEST(wb_resolve_unicode_superset_unarms)
{
  // `[\w😀]` is a strict superset of `\w` — B-2 must not arm (unsound maximal run).
  // UTF-8 for U+1F600 (😀) embedded in the pattern string.
  const real::regex re {"\\b[\\w\xF0\x9F\x98\x80]+\\b"};
  EXPECT_EQ(static_cast<int>(re.raw_program().hints.greedy_cp_class), -1);
}

TEST(wb_peel_reject_line_end_then_class)
{
  // `$` is not peelable as trail-style lead; `^[a-z]+$` has no wb wrap.
  const real::regex re   {R"(^[a-z]+$)"};
  const auto        prog {re.raw_program()};
  EXPECT_EQ(static_cast<int>(prog.hints.wb_lead), 0);
  EXPECT_EQ(static_cast<int>(prog.hints.wb_trail), 0);
  EXPECT(hit(re, "abc"));
  EXPECT(!hit(re, "abc "));
  EXPECT(!hit(re, " abc"));
}
