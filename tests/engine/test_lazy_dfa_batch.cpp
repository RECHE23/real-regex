// The lazy-DFA route is the fifth batched one, and the only one whose filler may stop with matches
// still ahead: it carries just the anchored-from-candidate sub-scan, it needs `lazy_dfa_min_input`
// bytes of runway, and the shared DFAs may not be built yet. Each of those ends a batch early, and an
// empty batch is how every OTHER filler says "the subject is spent" -- so getting this wrong drops the
// tail of a walk silently, with every existing test still green.
//
// Everything here is a differential against the SAME regex with the route seam pulled: two routing
// paths, one answer. The subjects are deliberately long (the route declines under 512 bytes of runway,
// so a short subject proves nothing about it) and the matches are deliberately placed across the
// runway boundary and at the very end, which is where the partial-batch handover lives.
#include <string>
#include <vector>

#include <sciforge/test/framework.hpp>
#include "real/automata/lazy_dfa.hpp" // lazy_dfa_route_disabled
#include "real/real.hpp"

namespace {

  using spans = std::vector<std::pair<std::size_t, std::size_t>>;

  spans walk(const real::regex& rx,
             std::string_view   t)
  {
    spans v;
    for (const auto& m : rx.find_iter(t)) {
      v.emplace_back(m.start(0), m.end(0));
    }
    return v;
  }

  //! \brief Every span the routed walk finds, and every span it finds with the route pulled.
  std::pair<spans, spans> both_ways(std::string_view pattern,
                                    std::string_view subject)
  {
    const real::regex rx      {pattern};
    const spans       routed  {walk(rx, subject)};
    real::detail::lazy_dfa_route_disabled() = true;
    const real::regex core_rx {pattern};
    const spans       core    {walk(core_rx, subject)};
    real::detail::lazy_dfa_route_disabled() = false;
    return {routed, core};
  }

  //! \brief A subject with `filler` repeated to at least \p len bytes, then \p tail appended verbatim.
  std::string padded(std::string_view filler,
                     std::size_t      len,
                     std::string_view tail)
  {
    std::string s;
    while (s.size() < len) {
      s += filler;
    }
    s += tail;
    return s;
  }
} // namespace

TEST(lazy_dfa_batch_agrees_with_the_unrouted_walk)
{
  // Two branches, no captures, not nullable: the shape the filler exists for. The 8x it recovers was
  // measured on exactly these.
  for (const char* pat : {"[a-z]+|[0-9]+", "[a-z]+|zzzz", "[0-9]+|zzzz", "[a-z]{3,}|[0-9]{2,}"}) {
    const std::string subject {padded("the quick brown fox 42 jumps over 7 lazy dogs ", 4096, "")};
    const auto [routed, core] {both_ways(pat, subject)};
    EXPECT(routed == core);
    EXPECT(!routed.empty());
  }
}

TEST(lazy_dfa_batch_keeps_the_tail_under_the_runway)
{
  // The filler stops once fewer than `lazy_dfa_min_input` bytes remain, with matches still ahead. If
  // the empty batch were read as exhaustion, every match in that last stretch would vanish.
  const std::string subject {padded("xxxxxxxxxxxxxxxx ", 3000, " tail alpha 99 omega beta 7 gamma")};
  const auto [routed, core] {both_ways("[a-z]+|[0-9]+", subject)};
  EXPECT(routed == core);
  // The last match must be in the tail, or this test is not testing the handover.
  EXPECT(!routed.empty());
  EXPECT(routed.back().second > subject.size() - 40);
}

TEST(lazy_dfa_batch_handles_a_match_ending_at_the_subject_end)
{
  const std::string subject {padded("filler filler 1234 ", 2048, "zzzz")};
  const auto [routed, core] {both_ways("[a-z]+|zzzz", subject)};
  EXPECT(routed == core);
  EXPECT(!routed.empty());
  EXPECT(routed.back().second == subject.size());
}

TEST(lazy_dfa_batch_agrees_on_a_subject_with_no_match_at_all)
{
  const std::string subject {padded("---------------- ", 2048, "----")};
  const auto [routed, core] {both_ways("[a-z]+|[0-9]+", subject)};
  EXPECT(routed == core);
  EXPECT(routed.empty());
}

