// Stage-2 S2b: N-hybrid RegexSet — fused path == N-walks oracle, interleaved map.
#include <string>
#include <string_view>
#include <vector>

#include <sciforge/test/framework.hpp>
#include "real/regex_set.hpp"
#include "real/real.hpp"

namespace {

  std::vector<bool> oracle_nwalk(std::span<const std::string_view> pats,
                                 std::string_view                  text)
  {
    std::vector<bool> hit;
    for (const auto p : pats) {
      hit.push_back(static_cast<bool>(real::regex(p).search(text)));
    }
    return hit;
  }

  std::vector<std::string> log_eligible()
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

  //! Pad with DFA-eligible absents so eligible.count ≥ fused_min_eligible.
  std::vector<std::string_view> pad_eligible(std::vector<std::string>& owned,
                                             std::size_t               n)
  {
    auto base = log_eligible();
    owned     = base;
    for (std::size_t i = base.size(); i < n; ++i) {
      owned.push_back("SEV" + std::to_string(i) + "|tr" + std::to_string(i) + "x");
    }
    std::vector<std::string_view> views;
    views.reserve(owned.size());
    for (const auto& s : owned) {
      views.emplace_back(s);
    }
    return views;
  }
} // namespace

TEST(hybrid_small_set_uses_nwalks_not_fused)
{
  // Far below fused_min_eligible → pure Stage-1.
  const real::regex_set set {"alpha", "beta", "gamma"};
  EXPECT(!set.uses_fused());
  EXPECT_EQ(set.eligible_count(), 0U);
  EXPECT(set.is_match("xx beta yy"));
  EXPECT_EQ(set.which("xx beta yy")[0], 1U);
}

TEST(hybrid_large_eligible_uses_fused)
{
  std::vector<std::string> owned;
  const auto               views {pad_eligible(owned, real::regex_set::fused_min_eligible)};
  const real::regex_set    set(views);
  EXPECT(set.uses_fused());
  EXPECT_EQ(set.eligible_count(), real::regex_set::fused_min_eligible);
  const std::string text =
    "2026-06-13 12:04:55 error id=a3f9c1d8 GET /api user=bob q=42\n";
  const auto got = set.matches(text);
  const auto ora = oracle_nwalk(views, text);
  EXPECT_EQ(got.size(), ora.size());
  for (std::size_t i = 0; i < got.size(); ++i) {
    EXPECT_EQ(static_cast<int>(got[i]), static_cast<int>(ora[i]));
  }
}

TEST(hybrid_interleaved_eligible_ineligible_map)
{
  // Construction indices: 0,2,4,… eligible pads; 1,3 lookbehind (ineligible).
  // Catches fused rule_index vs construction-order inversion.
  std::vector<std::string>      owned;
  std::vector<std::string_view> views;
  // Need ≥ fused_min_eligible eligibles → build many eligibles interleaved with a few LA.
  const auto  base = log_eligible();
  std::size_t elig {0};
  for (std::size_t i = 0; elig < real::regex_set::fused_min_eligible || i < 4; ++i) {
    if (i % 2 == 1 && i < 8) {
      // Ineligible lookbehind (DFA rejects).
      owned.emplace_back(R"((?<=id=)[a-f0-9]{8})");
    }
    else {
      if (elig < base.size()) {
        owned.push_back(base[elig]);
      }
      else {
        owned.push_back("PAD" + std::to_string(elig) + "|tok" + std::to_string(elig));
      }
      ++elig;
    }
  }
  views.reserve(owned.size());
  for (const auto& s : owned) {
    views.emplace_back(s);
  }
  const real::regex_set set(views);
  EXPECT(set.uses_fused());
  // Text must hit lookbehind (id=a3f9c1d8) and date/error.
  const std::string text =
    "2026-06-13 error id=a3f9c1d8 GET /api user=bob q=7\n";
  const auto got = set.matches(text);
  const auto ora = oracle_nwalk(views, text);
  EXPECT_EQ(got.size(), ora.size());
  for (std::size_t i = 0; i < got.size(); ++i) {
    EXPECT_EQ(static_cast<int>(got[i]), static_cast<int>(ora[i]));
  }
  // At least one lookbehind index true if present in set.
  bool any_la {false};
  for (std::size_t i = 0; i < views.size(); ++i) {
    if (views[i].find("?<=") != std::string_view::npos) {
      any_la = any_la || got[i];
    }
  }
  EXPECT(any_la);
}

TEST(hybrid_region_falls_back_to_nwalks)
{
  std::vector<std::string> owned;
  const auto               views {pad_eligible(owned, real::regex_set::fused_min_eligible)};
  const real::regex_set    set(views);
  EXPECT(set.uses_fused());
  const std::string text = "zzzz 2026-06-13 error id=a3f9c1d8 end";
  // Region that excludes the date at the start of the interesting part.
  const auto full = set.matches(text);
  const auto reg  = set.matches(text, 0, 10); // only "zzzz 2026"
  // Oracle for region
  std::vector<bool> ora;
  ora.reserve(views.size());
  for (const auto p : views) {
    ora.push_back(static_cast<bool>(real::regex(p).search(text, 0, 10)));
  }
  for (std::size_t i = 0; i < reg.size(); ++i) {
    EXPECT_EQ(static_cast<int>(reg[i]), static_cast<int>(ora[i]));
  }
  (void) full;
}

TEST(hybrid_brace_init_still_works)
{
  const real::regex_set set {"a", "b"};
  EXPECT(!set.uses_fused());
  EXPECT(set.matches("xb")[1]);
}
