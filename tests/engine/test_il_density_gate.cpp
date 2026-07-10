// O1: IL density-gate — dense capture-free inner-literal falls through to DFA; sparse stays IL;
// spans always match the pure-core route (semantic transparency).
#include <sciforge/test/framework.hpp>

#include <real/automata/lazy_dfa.hpp>
#include <real/real.hpp>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

  std::string pad_unit(std::string_view unit,
                       std::size_t      bytes)
  {
    std::string s;
    s.reserve(bytes + unit.size());
    while (s.size() < bytes) {
      s += unit;
    }
    return s;
  }

  std::vector<std::pair<std::size_t, std::size_t>> spans(const real::regex& re,
                                                         std::string_view   text)
  {
    std::vector<std::pair<std::size_t, std::size_t>> out;
    for (const auto& m : re.find_iter(text)) {
      out.emplace_back(m.start(0), m.end(0));
    }
    return out;
  }
} // namespace

TEST(il_density_gate_dense_ncap_matches_core)
{
  // Dense id_* tokens: density gate must not change spans vs pure core (IL forced off).
  const std::string text {pad_unit("id_42 name_foo val_7 key_bar ok_9 ", 300000)};
  const real::regex re   {R"((?:\w+)_(?:\w+))"};

  real::detail::inner_literal_route_disabled() = false;
  const auto gated {spans(re, text)};
  real::detail::inner_literal_route_disabled() = true;
  const auto core  {spans(re, text)};
  real::detail::inner_literal_route_disabled() = false;

  EXPECT(!gated.empty());
  EXPECT(gated == core);
}

TEST(il_density_gate_dense_cap_matches_core)
{
  // Capturing shape: gate must NOT fire (slot_count > 2); still equal to core for correctness.
  const std::string text {pad_unit("id_42 name_foo val_7 ", 200000)};
  const real::regex re   {R"((\w+)_(\w+))"};

  real::detail::inner_literal_route_disabled() = false;
  const auto a {spans(re, text)};
  real::detail::inner_literal_route_disabled() = true;
  const auto b {spans(re, text)};
  real::detail::inner_literal_route_disabled() = false;

  EXPECT(!a.empty());
  EXPECT(a == b);
  // Groups filled
  const auto all {re.find_all(text)};
  EXPECT(!all.empty());
  EXPECT(!all[0][1].empty());
  EXPECT(!all[0][2].empty());
}

TEST(il_density_gate_sparse_stays_correct)
{
  // Sparse `_`: IL remains the right route; spans match core.
  std::string text;
  for (int i = 0; i < 200; ++i) {
    text += "plain english words with no special token on this line at all ";
  }
  text += "then id_42 appears once ";
  for (int i = 0; i < 200; ++i) {
    text += "more plain filler words running along ";
  }
  // Past il_min_haystack so IL is eligible (not small-haystack abandon).
  while (text.size() < 300000) {
    text += "more plain filler words running along ";
  }

  const real::regex re {R"((?:\w+)_(?:\w+))"};
  real::detail::inner_literal_route_disabled() = false;
  const auto gated     {spans(re, text)};
  real::detail::inner_literal_route_disabled() = true;
  const auto core      {spans(re, text)};
  real::detail::inner_literal_route_disabled() = false;

  EXPECT_EQ(gated.size(), 1U);
  EXPECT(gated == core);
}

TEST(il_density_gate_date_and_email_shapes_ok)
{
  // Non-reg: fixed-shape date (IL-fusion) and email still match core.
  const std::string log  {pad_unit("2026-07-09T12:00:00 INFO user=alice id=42\n", 200000)};
  const std::string mail {pad_unit("contact john.doe@example.com or plain words ", 200000)};

  for (const char* pat : {R"(\d{4}-\d{2}-\d{2})", R"((\w+)@(\w+))"}) {
    const real::regex re {pat};
    real::detail::inner_literal_route_disabled() = false;
    const auto a         {spans(re, pat[1] == 'd' ? log : mail)};
    real::detail::inner_literal_route_disabled() = true;
    const auto b         {spans(re, pat[1] == 'd' ? log : mail)};
    real::detail::inner_literal_route_disabled() = false;
    EXPECT(a == b);
  }
}
