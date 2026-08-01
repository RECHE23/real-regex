// AC density gate: the Aho-Corasick route is chosen by candidate density, not by branch count.
//
// The gate replaces a branch-COUNT threshold that could not be right in principle: AC scans at a
// flat ~3.15 ns/byte whatever the subject, while the memchr cascade it replaces runs from 2 us to
// 135 us on the SAME pattern and the same subject length, so only the haystack decides. These tests
// pin both halves of that claim -- that the answers never change, and that the routing actually
// moves -- using the two seams (`aho_corasick_route_disabled`, `ac_density_gate_disabled`).
#include <sciforge/test/framework.hpp>

#include <real/automata/lazy_dfa.hpp>
#include <real/real.hpp>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

  //! 24 branches with 24 distinct heads: past small_set's 2..8 SIMD loop, so candidates come from
  //! the first-bytes bitmap -- the path the density gate samples.
  constexpr std::string_view k_pattern {
    "alpha|bravo|charlie|delta|echo|foxtrot|golf|hotel|india|juliet|kilo|lima|mike|november|oscar|"
    "papa|quebec|romeo|sierra|tango|uniform|victor|whiskey|xray"};

  constexpr std::size_t k_size {4000};

  //! No candidate anywhere: the cascade skips the subject, the automaton would still walk it.
  std::string sparse_subject()
  {
    std::string s(k_size, '_');
    s.replace(k_size - 10, 5, "tango"); // one late match, so the scan is not trivially empty
    return s;
  }

  //! A branch head every other byte, whole branches almost never: the regime the cascade degrades
  //! in and the automaton exists for.
  std::string dense_subject()
  {
    static constexpr std::string_view heads {"abcdefghijklmnopqrstuvwx"};
    std::string                       s;
    s.reserve(k_size + 2);
    for (std::size_t i = 0; s.size() < k_size; ++i) {
      s += heads[i % heads.size()];
      s += '_';
    }
    s.resize(k_size);
    return s;
  }

  //! An `n`-branch alternation of distinct words, and a subject whose candidate heads occur every
  //! `stride` bytes without ever completing a branch.
  std::string alternation_of(std::size_t n)
  {
    static constexpr std::string_view words[] {"alpha", "bravo", "charlie", "delta", "echo",
                                               "foxtrot", "golf", "hotel", "india", "juliet",
                                               "kilo", "lima"};
    std::string out;
    for (std::size_t i = 0; i < n; ++i) {
      if (i != 0) {
        out += '|';
      }
      out += words[i % (sizeof(words) / sizeof(words[0]))];
    }
    return out;
  }

  std::string heads_every(std::size_t n,
                          std::size_t stride)
  {
    static constexpr std::string_view heads {"abcdefghijkl"};
    std::string                       s(k_size, '_');
    for (std::size_t i = stride, k = 0; i < k_size; i += stride, ++k) {
      s[i] = heads[k % n];
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

  //! The verdict as a name. EXPECT_EQ on the enum reports "<unprintable>" on both sides, which is
  //! the least useful thing a routing test can say when it fails.
  std::string_view verdict_name()
  {
    switch (real::detail::ac_density_last_verdict()) {
      case real::detail::ac_verdict::cascade:   return "cascade";
      case real::detail::ac_verdict::automaton: return "automaton";
      default:                                  return "not_consulted";
    }
  }

  //! Sets both seams for the duration of a scope and restores them, so a failing EXPECT cannot
  //! leave the process-wide flags set for every test that runs after it.
  struct seam_scope
  {
    seam_scope(bool route_off,
               bool gate_off)
    {
      real::detail::aho_corasick_route_disabled() = route_off;
      real::detail::ac_density_gate_disabled()    = gate_off;
    }

    seam_scope(const seam_scope&)            = delete;
    seam_scope& operator=(const seam_scope&) = delete;
    seam_scope(seam_scope&&)                 = delete;
    seam_scope& operator=(seam_scope&&)      = delete;
    ~seam_scope()
    {
      real::detail::aho_corasick_route_disabled() = false;
      real::detail::ac_density_gate_disabled()    = false;
    }
  };

  //! Best-of-N nanoseconds for one full drain -- the minimum, which is the run least contaminated
  //! by interference on a machine that is not quiet (a CI runner is not).
  template <typename F>
  double best_ns(F&& f,
                 int reps)
  {
    double best {-1.0};
    for (int r = 0; r < reps; ++r) {
      const auto   t0 {std::chrono::steady_clock::now()};
      f();
      const auto   t1 {std::chrono::steady_clock::now()};
      const double ns {std::chrono::duration<double, std::nano>(t1 - t0).count()};
      if (best < 0.0 || ns < best) {
        best = ns;
      }
    }
    return best;
  }
} // namespace

// Semantic transparency: routing decides only speed. Every combination of the two seams must
// produce byte-identical spans on both regimes -- this is the net that lets the gate be tuned at
// all, and it is the same contract test_il_density_gate holds the inner-literal gate to.
TEST(ac_density_gate_never_changes_the_answer)
{
  const real::regex re     {k_pattern};
  const std::string sparse {sparse_subject()};
  const std::string dense  {dense_subject()};

  for (const std::string* subject : {&sparse, &dense}) {
    std::vector<std::pair<std::size_t, std::size_t>> reference;
    {
      const seam_scope seam {true, true}; // no automaton at all: the pure cascade
      reference = spans(re, *subject);
    }
    for (const bool route_off : {false, true}) {
      for (const bool gate_off : {false, true}) {
        const seam_scope seam {route_off, gate_off};
        EXPECT_EQ(spans(re, *subject), reference);
      }
    }
  }
}

// The routing decision itself, asserted directly rather than through a stopwatch. A sparse subject
// must keep the search on the cascade: the automaton would walk all 4000 bytes to reach one match
// near the end, which measures 5.9x slower optimised -- but that RATIO is 1.4x under ASan/UBSan,
// because sanitizer overhead is additive per operation and dilutes the advantage of a route whose
// merit is skipping bytes. A ratio assertion calibrated on one build configuration turned the
// sanitize leg red while the engine was correct; the verdict seam has no such dependence.
TEST(ac_density_gate_keeps_a_sparse_subject_off_the_automaton)
{
  const real::regex re     {k_pattern};
  const std::string sparse {sparse_subject()};

  const seam_scope seam    {false, false}; // both seams live: the gate decides
  real::detail::ac_density_last_verdict() = real::detail::ac_verdict::not_consulted;
  (void) spans(re, sparse);
  EXPECT_EQ(verdict_name(), "cascade");
}

// And the converse, without which the test above would pass on a gate that simply never routes to
// the automaton: on a candidate-dense subject it must still be chosen, where the cascade costs ~10x
// more. The pair is what makes either one mean anything.
TEST(ac_density_gate_still_takes_the_automaton_when_candidates_are_dense)
{
  const real::regex re    {k_pattern};
  const std::string dense {dense_subject()};

  const seam_scope seam   {false, false};
  real::detail::ac_density_last_verdict() = real::detail::ac_verdict::not_consulted;
  (void) spans(re, dense);
  EXPECT_EQ(verdict_name(), "automaton");
}

// Below twelve branches the automaton was never taken at all, so the gate's second threshold applies
// there and it is the measured MAXIMUM rather than the minimum: switching early is a regression
// against what ships, where above twelve it could not be. These three pin the three outcomes that
// distinction produces.
TEST(ac_density_gate_takes_the_automaton_below_twelve_branches_when_dense_enough)
{
  const real::regex re    {alternation_of(8)};
  const std::string dense {heads_every(8, 2)}; // ~500 candidates per 1000 bytes, far past 1400/8

  const seam_scope seam   {false, false};
  real::detail::ac_density_last_verdict() = real::detail::ac_verdict::not_consulted;
  (void) spans(re, dense);
  EXPECT_EQ(verdict_name(), "automaton");
}

// The same eight branches on a subject BETWEEN the two thresholds must stay on the cascade. One head
// every 10 bytes is 100 per 1000, so the product is 800: past the 550 that applies at twelve branches
// and above, short of the 1400 that applies below it. A subject further out would pass against
// EITHER constant and so would test nothing -- 800 fails if the low region ever adopts the high
// region's number.
TEST(ac_density_gate_stays_on_the_cascade_below_twelve_when_not_dense_enough)
{
  const real::regex re     {alternation_of(8)};
  const std::string sparse {heads_every(8, 10)};

  const seam_scope seam    {false, false};
  real::detail::ac_density_last_verdict() = real::detail::ac_verdict::not_consulted;
  (void) spans(re, sparse);
  EXPECT_EQ(verdict_name(), "cascade");
}

// A three-branch alternation must never reach the automaton, however dense the subject: below four
// branches nothing has been measured, and an unmeasured domain is not one to route into.
//
// This pins the OUTCOME and deliberately does not claim the mechanism. Lowering `ac_branch_floor` to
// 2 leaves the verdict at `not_consulted`, so something upstream of the floor already excludes a
// three-branch alternation and the floor is not what does the work here. Found by sabotaging the
// constant and watching this test NOT fail -- which is the only reason the first version of this
// comment, which did claim the mechanism, was caught. What that upstream condition is remains open.
TEST(ac_density_gate_never_routes_a_three_branch_alternation_to_the_automaton)
{
  const real::regex re    {alternation_of(3)};
  const std::string dense {heads_every(3, 2)};

  const seam_scope seam   {false, false};
  real::detail::ac_density_last_verdict() = real::detail::ac_verdict::not_consulted;
  (void) spans(re, dense);
  EXPECT(verdict_name() != "automaton");
}
