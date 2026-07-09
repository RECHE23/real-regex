// Stage-2 S2a: multi-accept unanchored which_matched vs N×search oracle.
#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <sciforge/test/framework.hpp>
#include "real/dfa.hpp"
#include "real/real.hpp"

namespace {

  std::vector<bool> nwalk_which(const std::vector<real::regex>& pats,
                                std::string_view                text)
  {
    std::vector<bool> hit;
    hit.reserve(pats.size());
    for (const auto& re : pats) {
      hit.push_back(static_cast<bool>(re.search(text)));
    }
    return hit;
  }

  std::vector<std::string> present_log()
  {
    return {
      R"([0-9]{4}-[0-9]{2}-[0-9]{2})",
      R"([0-9]{2}:[0-9]{2}:[0-9]{2})",
      R"(error|warn|info|debug|fatal)",
      R"([a-f0-9]{8})",
      R"([0-9]+\.[0-9]+\.[0-9]+\.[0-9]+)",
      R"(GET|POST|PUT|DELETE)",
      R"(user=[a-z]+)",
      R"(q=[0-9]+)",
    };
  }
} // namespace

TEST(which_matched_equals_nwalk_search)
{
  const auto               raw {present_log()};
  std::vector<real::regex> pats;
  pats.reserve(raw.size());
  for (const auto& p : raw) {
    pats.emplace_back(p);
  }
  const real::dfa d {std::span<const real::regex>(pats), real::dfa_mode::which_matched};
  EXPECT(d.is_unanchored());
  EXPECT_EQ(d.rule_count(), pats.size());

  const std::string text =
    "2026-06-13 12:04:55 error id=a3f9c1d8 GET /api/x from 10.0.2.15 user=bob q=42\n"
    "plain line\n";
  const auto got = d.which_matched(text);
  const auto ora = nwalk_which(pats, text);
  EXPECT_EQ(got.size(), ora.size());
  for (std::size_t i = 0; i < got.size(); ++i) {
    EXPECT_EQ(static_cast<int>(got[i]), static_cast<int>(ora[i]));
  }
}

TEST(which_matched_both_on_overlap_prefix)
{
  // "ab" and "a" both match "ab" (which-matched); munch would pick one.
  std::vector<real::regex> pats;
  pats.emplace_back("ab");
  pats.emplace_back("a");
  const real::dfa d   {std::span<const real::regex>(pats), real::dfa_mode::which_matched};
  const auto      hit {d.which_matched("xx ab yy")};
  EXPECT(hit[0] && hit[1]);
}

TEST(which_matched_absent_false)
{
  std::vector<real::regex> pats;
  pats.emplace_back("needle");
  pats.emplace_back("zzz_absent");
  const real::dfa d   {std::span<const real::regex>(pats), real::dfa_mode::which_matched};
  const auto      hit {d.which_matched("a long haystack with a needle")};
  EXPECT(hit[0]);
  EXPECT(!hit[1]);
}

TEST(munch_mode_unchanged_default)
{
  std::vector<real::regex> pats;
  pats.emplace_back("ab");
  pats.emplace_back("a");
  const real::dfa d {std::span<const real::regex>(pats)}; // default munch
  EXPECT(!d.is_unanchored());
  const auto m      {d.match("ab")};
  EXPECT(m.has_value());
  EXPECT_EQ(m->rule_index, 0U); // longest "ab"
  EXPECT_EQ(m->length, 2U);
}

TEST(which_matched_state_count_bounded_log_patterns)
{
  auto raw = present_log();
  for (int i = 0; i < 56; ++i) {
    raw.push_back("SEV" + std::to_string(i) + "|trace" + std::to_string(i));
  }
  std::vector<real::regex> pats;
  pats.reserve(raw.size());
  for (const auto& p : raw) {
    pats.emplace_back(p);
  }
  const real::dfa d {std::span<const real::regex>(pats), real::dfa_mode::which_matched};
  // Unanchored can be larger than munch proxy; must stay under production cap.
  EXPECT(d.state_count() < 65536U);
  EXPECT(d.state_count() > 0U);
}
