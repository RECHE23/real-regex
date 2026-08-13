// real::regex_set — Stage-1 which-matched (N-walks + bitset).
// Oracle primary: N × regex::search (self-consistent; works for lookarounds RE2 rejects).
// Secondary RE2::Set cross-check lives in the multi-pattern bench (optional dep), not here —
// so the unit suite stays free of RE2 link requirements.
#include <algorithm>
#include <array>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <sciforge/test/framework.hpp>
#include "real/engine/prefilter.hpp"
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

// --- byte filter: the SKIP path, which nothing above exercises ---------------------------------
//
// Every subject in this file so far contains a byte one member can start with, so the filter finds a
// candidate and the N-walk resumes exactly as before -- the skip itself, which is the whole reason the
// filter exists, went unpinned. These two tests take the two halves: the scan primitive on its own, and a
// set whose union is provably absent from the subject.

TEST(find_members_scan_hit_miss_and_boundaries)
{
  const std::array<std::uint8_t, 8> one   {{'z'}};
  const std::array<std::uint8_t, 8> eight {{'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h'}};

  // Long enough for the 16-byte block loop, then a short one for the scalar tail: the boundary is where a
  // block-scan-then-tail loop gets its off-by-one, and neither half is reachable from the other.
  const std::string long_no   {"....................................."}; // 37 bytes, no member
  const std::string long_hit  {"...................z................."};
  const std::string short_no  {"....."};
  const std::string short_hit {"..z.."};

  EXPECT_EQ(real::detail::find_members(long_no, 0, one, 1), real::npos);
  EXPECT_EQ(real::detail::find_members(long_hit, 0, one, 1), 19U);
  EXPECT_EQ(real::detail::find_members(short_no, 0, one, 1), real::npos);
  EXPECT_EQ(real::detail::find_members(short_hit, 0, one, 1), 2U);

  // `pos` must be honoured: the hit before it is not a hit.
  EXPECT_EQ(real::detail::find_members(long_hit, 20, one, 1), real::npos);
  EXPECT_EQ(real::detail::find_members(long_hit, 19, one, 1), 19U);

  // Eight members, the widest the scan carries, and the last one declared is still found.
  const std::string eight_hit {"zzzzzzzzzzzzzzzzzzzzzzzzhzzzz"};
  EXPECT_EQ(real::detail::find_members(eight_hit, 0, eight, 8), 24U);
  EXPECT_EQ(real::detail::find_members("zzzz", 0, eight, 8), real::npos);

  // Degenerate arguments answer rather than read out of range.
  EXPECT_EQ(real::detail::find_members("abc", 3, one, 1), real::npos);
  EXPECT_EQ(real::detail::find_members("", 0, one, 1), real::npos);
  EXPECT_EQ(real::detail::find_members("abc", 0, one, 0), real::npos);
}

TEST(regex_set_byte_filter_skips_and_agrees_with_the_n_walk)
{
  // Two rare-prefix literals: both are single-first-byte members, so where the filter is ARMED
  // (`detail::have_members_scan`, NEON only -- see its note for the x86-64 measurement that disarms it)
  // its union is {E, W} and this subject, holding neither, exercises the skip. Where it is not armed the
  // set N-walks and the same assertions still hold: they check the ANSWER, which must not depend on
  // whether a filter ran. Only the coverage claim is conditional, not the expectations.
  const std::vector<std::string_view> pats {"ERROR", "WARN"};
  const real::regex_set               set {std::span<const std::string_view> {pats}};
  const std::string                   absent {"quiet log line with nothing of interest here at all"};

  const auto oracle {[&](std::string_view text, std::size_t pos, std::size_t endpos) {
                       std::vector<bool> want(pats.size(), false);
                       for (std::size_t i = 0; i < pats.size(); ++i) {
                         const real::regex re {pats[i]};
                         want[i] = static_cast<bool>(re.search(text, pos, endpos)); // explicit: the result is not implicitly bool
                       }
                       return want;
                     }};

  EXPECT(!set.is_match(absent));
  EXPECT(set.matches(absent) == oracle(absent, 0, real::npos));

  // And it must not skip when the union IS present, whether or not a full match follows: `E` occurs here,
  // so the filter admits and the walk decides.
  const std::string near_miss {"quiet log with an E and a W, neither word spelled"};
  EXPECT(!set.is_match(near_miss));
  EXPECT(set.matches(near_miss) == oracle(near_miss, 0, real::npos));

  const std::string hit {"line: WARN queue depth"};
  EXPECT(set.is_match(hit));
  EXPECT(set.matches(hit) == oracle(hit, 0, real::npos));

  // Regions take the same walk (a fused set with pos/endpos falls here too), so the skip must hold there.
  EXPECT(set.matches(hit, 0, 6U) == oracle(hit, 0, 6U));   // "line: " — WARN is past the region
  EXPECT(set.matches(hit, 6U, real::npos) == oracle(hit, 6U, real::npos));
  EXPECT_EQ(set.is_match(hit, 0, 6U), false);
}
