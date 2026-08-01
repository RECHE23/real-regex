// real::regex_set — Stage-1 which-matched (N-walks + bitset).
// Oracle primary: N × regex::search (self-consistent; works for lookarounds RE2 rejects).
// Secondary RE2::Set cross-check lives in the multi-pattern bench (optional dep), not here —
// so the unit suite stays free of RE2 link requirements.
#include <algorithm>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <sciforge/test/framework.hpp>
#include "real/regex_set.hpp"
#include "real/real.hpp"

using namespace std::string_view_literals;

namespace {

  //! Primary oracle: which patterns match via independent search (must equal regex_set).
  std::vector<bool> oracle_n_search(std::span<const std::string_view> pats,
                                    std::string_view                  text,
                                    std::size_t                       pos    = 0,
                                    std::size_t                       endpos = real::npos)
  {
    std::vector<bool> hit;
    hit.reserve(pats.size());
    for (const std::string_view p : pats) {
      const real::regex re(p);
      hit.push_back(static_cast<bool>(re.search(text, pos, endpos)));
    }
    return hit;
  }

  bool any_of(const std::vector<bool>& hit)
  {
    return std::ranges::any_of(hit, [](bool b) {
                                 return b;
                               });
  }
} // namespace

TEST(regex_set_empty)
{
  const std::vector<std::string_view> none {};
  const real::regex_set               set(none);
  EXPECT(set.empty());
  EXPECT_EQ(set.size(), 0U);
  EXPECT(!set.is_match("anything"));
  EXPECT(set.matches("anything").empty());
  EXPECT(set.which("anything").empty());
}

TEST(regex_set_brace_init_and_vector_string)
{
  // Inline brace-init (const char* → string_view) — public ergonomics.
  const real::regex_set braced {"alpha", "beta", "gamma"};
  EXPECT_EQ(braced.size(), 3U);
  EXPECT(braced.is_match("xx beta yy"));
  EXPECT_EQ(braced.which("xx beta yy").size(), 1U);
  EXPECT_EQ(braced.which("xx beta yy")[0], 1U);

  // Owning strings (vector<string>) — common construction path.
  const std::vector<std::string> owned {"error|warn", R"([0-9]{4})", "absent"};
  const real::regex_set          from_vec(owned);
  EXPECT_EQ(from_vec.size(), 3U);
  EXPECT(from_vec.matches("error 2026")[0]);
  EXPECT(from_vec.matches("error 2026")[1]);
  EXPECT(!from_vec.matches("error 2026")[2]);
}

TEST(regex_set_which_matched_order_is_construction_order)
{
  const std::string_view pats[] = {"alpha", "beta", "gamma"};
  const real::regex_set  set(pats, 3);
  EXPECT_EQ(set.size(), 3U);
  const auto hit = set.matches("xx beta yy");
  EXPECT_EQ(hit.size(), 3U);
  EXPECT(!hit[0]);
  EXPECT(hit[1]);
  EXPECT(!hit[2]);
  const auto ids = set.which("xx beta yy");
  EXPECT_EQ(ids.size(), 1U);
  EXPECT_EQ(ids[0], 1U);
}

TEST(regex_set_is_match_any)
{
  const std::string_view pats[] = {"nope", "findme", "zzz"};
  const real::regex_set  set(pats, 3);
  EXPECT(set.is_match("please findme here"));
  EXPECT(!set.is_match("nothing here"));
}

TEST(regex_set_matches_oracle_n_search)
{
  // Present + absent mix (table-A style): date, error-ish, hex, absent literal.
  const std::string_view pats[] = {
    R"([0-9]{4}-[0-9]{2}-[0-9]{2})",
    R"(error|warn|info)",
    R"([a-f0-9]{8})",
    R"(absent_token_xyz)",
    R"(GET|POST)",
  };
  const std::string      text =
    "2026-06-13 12:04:55 error id=a3f9c1d8 GET /api/x from 10.0.0.1\n"
    "plain line without tokens\n";
  const real::regex_set set(pats, 5);
  const auto            got    = set.matches(text);
  const auto            oracle = oracle_n_search(pats, text);
  EXPECT_EQ(got.size(), oracle.size());
  for (std::size_t i = 0; i < got.size(); ++i) {
    EXPECT_EQ(static_cast<int>(got[i]), static_cast<int>(oracle[i]));
  }
  EXPECT_EQ(static_cast<int>(set.is_match(text)), static_cast<int>(any_of(oracle)));
}

TEST(regex_set_region_pos_endpos)
{
  const std::string_view pats[] = {"abc", "def"};
  const real::regex_set  set(pats, 2);
  // Subject: "xx abc yy def zz" — "abc" at 3..6, "def" at 10..13
  const std::string_view text = "xx abc yy def zz";
  const auto             all  = set.matches(text);
  EXPECT(all[0] && all[1]);
  // Only first half: "def" out of region
  const auto left = set.matches(text, 0, 8);
  EXPECT(left[0]);
  EXPECT(!left[1]);
  // From pos past "abc"
  const auto right = set.matches(text, 8);
  EXPECT(!right[0]);
  EXPECT(right[1]);
}

