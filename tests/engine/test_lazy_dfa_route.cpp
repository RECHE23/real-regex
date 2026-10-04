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

#include <array>
#include <chrono>
#include <cstdio>
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
                               real::detail::tally(real::detail::counter::prefilter_work_units) = 0;
                               EXPECT(!rx.search(text).matched());
                               return real::detail::tally(real::detail::counter::prefilter_work_units);
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
  // a benchmark concern, not a unit-test one) -- this just pins that the dispatch priority
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
  // adversarial corpus for each pattern shape named in the fix review.
  expect_search_is_linear(R"(a.*b)", corpus_all_a);
  expect_search_is_linear(R"(a.+b)", corpus_all_a);
  expect_search_is_linear(R"(.*needle)", corpus_all_a); // no first-byte prefilter -- forward_end from the start, pre-existing O(n)
  // \w+.*\w+ dropped from the battery: it is a false adversarial case, not a test bug fix --
  // any text with 2+ word characters ANYWHERE matches it trivially (.* bridges any gap), so a large
  // no-match corpus for it does not exist. \w+.*x keeps the \w+-prefixed, unbounded-.*-reach shape
  // while staying genuinely unmatched (no literal terminator in the corpus).
  expect_search_is_linear(R"(\w+.*x)", corpus_all_a);
  expect_search_is_linear(R"((x|y)*z)", corpus_xy);
}

// Inside a long run of candidate bytes that no match ends, each anchored walk from a candidate rereads the
// run the previous one crossed. The walks give way to one forward pass and one reverse once the walks that
// found nothing cost clearly more than that pass would (`anchored_walk_bill`). No answer changes and the cost
// stays linear, so only a comparison sees it: the reference is the bare forward pass over the same text,
// built from the same program in this binary, so machine speed cancels.
//
// Measured on 200 KB (best of seven, 2026-09-26, arm64): with the bill both queries run at 1.04x and 1.02x the
// bare pass; with the walks kept, count_matches ran at 18.7x and search at 9.4x. The bound sits at 3x.
TEST(lazy_dfa_anchored_walks_give_way_to_one_pass)
{
  using clock_type = std::chrono::steady_clock;
  constexpr double ratio_bound {3.0};
  std::string      text;
  while (text.size() < 200000U) {
    text += "the quick fox singing 123x and bringing 7x over 42 dogs ";
  }
  // Best of seven rounds, the runs alternating within each round: a clock that speeds up part-way through (a
  // shared runner leaving a quiet period) then weighs on every run alike, not on whichever was timed first.
  const auto best_ns {[](const auto&... runs) {
                        std::array<double, sizeof...(runs)> best {};
                        best.fill(-1.0);
                        const auto one {[](const auto& run, double& slot) {
                                          const auto   t0 {clock_type::now()};
                                          run();
                                          const double ns {std::chrono::duration<double, std::nano>(clock_type::now() - t0).count()};
                                          slot = (slot < 0.0 || ns < slot) ? ns : slot;
                                        }};
                        for (int r {0}; r < 7; ++r) {
                          std::size_t k {0};
                          (one(runs, best[k++]), ...);
                        }
                        return best;
                      }};
  const std::string_view       pattern  {R"([a-z ]*x\d\d\d\d)"};
  const auto                   compiled {real::detail::dynamic_storage::compile(pattern, real::flags::none)};
  const auto                   pv       {compiled.view()};
  const auto                   bp       {real::detail::build_byte_program(pv)};
  real::detail::lazy_dfa       fwd(bp.code, bp.classes);
  const real::regex            re       {std::string {pattern}};
  std::size_t                  sink     {0};
  EXPECT(fwd.eligible());
  const auto times                      {best_ns([&] {
                                                   fwd.begin_scan();
                                                   sink += fwd.forward_end(text, 0);
                                                 },
                                                 [&] { sink += re.count_matches(text); }, [&] { sink += re.search(text) ? 1U : 0U; })};
  const double pass   {times[0]};
  const double counts {times[1]};
  const double finds  {times[2]};
  std::printf("  %s: count_matches %.2fx, search %.2fx the bare forward pass\n", std::string {pattern}.c_str(),
              counts / pass, finds / pass);
  EXPECT(counts < ratio_bound * pass);
  EXPECT(finds < ratio_bound * pass);
  EXPECT(sink != 0U);
}

