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

  //! The FIRST span only, through `search` -- the surface `run()` serves, and the only one that still
  //! consults the density gate now that the batched filler is muted.
  std::pair<std::size_t, std::size_t> first_span(const real::regex& re,
                                                 std::string_view   text)
  {
    const auto m {re.search(text)};
    return m.matched() ? std::pair {m.start(0), m.end(0)} : std::pair {real::npos, real::npos};
  }

  //! `n` bytes of FAILING candidates -- one isolated `_`, which `(?:\w+)_(?:\w+)` cannot complete --
  //! spaced `every` bytes, then one real match. `search` must walk the failures to reach it, which is
  //! what accumulates the gate's candidate sample; a corpus of matches would return on the first one.
  std::string failing_then_match(std::size_t every,
                                 std::size_t n)
  {
    std::string unit(every, ' ');
    unit[1] = '_';
    std::string out;
    while (out.size() + unit.size() + 5 <= n) {
      out += unit;
    }
    out += "id_42";
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

// The threshold's VALUE, bracketed. Everything above pins semantic transparency -- that the gate
// changes only which route runs, never the spans -- which is the contract that matters and also the
// reason none of it can react to il_density_milli_threshold moving: the answers are equal whichever
// way the gate goes. The sabotage sweep (tools/sabotage_sweep.py) reported the constant unguarded for
// exactly that reason, on a file that tests the gate thoroughly.
//
// So these two read the decision itself, through the same kind of seam the AC gate has. One '_' every
// N bytes gives a candidate density of 1000/N per 1000 bytes: N=16 is 62, just past the threshold of
// 60, and N=20 is 50, just short. They fail in opposite directions, which is what makes the pair pin
// a value rather than a sign.
TEST(il_density_gate_abandons_just_past_the_threshold)
{
  const std::string text {failing_then_match(16, 300000)}; // one '_' per 16 bytes -> 62
  const real::regex re   {R"((?:\w+)_(?:\w+))"};

  // THROUGH `search`, NOT `find_iter`. The gate lives in `run()` only: the batched filler stopped
  // consulting it, because that counter is read before reverse/confirm and so cannot tell a candidate
  // that fails from one that completes -- the filler's stream is the latter. These two tests exist to
  // pin the THRESHOLD from both sides, and a surface that no longer asks would pin nothing while still
  // passing (its twin below did exactly that for one run).
  real::detail::inner_literal_route_disabled() = false;
  real::detail::il_density_last_abandoned()    = false;
  const auto gated {first_span(re, text)};
  EXPECT(real::detail::il_density_last_abandoned());
  EXPECT(gated.first != real::npos);

  // And still the same answers as the core route, which is the property the rest of this file holds.
  real::detail::inner_literal_route_disabled() = true;
  const auto core {first_span(re, text)};
  real::detail::inner_literal_route_disabled() = false;
  EXPECT(gated == core);
}

TEST(il_density_gate_stays_on_the_route_just_short_of_the_threshold)
{
  const std::string text {failing_then_match(20, 300000)}; // one '_' per 20 bytes -> 50
  const real::regex re   {R"((?:\w+)_(?:\w+))"};

  real::detail::inner_literal_route_disabled() = false;
  real::detail::il_density_last_abandoned()    = false;
  const auto gated {first_span(re, text)};
  EXPECT(!real::detail::il_density_last_abandoned());
  EXPECT(gated.first != real::npos);

  real::detail::inner_literal_route_disabled() = true;
  const auto core {first_span(re, text)};
  real::detail::inner_literal_route_disabled() = false;
  EXPECT(gated == core);
}

// The behaviour the muting introduces, pinned where nothing else would notice it. Without this, the two
// tests above pass on `search` while the filler could start consulting the gate again and no test would
// say so -- which is how the twin above spent a run passing for the wrong reason.
TEST(il_density_gate_is_muted_for_the_batched_filler)
{
  // The SAME density that makes `search` abandon just above (62 per 1000 bytes), but a corpus of
  // MATCHES: the filler emits them in batches, and its candidates complete rather than fail.
  const std::string text {pad_unit("id_42           ", 300000)};
  const real::regex re   {R"((?:\w+)_(?:\w+))"};

  real::detail::inner_literal_route_disabled() = false;
  real::detail::il_density_last_abandoned()    = false;
  const auto gated {spans(re, text)};
  EXPECT(!real::detail::il_density_last_abandoned());
  EXPECT(!gated.empty());

  // And the answers are still the core's, which is what the rest of this file holds for every route.
  real::detail::inner_literal_route_disabled() = true;
  const auto core {spans(re, text)};
  real::detail::inner_literal_route_disabled() = false;
  EXPECT(gated == core);
}

// The floor's PREDICATE, which nothing else pins -- swapping it for `il_warmed` passes every other test
// in this repository.
//
// `il_warmed` means "this regex was candidate-scanned", and the floor's own abandon branch is what sets
// it (before `ensure_immutables`, which is the whole reason that branch sits where it does). Lifting on
// it would therefore let the SECOND short call through and charge it the build the placement exists to
// avoid: ~490 us of UTF-8 machinery for `\w`, against a population of short subjects that needs ~670
// calls to amortise it. `built_for` is the state that actually means "paid" -- it is what
// `ensure_immutables` reads as its own hot path -- so the floor lifts only where the grid measured it.
TEST(il_floor_lifts_only_once_the_build_is_paid)
{
  const real::regex re {R"((?:\w+)_(?:\w+))"};
  // Under BOTH floors on purpose: 256 bytes is below `il_warm_floor` (4 KiB) and below the lazy DFA's
  // own 512-byte minimum, so no other route can reach `ensure_immutables` and build on IL's behalf.
  const std::string subject {failing_then_match(16, 256)};
  for (int i = 0; i < 8; ++i) {
    (void) re.search(subject);
  }
  const auto* const immut {re.raw_program().immut};
  EXPECT(immut != nullptr);
  EXPECT(immut->built_for.load(std::memory_order_acquire) != re.raw_program().code.data());
}

// The OTHER half of the floor, and the one whose absence let an inert patch go green: once the build is
// paid, a short subject must STAY on the route. The check that decides this sits after
// `ensure_immutables`; a change touching only the early exit before it leaves the route unreachable and
// every other test in this file still passes.
//
// Read without REAL_PROFILE: the density gate's own seam is the observation. A short, dense subject that
// reaches the route accumulates the probe's candidates and abandons on DENSITY, so the flag goes true.
// If the floor still bit, the walk would abandon on SIZE at the first candidate and the flag would stay
// false -- which is exactly the inertia this pins against.
TEST(il_floor_lifts_for_short_subjects_once_built)
{
  const real::regex re {R"((?:\w+)_(?:\w+))"};
  // At or above `il_warm_floor`, so this one reaches `ensure_immutables` and sets `built_for` -- whether
  // it then abandons on the cold floor or not is beside the point, the build is what matters here.
  const std::string big {pad_unit("id_42           ", 8192)};
  (void) re.search(big);

  real::detail::inner_literal_route_disabled() = false;
  real::detail::il_density_last_abandoned()    = false;
  const auto span {first_span(re, failing_then_match(16, 256))};
  EXPECT(real::detail::il_density_last_abandoned());
  EXPECT(span.first != real::npos);
}
