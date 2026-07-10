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

  std::string corpus_all_a(std::size_t n)
  {
    // {n, 'a'} prefers the initializer_list<char> overload and fails to narrow n (std::size_t) to char;
    // the (count, char) constructor needs the explicit call.
    return std::string(n, 'a'); // NOLINT(modernize-return-braced-init-list)
  }

  std::string corpus_xy(std::size_t n)
  {
    std::string s;
    s.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
      s += (i % 2 == 0) ? 'x' : 'y';
    }
    return s;
  }

  // Deterministic O(n) vs O(n^2) check for a lazy-DFA route pattern, via the compile-gated work
  // counter (REAL_TEST_INSTRUMENT) rather than wall-clock -- literal_prefilter_throughput_smoke's exact
  // method (tests/engine/test_prefilter.cpp), applied to the A2 unbounded-reach fix. `make_corpus`
  // builds the adversarial no-match haystack for `pat` (dense in the pattern's first-byte set, no
  // terminator anywhere).
  void expect_search_is_linear(std::string_view pat,
                               std::string    (*make_corpus)(std::size_t))
  {
    const real::regex  rx {pat};
    const auto         work {[&](std::size_t n) -> std::uint64_t {
                               const std::string text {make_corpus(n)};
                               real::detail::prefilter_work_units() = 0;
                               EXPECT(!rx.search(text).matched());
                               return real::detail::prefilter_work_units();
                             }};
    (void) work(1 << 10);                    // warmup (first-call path setup); discarded
    const std::uint64_t small {work(16384)};
    const std::uint64_t large {work(32768)}; // 2x the bytes
    // O(n) -> ~2x; O(n^2) -> ~4x. 3x bites quadratic, absorbs constant per-search overhead.
    EXPECT(large < small * 3);
    // Determinism pin: re-run large -- same work count (not wall time).
    EXPECT_EQ(work(32768), large);
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

TEST(lazy_dfa_a2_unbounded_reach_equals_core)
{
  // FIX P0 (O(n^2)): a first-byte-valid pattern whose reach past that byte is unbounded (`.*`, `.+`, a
  // wide-class `*`/`+`) used to make A2's candidate loop re-scan to the end of the haystack for every
  // dense candidate -- `a.*b` on a run of 'a' with no 'b' is O(n^2). The fix changes ROUTE (A2 hands off
  // to forward_end once a candidate's own miss proves the reach unbounded), never RESULT: prove it
  // against the route-disabled core across match/no-match/greedy-vs-lazy/boundary/empty corpora.
  struct testcase { std::string_view pat; std::string text; };
  const std::string all_a    (2000, 'a');                                                   // no 'b': no-match, the exact bug shape
  const std::string a_then_b (std::string(1999, 'a') + "b");                                // match right at the end -- reach truly unbounded until then
  const std::string two_b    (std::string(900, 'a') + "b" + std::string(900, 'a') + "b");   // two candidate ends: greedy/lazy must disagree correctly
  const testcase    cases[] {
    {.pat = R"(a.*b)",       .text = all_a},
    {.pat = R"(a.*b)",       .text = a_then_b},
    {.pat = R"(a.*b)",       .text = two_b},         // greedy: must reach the LAST b
    {.pat = R"(a.*?b)",      .text = two_b},         // lazy: must reach the FIRST b
    {.pat = R"(a.*b)",       .text = pad("the quick brown fox jumps over a lazy dog near the bench ")},
    {.pat = R"(a.*b)",       .text = std::string()}, // empty haystack
    {.pat = R"(.*x)",        .text = all_a},         // no sound first-byte prefilter at all -- pre-existing route, must stay unaffected
    {.pat = R"((?:a|c).*z)", .text = all_a},         // a first-byte SET (not one literal byte), still unbounded reach
  };
  for (const testcase& tc : cases) {
    real::detail::lazy_dfa_route_disabled() = true;
    const auto core   {spans(tc.pat, tc.text)};
    real::detail::lazy_dfa_route_disabled() = false;
    const auto routed {spans(tc.pat, tc.text)};
    EXPECT(core == routed);
  }
}

TEST(lazy_dfa_a2_unbounded_reach_scales_linearly)
{
  // THE gate for the O(n^2) fix itself: total search work must grow linearly with the haystack on the
  // adversarial corpus for each pattern shape named in the fix's own fiche.
  expect_search_is_linear(R"(a.*b)", corpus_all_a);
  expect_search_is_linear(R"(a.+b)", corpus_all_a);
  expect_search_is_linear(R"(.*needle)", corpus_all_a); // no first-byte prefilter -- forward_end from the start, pre-existing O(n)
  // \w+.*\w+ dropped from the fiche's battery: it is a false adversarial case, not a test bug fix --
  // any text with 2+ word characters ANYWHERE matches it trivially (.* bridges any gap), so a large
  // no-match corpus for it does not exist. \w+.*x keeps the \w+-prefixed, unbounded-.*-reach shape
  // while staying genuinely unmatched (no literal terminator in the corpus).
  expect_search_is_linear(R"(\w+.*x)", corpus_all_a);
  expect_search_is_linear(R"((x|y)*z)", corpus_xy);
}