namespace {

  //! Feeds \ref real::detail::anchored_walk_bill a walk that finds nothing every \p every bytes, each reading
  //! \p length bytes, over \p span bytes; returns the byte crossed when it first gives way, or npos.
  std::size_t bill_gives_way_at(std::size_t every,
                                std::size_t length,
                                std::size_t span)
  {
    real::detail::anchored_walk_bill bill {};
    for (std::size_t crossed {0}; crossed < span; crossed += every) {
      if (bill.overspent(length, crossed)) {
        return crossed;
      }
    }
    return real::npos;
  }
} // namespace

// The verdicts at the shapes the bill was measured on. Walks that find nothing on most candidates give way
// early, whether they are short (`[a-z]\d` on prose: one every 1.4 bytes, reading 2) or long
// (`[a-z ]*x\d\d\d\d`: one every 1.1 bytes, reading 12); sparse candidates keep their walks over a whole
// megabyte (`q[a-z]*z`, `(?a)\bfox\b|\bdog\b`: one every 28 bytes or more, reading 1 or 2), where the walks
// cost a tenth of the pass. Without the price of a walk, the short case never gives way; with a verdict
// that always gives way, the sparse one loses the walks.
TEST(anchored_walk_bill_verdicts)
{
  constexpr std::size_t megabyte {std::size_t {1} << 20U};
  EXPECT(bill_gives_way_at(2, 2, megabyte) < 256U);          // dense short walks, which read less than the bound alone
  EXPECT(bill_gives_way_at(1, 12, megabyte) < 256U);         // dense long walks
  EXPECT(bill_gives_way_at(8, 24, megabyte) < 4096U);        // sparser walks that reread three bytes per byte
  EXPECT_EQ(bill_gives_way_at(28, 2, megabyte), real::npos); // sparse candidates keep their walks
  EXPECT_EQ(bill_gives_way_at(12, 3, megabyte), real::npos); // at six tenths of the bound
}

// Once the walks give way, the span filler that feeds count_matches and find_iter carries on in the single
// pass itself. Handing the rest back to the per-match route instead walks the same candidates again after
// every match, which cost count_matches 1.2x to 1.9x on dense matches (`\w+\d+` on prose: 134 M against
// 248 M instructions over 2 MB). On `\w+\d+` the walks give way before the first match, so a filler that
// hands back returns no span at all.
TEST(lazy_dfa_span_filler_keeps_the_single_pass)
{
  std::string text;
  while (text.size() < 4096U) {
    text += "the quick fox singing 123x and bringing 7x over 42 dogs ";
  }
  const real::regex                                re {R"(\w+\d+)"};
  std::vector<std::pair<std::size_t, std::size_t>> expected;
  for (const auto& m : re.find_iter(text)) {
    expected.emplace_back(m.start(), m.end());
    if (expected.size() == 4U) {
      break;
    }
  }
  real::detail::dynamic_storage::state_type                                       state;
  const real::detail::program_view                                                prog    {re.raw_program()}; // the VM holds it by reference
  real::detail::pike_vm<real::detail::dynamic_storage::state_type, true>          vm      {prog, state};
  real::detail::pike_vm<real::detail::dynamic_storage::state_type, true>::cp_span out[4]  {};
  bool                                                                            partial {false};
  const std::size_t                                                               n       {vm.fill_lazy_dfa_spans(text, 0, out, 4, partial)};
  EXPECT_EQ(n, 4U);
  for (std::size_t i {0}; i < n && i < expected.size(); ++i) {
    EXPECT_EQ(out[i].start, expected[i].first);
    EXPECT_EQ(out[i].end, expected[i].second);
  }
}