TEST(lazy_dfa_batch_agrees_across_every_enumerating_surface)
{
  // find_iter is what the batched path serves; count_matches and find_all reach the walk through a
  // different specialization, and replace/split are built on find_iter. All five must give one answer.
  const std::string subject  {padded("alpha 12 beta 345 gamma 6 ", 4096, " delta 78")};
  const char*       pat      {"[a-z]+|[0-9]+"};

  const real::regex rx       {pat};
  const std::size_t n_count  {rx.count_matches(subject)};
  const std::size_t n_all    {rx.find_all(subject).size()};
  const std::size_t n_iter   {walk(rx, subject).size()};
  const std::string replaced {rx.replace(subject, "#")};
  const std::size_t n_split  {rx.split(subject).size()};

  real::detail::lazy_dfa_route_disabled() = true;
  const real::regex core_rx    {pat};
  const std::size_t c_count    {core_rx.count_matches(subject)};
  const std::size_t c_all      {core_rx.find_all(subject).size()};
  const std::size_t c_iter     {walk(core_rx, subject).size()};
  const std::string c_replaced {core_rx.replace(subject, "#")};
  const std::size_t c_split    {core_rx.split(subject).size()};
  real::detail::lazy_dfa_route_disabled() = false;

  EXPECT(n_count == c_count);
  EXPECT(n_all == c_all);
  EXPECT(n_iter == c_iter);
  EXPECT(replaced == c_replaced);
  EXPECT(n_split == c_split);
  EXPECT(n_count == n_iter);
  EXPECT(n_all == n_iter);
}

TEST(lazy_dfa_batch_declines_the_shapes_it_must)
{
  // Captures (slot_count > 2), a nullable pattern, an anchored one, a trailing lookaround, and four
  // branches (the Aho-Corasick gate's floor, which is consulted per search inside run()). Each must
  // still answer exactly as the unrouted walk does -- whether it declined for the right reason is what
  // route-surface-parity and test_ac_density_gate pin; here the contract is only that nothing broke.
  const std::string subject {padded("alpha 12 beta 345 gamma 6 zzzz ", 4096, " omega 78")};
  for (const char* pat : {"([a-z]+)|([0-9]+)",
                          "[a-z]*|[0-9]+",
                          "^[a-z]+|[0-9]+",
                          "[a-z]+(?=[0-9])",
                          "alpha|beta|gamma|omega"}) {
    const auto [routed, core] {both_ways(pat, subject)};
    EXPECT(routed == core);
  }
}

TEST(lazy_dfa_batch_agrees_with_captures_populated)
{
  // A capturing pattern declines the batched route, but the walk it falls back to must still fill
  // groups: the partial-batch handover puts a match on the per-match path mid-walk, which is exactly
  // where a half-populated result would show.
  const std::string subject {padded("key=12 other=345 more=6 ", 4096, " last=78")};
  const real::regex rx      {"([a-z]+)=([0-9]+)"};
  std::size_t       seen    {0};
  for (const auto& m : rx.find_iter(subject)) {
    EXPECT(m.size() == 3);
    EXPECT(!m[1].empty());
    EXPECT(!m[2].empty());
    ++seen;
  }
  EXPECT(seen > 100);
}

TEST(lazy_dfa_batch_agrees_with_a_prefix_skip_or_a_rare_discriminant)
{
  // `next_candidate`'s scan STRATEGIES -- a literal prefix skip (`prefix_size`) and the rare-discriminant
  // scan (`rare_disc`) -- were excluded from the arming condition at first, on the mistaken reading that
  // they are routes sitting above this one. They are branches of the very function the filler calls, so
  // the filler gets them for free; excluding them only declined shapes that take this route anyway
  // (`https?://` billed 0.996 engine entries per match, against 0.255 once armed). Both carry
  // per-haystack sticky state, which is what makes a differential worth having here rather than a
  // reading: the state is reset inside `next_candidate`, so a batched walk and a per-match walk must
  // arrive at the same spans across many resumes.
  const std::string subject {padded("visit https://a.io/x then http://b.dev/y and gopher://c ", 6144,
                                    " last https://z.io/end")};
  for (const char* pat : {"https?://", "http[a-z]*://", "[a-z]+://[a-z]"}) {
    const auto [routed, core] {both_ways(pat, subject)};
    EXPECT(routed == core);
    EXPECT(!routed.empty());
  }
}
