// P0: deterministic route-pinning — a regression of recognition is a silent perf loss.
// Gate-safe (hints / seams only; no wall-clock). Profile-OFF.
#include <sciforge/test/framework.hpp>

#include <real/automata/lazy_dfa.hpp>
#include <real/real.hpp>

#include <string>
#include <string_view>

namespace {

  // Pad past lazy_dfa_min_input (512) so search routes actually engage.
  std::string pad(std::string_view unit,
                  std::size_t      bytes = 700)
  {
    std::string s;
    s.reserve(bytes + unit.size());
    while (s.size() < bytes) {
      s += unit;
    }
    return s;
  }
} // namespace

TEST(route_pin_w_plus_is_cp_class_loop)
{
  const real::regex re   {R"(\w+)"};
  const auto        prog {re.raw_program()};
  EXPECT(prog.hints.greedy_cp_class >= 0);
  EXPECT(prog.hints.greedy_cp_class_plus);
  EXPECT_EQ(static_cast<int>(prog.hints.greedy_class_loop), -1);
}

TEST(route_pin_az_plus_is_class_loop)
{
  const real::regex re   {R"([a-z]+)"};
  const auto        prog {re.raw_program()};
  EXPECT(prog.hints.greedy_class_loop >= 0);
  EXPECT_EQ(static_cast<int>(prog.hints.greedy_cp_class), -1);
}

TEST(route_pin_dog_is_exact_literal)
{
  const real::regex re {"dog"};
  EXPECT(re.raw_program().hints.exact_literal_len == 3);
}

TEST(route_pin_bw_b1_drop_and_cp)
{
  // Arc B-1: `\b\w+\b` simplifies to bare greedy_cp (wb dropped).
  const real::regex re   {R"(\b\w+\b)"};
  const auto        prog {re.raw_program()};
  EXPECT(prog.hints.greedy_cp_class >= 0);
  EXPECT_EQ(static_cast<int>(prog.hints.wb_lead), 0);
  EXPECT_EQ(static_cast<int>(prog.hints.wb_trail), 0);
}

TEST(route_pin_baz_b2_wrap)
{
  // Arc B-2: subset under `\b` keeps wrap hints on class-loop.
  const real::regex re   {R"(\b[a-z]+\b)"};
  const auto        prog {re.raw_program()};
  EXPECT(prog.hints.greedy_class_loop >= 0);
  EXPECT_EQ(static_cast<int>(prog.hints.wb_lead), 1);
  EXPECT_EQ(static_cast<int>(prog.hints.wb_trail), 1);
}

TEST(route_pin_superset_stays_general)
{
  // Recognition gap / unsound wrap: `\w` ∪ emoji must NOT arm greedy_cp under `\b`.
  std::string pat = "\\b[\\w";
  pat += "\xF0\x9F\x98\x80"; // 😀
  pat += "]+\\b";
  const real::regex re   {pat};
  const auto        prog {re.raw_program()};
  EXPECT_EQ(static_cast<int>(prog.hints.greedy_cp_class), -1);
  EXPECT_EQ(static_cast<int>(prog.hints.greedy_class_loop), -1);
}

TEST(route_pin_email_not_dedicated_fastpath)
{
  // Capturing email shape must not steal class/cp/literal fast paths — lazy-DFA / general owns it.
  const real::regex re   {R"((\w+)@(\w+))"};
  const auto        prog {re.raw_program()};
  EXPECT_EQ(static_cast<int>(prog.hints.greedy_cp_class), -1);
  EXPECT_EQ(static_cast<int>(prog.hints.greedy_class_loop), -1);
  EXPECT_EQ(static_cast<int>(prog.hints.exact_literal_len), 0);
  // Search on a long corpus still matches (route is DFA-or-general, not a dedicated class loop).
  const std::string text {pad("contact john.doe@example.com or jane@corp.io today ")};
  EXPECT(static_cast<bool>(re.search(text)));
}

TEST(route_pin_alt_is_fixed_alternation)
{
  const real::regex re {"dog|fox|cat"};
  EXPECT(re.raw_program().hints.fixed_alternation);
}

TEST(route_pin_class_fastpath_seam_falls_through)
{
  // Seam: disabling class/cp fast paths must not change spans (transparency).
  const std::string text {pad("the quick brown fox jumps 42 times ")};
  const real::regex re   {R"(\w+)"};

  real::detail::class_fastpath_disabled() = false;
  std::size_t n_on                        = re.count_matches(text);
  real::detail::class_fastpath_disabled() = true;
  std::size_t n_off                       = re.count_matches(text);
  real::detail::class_fastpath_disabled() = false;

  EXPECT_EQ(n_on, n_off);
  EXPECT(n_on > 0);
}

// The one-pass extractor is by far the most expensive thing the per-regex cache holds -- measured on a
// first `(\w+)@(\w+)` search, 884 us against 331 for the byte program and 117 for the lazy DFA. It used
// to be built by ensure_immutables() alongside the byte program, so EVERY route that needed the cheap
// half paid for the expensive one, including patterns with no capture to extract at all. Splitting it
// behind its own identity flag took that first search from 1490 to 573 us on arm64 and 1958 to 813 on
// x86-64, and `\d{4}-\d{2}-\d{2}` (no match, memmem-only) from 296 to 167.
//
// This pins the split itself, not a duration: if a route change starts building the extractor on a
// search that never extracts through it, the win is gone and nothing else would say so. A future route
// that legitimately needs it here must show the measurement that justifies the cost.
TEST(capture_free_search_does_not_build_the_onepass_extractor)
{
  // SHORT, deliberately: below lazy_dfa_min_input (512) the search does not take the lazy-DFA route,
  // which DOES extract through the table and so builds it (correctly). The saving is on a short first
  // search -- which is exactly what the criterion first_use/ group measures (its subject is 80 bytes)
  // and what a program doing one small match pays. Padding this past 512 engages the DFA route and the
  // table is built again; that is the route working as intended, not a regression.
  const std::string text {"say alpha@beta now"};

  // 2 slots: the whole-match span and nothing else -- there is no capture for an extractor to fill.
  const real::regex bare(R"(\w+@\w+)");
  EXPECT_EQ(bare.raw_program().slot_count, 2U);
  EXPECT(bare.search(text).matched());
  const real::detail::regex_immutables* const bi {bare.raw_program().immut};
  EXPECT(bi != nullptr);
  EXPECT(bi->built_for.load(std::memory_order_acquire) != nullptr); // the cheap half WAS built
  EXPECT(bi->op_table_for.load(std::memory_order_acquire) == nullptr);
  EXPECT(!bi->op_table.has_value());

  // A no-match scan is memmem-only and likewise never extracts.
  const real::regex nomatch(R"(\d{4}-\d{2}-\d{2})");
  EXPECT(!nomatch.search(text).matched());
  const real::detail::regex_immutables* const ni {nomatch.raw_program().immut};
  EXPECT(ni != nullptr);
  EXPECT(ni->op_table_for.load(std::memory_order_acquire) == nullptr);

  // The extractor is still built where captures ARE filled through it, so the split cost nothing:
  // an anchored full-match on a one-pass pattern is the route that consults it.
  const real::regex anchored(R"((\w+)@(\w+))");
  EXPECT(anchored.fullmatch("alpha@beta").matched());
  const real::detail::regex_immutables* const ai {anchored.raw_program().immut};
  EXPECT(ai != nullptr);
  EXPECT(ai->op_table_for.load(std::memory_order_acquire) != nullptr);
  EXPECT(ai->op_table.has_value());
}