// A pattern whose DFA needs a state per window of the subject thrashes the cache: every step builds a state.
// The scan then quits and the VM finishes the search, instead of building states for the whole subject at
// four times the VM's cost. The reference is the same regex with the DFAs taken out by their knob.
//
// Measured on 1 MB of random a/b (best of three, 2026-09-26, arm64): 0.9x the VM with the quit, 3.8x without
// it. The bound sits at 1.8x.
TEST(lazy_dfa_thrash_hands_the_search_to_the_vm)
{
  using clock_type = std::chrono::steady_clock;
  std::string   text;
  std::uint32_t bits {0x2545F491U};
  while (text.size() < 1000000U) {
    bits ^= bits << 13U;
    bits ^= bits >> 17U;
    bits ^= bits << 5U;
    text += ((bits & 1U) != 0U) ? 'a' : 'b';
  }
  // Best of three rounds, the two runs alternating within each: a clock that speeds up part-way through then
  // weighs on both alike, not on whichever was timed first.
  const auto best_ns {[](const auto& first, const auto& second) {
                        std::array<double, 2> best {-1.0, -1.0};
                        const auto            one {[](const auto& run, double& slot) {
                                                     const auto   t0 {clock_type::now()};
                                                     run();
                                                     const double ns {std::chrono::duration<double, std::nano>(clock_type::now() - t0).count()};
                                                     slot = (slot < 0.0 || ns < slot) ? ns : slot;
                                                   }};
                        for (int r {0}; r < 3; ++r) {
                          one(first, best[0]);
                          one(second, best[1]);
                        }
                        return best;
                      }};
  const real::regex re       {"(a|b)*a(a|b){12}c"};
  std::size_t       routed_n {0};
  std::size_t       vm_n     {0};
  const auto        times    {best_ns([&] { routed_n = re.count_matches(text); },
                                      [&] {
                                        real::detail::lazy_dfa_route_disabled() = true;
                                        vm_n                                    = re.count_matches(text);
                                        real::detail::lazy_dfa_route_disabled() = false;
                                      })};
  const double      routed   {times[0]};
  const double      vm       {times[1]};
  std::printf("  (a|b)*a(a|b){12}c: %.2fx the VM\n", routed / vm);
  EXPECT_EQ(routed_n, vm_n);
  EXPECT(routed < 1.8 * vm);
}

// After an empty match the next may not be empty at the same spot. That rule binds one position: the VM
// decides whether a non-empty match starts there, and past it the search takes the DFAs. Every enumeration of
// a pattern that can match empty, in text and byte mode, over subjects with multi-byte code points (the next
// boundary is a code point away) and a final newline, against the same regex with the DFAs taken out.
TEST(lazy_dfa_route_after_an_empty_match)
{
  static constexpr std::string_view patterns[] {".*", "x*", "[^\"]*", "(?:)|a", "a??", "\\w*", "(a|)", "(?m)^", "$", "\\B",
                                                "\xC3\xA9*", "(?:ab)*", "a*?", "\\b", "(?m)$", "[a-z]*\\d*", "(?:x|\xC3\xA9)*"};
  static constexpr std::string_view units[]    {"xxab a\n", "\xC3\xA9x\xC3\xA9 \"ab\"\n", "a\xE6\x97\xA5xx\n\n", "abab  x9"};
  int                               compared   {0};
  for (const std::string_view pattern : patterns) {
    for (const real::flags fl : {real::flags::none, real::flags::bytes}) {
      const real::regex re {std::string {pattern}, fl};
      for (const std::string_view unit : units) {
        std::string text;
        while (text.size() < 700U) {
          text += unit;
        }
        const auto enumerate {[&] {
                                std::string out {std::to_string(re.count_matches(text)) + ":"};
                                std::size_t found {0};
                                for (const auto& m : re.find_iter(text)) {
                                  if (++found > text.size() + 1U) {
                                    out += " (runaway)"; // an empty match yielded twice at one spot
                                    break;
                                  }
                                  out += " " + std::to_string(m.start()) + "-" + std::to_string(m.end());
                                }
                                return out;
                              }};
        const std::string routed {enumerate()};
        real::detail::lazy_dfa_route_disabled() = true;
        const std::string vm     {enumerate()};
        real::detail::lazy_dfa_route_disabled() = false;
        EXPECT_EQ(routed, vm);
        ++compared;
      }
    }
  }
  EXPECT(compared > 100);
}
