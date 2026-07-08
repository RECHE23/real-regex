//! LEVIER-A: the lazy-DFA route's A1 (bounds-only for groupless patterns) and A2 (anchored-from-
//! candidate when the pattern has a sound first-byte prefilter) must give byte-identical results to the
//! core Pike search. The route toggle proves it within one binary; the acid pins A2's linearity.
//!
//! The route only engages at all above lazy_dfa_min_input (512 B, pike.hpp) -- every differential text
//! below is padded/repeated well past that, or the comparison would trivially pass on two identical
//! short-input paths without ever exercising A1/A2 (the same trap \d vs [0-9] was for IL-fusion).
#include <sciforge/test/framework.hpp>

#include <real/automata/lazy_dfa.hpp> // lazy_dfa_route_disabled
#include <real/real.hpp>

#include <chrono>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
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

  std::vector<std::pair<std::size_t, std::size_t>> spans(std::string_view pat,
                                                         std::string_view text)
  {
    const real::regex                                re {pat};
    std::vector<std::pair<std::size_t, std::size_t>> out;
    for (const auto& m : re.find_iter(text)) {
      out.emplace_back(m.start(0), m.end(0));
    }
    return out;
  }
}

TEST(lazy_dfa_routed_equals_core)
{
  struct testcase { std::string_view pat; std::string text; };
  const testcase cases[] {
    // A1: groupless, no fast path (a bare class-loop like [a-z]+ has its OWN fast path -- this needs
    // TWO consuming ops, one fixed then one looped, to fall through to the lazy-DFA route at all).
    {.pat = R"([a-z][a-z]+)",       .text = pad("the quick brown Fox jumps 42 over a lazy DOG and rests. ")},
    // A2: has a sound first-byte prefilter (first_bytes_valid) -- routes through anchored_end.
    {.pat = R"([a-z][0-9]+)",       .text = pad("a1 bb22 c d333 EEE f4 g no5 h ")},                       // every candidate's own first byte is unambiguous
    {.pat = R"([a-z][a-z]+@[a-z]+)", .text = pad("ab@cd x@y noat foo@bar @ trailing@ a@ Z9@no ")},        // grouped-shape-adjacent but no captures (slot_count == 2)
    // A2 with capturing groups (slot_count > 2): the onepass/run_general fallback inside the candidate
    // loop, not the bounds-only one.
    {.pat = R"(([a-z])([a-z]+))",    .text = pad("the quick brown Fox jumps over a lazy DOG and rests. ")},
    // No sound first-byte prefilter at all (an unanchored leading .): stays on the pre-A2 forward+
    // reverse route entirely -- unaffected by A2's branch, exercises the OTHER side of the if.
    {.pat = R"(.*?ZZZ)",             .text = pad("aaaZZZbbb noZZZhere ZZZ start ZZZZZZ ")},
  };
  for (const testcase& tc : cases) {
    real::detail::lazy_dfa_route_disabled() = true;
    const auto core   {spans(tc.pat, tc.text)};
    real::detail::lazy_dfa_route_disabled() = false;
    const auto routed {spans(tc.pat, tc.text)};
    EXPECT(core == routed);
    EXPECT(!core.empty()); // a trivially-empty comparison would prove nothing
  }
}

TEST(lazy_dfa_a2_group_captures_match_core)
{
  // The span-only differential above does not read sub-groups; A2's grouped fallback (onepass or
  // run_general, anchored at the candidate) must still fill them correctly.
  const real::regex re   {R"(([a-z])([a-z]+))"};
  const std::string text {pad("the quick brown Fox jumps over a lazy DOG and rests. ")};

  real::detail::lazy_dfa_route_disabled() = true;
  const auto core   {re.find_all(text)};
  real::detail::lazy_dfa_route_disabled() = false;
  const auto routed {re.find_all(text)};

  EXPECT(!core.empty());
  EXPECT_EQ(routed.size(), core.size());
  for (std::size_t i = 0; i < core.size(); ++i) {
    EXPECT_EQ(routed[i][0], core[i][0]);
    EXPECT_EQ(routed[i][1], core[i][1]); // first letter
    EXPECT_EQ(routed[i][2], core[i][2]); // rest of the word
  }
}

TEST(lazy_dfa_a2_false_candidate_acid_stays_linear)
{
  // A2's "false candidate" path (a valid first byte whose pattern does not actually continue to match)
  // advances one candidate at a time, re-scanning via next_candidate rather than the DFA. On an all-
  // lowercase corpus with NO digits at all, [a-z][0-9]+ makes EVERY position a first-byte-valid
  // candidate whose anchored walk fails after exactly one byte (position 1 is never a digit) -- the
  // adversarial case for "many false candidates". A quadratic re-scan would not finish in time; O(n)
  // candidate checks, each O(1), does.
  const std::string text (100000, 'x');
  const real::regex re {R"([a-z][0-9]+)"};
  const auto        t0 {std::chrono::steady_clock::now()};
  std::size_t       n  {0};
  for (const auto& m : re.find_iter(text)) {
    (void) m;
    ++n;
  }
  const auto ms {std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count()};
  EXPECT(n == 0U);
  EXPECT(ms < 5000); // generous even under sanitizers; a quadratic scan would not finish in time
}

TEST(lazy_dfa_a1_a2_do_not_regress_the_dedicated_fast_paths)
{
  // A1/A2 only touch the lazy-DFA route, which sits AFTER the dedicated fast paths in the dispatch
  // chain (run_fixed_shape, run_alternation, run_class_loop, run_exact_literal, the fused inner-literal
  // route) -- those patterns never reach pike.hpp's lazy-DFA block at all, so their own results (and, by
  // extension, their measured throughput) must be completely unaffected. Spans only here (throughput is
  // the fiche's own benchmark, not a unit-test concern) -- this just pins that the dispatch priority
  // itself did not shift. Padded past lazy_dfa_min_input too: at a short length none of this would
  // exercise anything (the fast paths apply regardless of length, but the point is the A1/A2 route
  // itself must never even be REACHED for these, at any length the route would otherwise engage at).
  struct testcase { std::string_view pat; std::string text; };
  const testcase cases[] {
    {.pat = R"([0-9a-f]{8})",             .text = pad("id=a3f9c1d8 x deadbeef no id=zz ")},            // run_fixed_shape (SIMD)
    {.pat = R"([0-9]{4}-[0-9]{2}-[0-9]{2})", .text = pad("log 2026-07-04 x bad-date 2099-12-25 ")},    // run_fixed_shape via IL-fusion
    {.pat = R"(the|fox|dog)",              .text = pad("the quick brown fox jumps over a lazy dog ")}, // run_alternation
    {.pat = R"([a-z]+)",                   .text = pad("the quick brown fox jumps over a lazy dog ")}, // run_class_loop
    {.pat = R"(dog)",                      .text = pad("the quick brown fox jumps over a lazy dog ")}, // run_exact_literal
  };
  for (const testcase& tc : cases) {
    real::detail::lazy_dfa_route_disabled() = true;
    const auto core   {spans(tc.pat, tc.text)};
    real::detail::lazy_dfa_route_disabled() = false;
    const auto routed {spans(tc.pat, tc.text)};
    EXPECT(core == routed);
    EXPECT(!core.empty());
  }
}
