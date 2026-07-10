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
