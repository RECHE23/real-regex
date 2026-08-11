// The exact-literal route batched, which is a REOPENED REFUSAL: the filler was written, measured and
// refused once (see pike.hpp's fill_exact_literal_spans for the record and for what reopened it). The
// first attempt was never wrong -- exhaustive-compat was byte-identical and a both-ways differential
// agreed on every span -- so these tests are not what settled it then and are not what settles it now.
// They exist so that the routing change cannot go wrong silently while the performance question is being
// re-decided.
//
// The seam is class_fastpath_disabled, which takes BATCHING out (it gates every route's eligibility in
// decide_batching) without touching the exact-literal route itself in run(): that gate has no such
// check. So the two sides here are batched-walk against per-match-walk on the same route, which is
// exactly the comparison a batch filler needs.
#include <string>
#include <vector>

#include <sciforge/test/framework.hpp>
#include "real/automata/lazy_dfa.hpp" // class_fastpath_disabled
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

  //! \brief The spans of a batched walk, and of the same walk with batching disabled.
  std::pair<spans, spans> both_ways(std::string_view pattern,
                                    std::string_view subject)
  {
    const real::regex rx           {pattern};
    const spans       batched      {walk(rx, subject)};
    real::detail::class_fastpath_disabled() = true;
    const real::regex per_match_rx {pattern};
    const spans       per_match    {walk(per_match_rx, subject)};
    real::detail::class_fastpath_disabled() = false;
    return {batched, per_match};
  }

  std::string repeat_to(std::string_view unit,
                        std::size_t      len)
  {
    std::string s;
    while (s.size() < len) {
      s += unit;
    }
    return s;
  }
} // namespace

TEST(exact_literal_batch_agrees_with_the_per_match_walk)
{
  const std::string subject {repeat_to("the quick brown fox jumps over the lazy dog charlie ", 8192)};
  for (const char* pat : {"charlie", "dog", "the", "quick brown", "zz"}) {
    const auto [batched, per_match] {both_ways(pat, subject)};
    EXPECT(batched == per_match);
  }
}

TEST(exact_literal_batch_handles_the_boundaries_of_the_buffer)
{
  // batch_cap is 4, so a subject with 1, 2, 3, 4, 5 and 9 occurrences crosses the refill boundary in
  // every position: exactly at it, one short, and one past.
  for (const std::size_t count : {std::size_t {1}, std::size_t {2}, std::size_t {3},
                                  std::size_t {4}, std::size_t {5}, std::size_t {9}}) {
    std::string subject {"lead "};
    for (std::size_t i = 0; i < count; ++i) {
      subject += "charlie gap ";
    }
    subject += "tail";
    const auto [batched, per_match] {both_ways("charlie", subject)};
    EXPECT(batched == per_match);
    EXPECT(batched.size() == count);
  }
}

TEST(exact_literal_batch_handles_adjacent_and_overlapping_occurrences)
{
  // Back-to-back occurrences, and a literal whose own bytes overlap ("aa" in "aaaa" must yield the
  // non-overlapping [0,2) and [2,4), not three matches).
  const auto [b1, p1] {both_ways("charlie", "charliecharliecharliecharlie")};
  EXPECT(b1 == p1);
  EXPECT(b1.size() == 4);
  const auto [b2, p2] {both_ways("aa", "aaaaaaaaa")};
  EXPECT(b2 == p2);
  EXPECT(b2.size() == 4); // 9 bytes, non-overlapping pairs
  const auto [b3, p3] {both_ways("aba", "abababa")};
  EXPECT(b3 == p3);
  EXPECT(b3.size() == 2); // [0,3) and [4,7) -- the middle overlap is not a match
}

TEST(exact_literal_batch_handles_the_subject_edges)
{
  const auto [b1, p1] {both_ways("charlie", "charlie")};
  EXPECT(b1 == p1);
  EXPECT(b1.size() == 1);
  const auto [b2, p2] {both_ways("charlie", "xcharlie")};
  EXPECT(b2 == p2);
  const auto [b3, p3] {both_ways("charlie", "charliex")};
  EXPECT(b3 == p3);
  const auto [b4, p4] {both_ways("charlie", "")};
  EXPECT(b4 == p4);
  EXPECT(b4.empty());
  const auto [b5, p5] {both_ways("charlie", "charli")};
  EXPECT(b5 == p5);
  EXPECT(b5.empty());
}

TEST(exact_literal_batch_agrees_across_every_enumerating_surface)
{
  const std::string subject  {repeat_to("alpha charlie beta charlie gamma ", 4096)};
  const char*       pat      {"charlie"};

  const real::regex rx       {pat};
  const std::size_t n_count  {rx.count_matches(subject)};
  const std::size_t n_all    {rx.find_all(subject).size()};
  const std::size_t n_iter   {walk(rx, subject).size()};
  const std::string replaced {rx.replace(subject, "#")};
  const std::size_t n_split  {rx.split(subject).size()};

  real::detail::class_fastpath_disabled() = true;
  const real::regex core_rx    {pat};
  const std::size_t c_count    {core_rx.count_matches(subject)};
  const std::size_t c_all      {core_rx.find_all(subject).size()};
  const std::size_t c_iter     {walk(core_rx, subject).size()};
  const std::string c_replaced {core_rx.replace(subject, "#")};
  const std::size_t c_split    {core_rx.split(subject).size()};
  real::detail::class_fastpath_disabled() = false;

  EXPECT(n_count == c_count);
  EXPECT(n_all == c_all);
  EXPECT(n_iter == c_iter);
  EXPECT(replaced == c_replaced);
  EXPECT(n_split == c_split);
  EXPECT(n_count == n_iter);
  EXPECT(n_all == n_iter);
  EXPECT(n_count > 100);
}

TEST(exact_literal_batch_declines_the_shapes_it_must)
{
  // Each of these keeps the literal fast path in run() but must NOT be batched: the `literal_one_search`
  // hint is false for a capture, for any assertion, for an anchor and for a one-byte literal, and the
  // batched span path applies none of the machinery those need (an assertion can make a given occurrence
  // fail, which is why the per-match route retries the next one). A one-byte literal has its own measured
  // reason to stay off the batched class route, recorded at run_literal_one_search.
  const std::string subject {repeat_to("charlie 12 charlie_ x charlie ", 4096)};
  for (const char* pat : {"(charlie)",
                          "\\bcharlie\\b",
                          "^charlie",
                          "charlie$",
                          "c",
                          "\\Bcharlie"}) {
    const auto [batched, per_match] {both_ways(pat, subject)};
    EXPECT(batched == per_match);
  }
}

TEST(exact_literal_batch_keeps_a_trailing_assertion_correct)
{
  // The shape the differential fuzzer found for the per-match route (`\B2` on "220"): an occurrence at
  // which the assertion fails must be skipped, not reported. It declines the batch, and this pins that
  // declining did not disturb it.
  const real::regex rx  {"\\B2"};
  const auto        got {walk(rx, "220")};
  EXPECT(got.size() == 1);
  EXPECT(got[0].first == 1);
  EXPECT(got[0].second == 2);
}