TEST(regex_set_compile_fail_throws)
{
  const std::string_view pats[] = {"ok", "(?>a|b)"}; // atomic groups: Tier 1 bodies only; a compound/alternating body is not
  EXPECT_THROWS(real::regex_set(pats, 2), real::regex_error);
}

TEST(regex_set_lookaround_oracle_n_search)
{
  // Lookahead: RE2 cannot compile; primary oracle is still N×search REAL.
  const std::string_view pats[] = {R"([a-z]+(?=[a-z]))", R"(zzz_absent)"};
  const real::regex_set  set(pats, 2);
  const std::string_view text = "abc def";
  const auto             got  = set.matches(text);
  const auto             ora  = oracle_n_search(pats, text);
  EXPECT_EQ(got.size(), 2U);
  EXPECT_EQ(static_cast<int>(got[0]), static_cast<int>(ora[0]));
  EXPECT_EQ(static_cast<int>(got[1]), static_cast<int>(ora[1]));
  EXPECT(got[0]);
  EXPECT(!got[1]);
}

TEST(regex_set_member_access_for_capture_rerun)
{
  const std::string_view pats[] = {R"((\d+))", R"(nope)"};
  const real::regex_set  set(pats, 2);
  EXPECT(set.matches("x42y")[0]);
  const auto m = set[0].search("x42y");
  EXPECT(m);
  EXPECT_EQ(m[1], "42"sv);
}

// Sanity: regex_set must not be implementable via real::dfa (different contract).
// This test only documents the product rule — dfa is maximal-munch one-winner.
TEST(regex_set_is_not_dfa_munch)
{
  // Two patterns both match at the same start; set reports BOTH; dfa would pick one.
  const std::string_view pats[] = {"ab", "a"};
  const real::regex_set  set(pats, 2);
  const auto             hit = set.matches("ab");
  EXPECT(hit[0] && hit[1]); // which-matched: both true
}

// The fused build's threshold, pinned at its edges and with a mixed set. Nothing pinned this before:
// the partition that decides it runs, or does not run, entirely inside the constructor, and its only
// externally visible consequences are uses_fused()/eligible_count() and the answers themselves.
//
// It became worth pinning when the constructor learned to skip the partition outright for a set
// smaller than the threshold -- an exact short-circuit, since the eligible subset is at most the whole
// set, so a smaller set can never reach it. That is provably behaviour-neutral and therefore has no
// observable of its own to test; what CAN be tested is that the outcomes either side of the boundary
// are what they were, which is what this does.
TEST(regex_set_fused_threshold_edges_and_mixed_sets)
{
  const auto build = [](std::size_t n, std::size_t ineligible) {
                       std::vector<std::string> pats;
                       pats.reserve(n);
                       for (std::size_t i = 0; i < n; ++i) {
                         // A lookaround is DFA-ineligible; the rest are plain enough to fuse.
                         pats.push_back(i < ineligible
                                          ? "X" + std::to_string(i) + "(?=[0-9])"
                                          : "ERR" + std::to_string(i) + "[0-9]{2}[a-z]+");
                       }
                       return pats;
                     };
  const auto make_set = [](const std::vector<std::string>& pats) {
                          std::vector<std::string_view> views(pats.begin(), pats.end());
                          return real::regex_set {std::span<const std::string_view> {views}};
                        };

  // Just under the threshold: no fused table, and eligible_count reports 0 rather than the count of
  // patterns that WOULD have been eligible -- the set falls back to N-walks wholesale.
  const auto under = build(55, 0);
  const auto s55   = make_set(under);
  EXPECT(!s55.uses_fused());
  EXPECT_EQ(s55.eligible_count(), 0U);
  EXPECT(s55.is_match("ERR942abc"));

  // At the threshold and one past it: fused, and every member counted.
  const auto at   = build(56, 0);
  const auto s56  = make_set(at);
  EXPECT(s56.uses_fused());
  EXPECT_EQ(s56.eligible_count(), 56U);
  EXPECT(s56.is_match("ERR942abc"));

  const auto over = build(57, 0);
  const auto s57  = make_set(over);
  EXPECT(s57.uses_fused());
  EXPECT_EQ(s57.eligible_count(), 57U);

  // A big enough set whose ELIGIBLE subset falls short: the partition must run (the set is large
  // enough that it might have qualified) and then decline. This is the case the short-circuit must
  // not swallow -- 60 members, 10 of them ineligible, leaves 50 and the fused table stays unbuilt.
  const auto mixed_short = build(60, 10);
  const auto s60a        = make_set(mixed_short);
  EXPECT(!s60a.uses_fused());
  EXPECT_EQ(s60a.eligible_count(), 0U);
  EXPECT(s60a.is_match("ERR1042abc")); // a member past the ineligible prefix still answers

  // And the same size with few enough ineligibles to clear it.
  const auto mixed_ok = build(60, 4);
  const auto s60b     = make_set(mixed_ok);
  EXPECT(s60b.uses_fused());
  EXPECT_EQ(s60b.eligible_count(), 56U);
  EXPECT(s60b.is_match("ERR942abc"));
  EXPECT(s60b.is_match("X07")); // an ineligible member is still searched, by N-walk
}
