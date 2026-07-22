// `\b`/`\B` wrap on fixed_shape / alternation / exact_literal —
// oracle vs pure Pike (lazy_dfa + IL + trailing-LA seams forced off still leaves
// these shape paths; differential against a twin without the wrap where counts match,
// plus boundary cases the wrapper must not mis-accept).
#include <string>
#include <string_view>
#include <vector>

#include <sciforge/test/framework.hpp>
#include "real/automata/lazy_dfa.hpp"
#include "real/real.hpp"

namespace {

  bool search_bool(const real::regex& re,
                   std::string_view   t)
  {
    return static_cast<bool>(re.search(t));
  }

  std::size_t count(const real::regex& re,
                    std::string_view   t)
  {
    return re.count_matches(t);
  }
} // namespace

TEST(wb_hex_fixed_shape_hints_and_boundaries)
{
  const real::regex re {R"(\b[0-9a-f]{8}\b)"};
  const auto&       h  {re.raw_program().hints};
  EXPECT(h.fixed_shape);
  EXPECT_EQ(static_cast<int>(h.fixed_shape_simd_len), 8);
  EXPECT_EQ(static_cast<int>(h.wb_lead), 1);
  EXPECT_EQ(static_cast<int>(h.wb_trail), 1);

  EXPECT(search_bool(re, " deadbeef "));
  EXPECT(search_bool(re, "deadbeef"));
  EXPECT(!search_bool(re, "Xdeadbeef"));
  EXPECT(!search_bool(re, "deadbeefY"));
  EXPECT(!search_bool(re, "xdeadbeefy"));
  EXPECT(search_bool(re, "\ndeadbeef\n"));
  EXPECT_EQ(count(re, "a3f9c1d8 and deadbeef end"), 2U);
}

TEST(wb_alternation_trail_boundaries)
{
  const real::regex re {R"((?:foo|bar)\b)"};
  const auto&       h  {re.raw_program().hints};
  EXPECT(h.fixed_alternation);
  EXPECT_EQ(static_cast<int>(h.wb_trail), 1);
  EXPECT_EQ(static_cast<int>(h.wb_lead), 0);

  EXPECT(search_bool(re, " foo "));
  EXPECT(search_bool(re, " bar,"));
  EXPECT(!search_bool(re, " food "));
  EXPECT(!search_bool(re, "baritone"));
  EXPECT(search_bool(re, "foo")); // end of text is a boundary
  EXPECT_EQ(count(re, "foo bar foo"), 3U);
}

TEST(wb_exact_literal_both_sides)
{
  const real::regex re {R"(\bERROR\b)"};
  const auto&       h  {re.raw_program().hints};
  EXPECT_EQ(static_cast<int>(h.exact_literal_len), 5);
  EXPECT_EQ(static_cast<int>(h.wb_lead), 1);
  EXPECT_EQ(static_cast<int>(h.wb_trail), 1);

  EXPECT(search_bool(re, " ERROR "));
  EXPECT(search_bool(re, "ERROR"));
  EXPECT(!search_bool(re, "XERROR"));
  EXPECT(!search_bool(re, "ERRORY"));
  EXPECT(!search_bool(re, "XERRORY"));
}

TEST(wb_not_boundary_wrap)
{
  const real::regex re {R"(\Bfoo\B)"};
  const auto&       h  {re.raw_program().hints};
  EXPECT_EQ(static_cast<int>(h.wb_lead), 2);
  EXPECT_EQ(static_cast<int>(h.wb_trail), 2);
  // `\Bfoo\B` needs word chars on both sides (no boundary at start/end of "foo").
  EXPECT(search_bool(re, "xfooy"));
  EXPECT(!search_bool(re, " foo "));
  EXPECT(!search_bool(re, "foo"));
}

TEST(wb_hex_count_matches_proxy_hitcount_equal_when_all_bounded)
{
  // On a corpus where every hex8 is already word-bounded, counts must match the proxy.
  const real::regex re   {R"(\b[0-9a-f]{8}\b)"};
  const real::regex px   {R"([0-9a-f]{8})"};
  const std::string text {
    " a3f9c1d8 deadbeef 00000000 ffffffff \n"
    " more a3f9c1d8 tokens\n"};
  EXPECT_EQ(count(re, text), count(px, text));
}

TEST(wb_empty_and_edge_text)
{
  const real::regex re {R"(\b[0-9a-f]{8}\b)"};
  EXPECT(!search_bool(re, ""));
  EXPECT(search_bool(re, "abcdefaa")); // [0-9a-f]{8} + whole-string boundaries
  EXPECT(!search_bool(re, "abcdefg")); // length 7
}

// fuzz-compat crash-86573f5 (CI after tsan-core land): mid-pattern `\b` was peeled as a *trail*
// wrap on fixed_shape, so match_byte_klass_run stopped at the assert and dropped the following
// literal — `\w{2}\bthe` falsely matched just `\w{2}` under flags::bytes (compat's default).
// A true trail `\b` (`\w{2}\b`) must still arm fixed_shape + wb_trail.
TEST(wb_mid_pattern_boundary_not_peeled_as_trail_fixed_shape)
{
  constexpr real::flags bytes_ecma {real::flags::bytes | real::flags::ecma};
  const real::regex     mid        {R"(\w{2}\bthe)", bytes_ecma};
  const auto&           hm         {mid.raw_program().hints};
  EXPECT(!hm.fixed_shape); // mid \b disqualifies the pure fixed-shape run
  EXPECT_EQ(static_cast<int>(hm.wb_trail), 0);
  // Impossible shape: \w{2} ends on a word char, "the" starts on one — no \b junction either.
  EXPECT(!search_bool(mid, "\xa3ox"));
  EXPECT(!search_bool(mid, "oxthe"));
  EXPECT(!search_bool(mid, "ox the"));

  const real::regex trail {R"(\w{2}\b)", bytes_ecma};
  const auto&       ht    {trail.raw_program().hints};
  EXPECT(ht.fixed_shape);
  EXPECT_EQ(static_cast<int>(ht.wb_trail), 1);
  EXPECT(search_bool(trail, "\xa3ox")); // true trail at end of text
  EXPECT(search_bool(trail, "ox "));
  // "oxy": `\w{2}` can start at 'x' → "xy" + end-of-text `\b` — a legitimate match.
  EXPECT(search_bool(trail, "oxy"));
  EXPECT(!search_bool(trail, "x")); // only one word char
}
