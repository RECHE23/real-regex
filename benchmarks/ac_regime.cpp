// Aho-Corasick routing: the four subject regimes, AC forced on against AC forced off.
//
// WHY THIS EXISTS. The AC route is gated on `ac_branch_threshold` -- a branch COUNT -- and the
// gate's own documentation records that this selects on the wrong property: AC scans at a flat
// ~3.2 ns/byte whatever the subject, while the memchr cascade it replaces swings by three orders of
// magnitude on the SAME pattern depending only on the haystack. A count cannot predict that. This
// harness is the instrument for the fix: it pins the four regimes so a routing policy can be shown
// to pick the right side of each, rather than argued to.
//
// The regimes, all on one pattern and one subject size, differing only in subject CONTENT:
//   no_match     -- nothing matches, and no candidate even starts. Cascade skips; AC still walks.
//   late_match   -- one match near the end. Cascade skips to it; AC still walks.
//   dense        -- matches everywhere. Both find one immediately; the tie that proves the harness
//                   is measuring routing and not something else.
//   false_starts -- candidate first bytes everywhere, almost none completing. The cascade verifies
//                   and rejects over and over; AC's flat cost finally wins.
//
// Reads as a RATIO (AC / cascade) per regime, which is the number a routing decision is made on:
// > 1 means the AC route loses on that subject, < 1 means it wins.
//
// BUILD: `make ac-regime`, or
//   c++ -std=c++20 -O2 -I include -I benchmarks benchmarks/ac_regime.cpp -o build/ac_regime

#define REAL_BENCH_TIME
#include "measure.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "real/real.hpp"
#include "real/automata/lazy_dfa.hpp"

namespace {

  //! 24 distinct heads: past `small_set`'s 2..8 SIMD block loop, so candidates come from the
  //! `first_bytes` bitmap inside fast_search -- the path the false-start regime actually takes.
  const char* const k_pattern {"alpha|bravo|charlie|delta|echo|foxtrot|golf|hotel|india|juliet|kilo|"
                               "lima|mike|november|oscar|papa|quebec|romeo|sierra|tango|uniform|"
                               "victor|whiskey|xray"};

  constexpr std::size_t k_size {4000};

  //! Nothing matches and no branch head ever appears: the cascade skips the whole subject.
  std::string subject_no_match()
  {
    return std::string(k_size, '_');
  }

  //! One match, near the end: the cascade skips to it, AC walks everything before it.
  std::string subject_late_match()
  {
    std::string s(k_size, '_');
    s.replace(k_size - 10, 5, "tango");
    return s;
  }

  //! Matches everywhere: both routes answer from the first bytes they touch.
  std::string subject_dense()
  {
    std::string s;
    while (s.size() < k_size) {
      s += "tango ";
    }
    s.resize(k_size);
    return s;
  }

  //! Branch HEADS everywhere, whole branches almost never: every candidate is verified and
  //! rejected. This is the regime the cascade degrades in and the one AC exists for.
  std::string subject_false_starts()
  {
    static const char* const heads {"abcdefghijklmnopqrstuvwx"};
    std::string              s;
    s.reserve(k_size + 8);
    std::size_t i {0};
    while (s.size() < k_size) {
      s += heads[i % 24];
      s += '_';
      ++i;
    }
    s.resize(k_size);
    return s;
  }

  //! Wall time of one full find_iter drain, with the AC route forced to the requested side.
  //! \param[out] matches Number of matches the drain produced -- the caller compares it across the
  //!                     two arms before believing any ratio, so a routing difference that changed
  //!                     the ANSWER shows up as a correctness failure rather than a speed number.
  double drain_ns(const real::regex& re, const std::string& subject, bool ac_on, int reps,
                  std::size_t& matches)
  {
    real::detail::aho_corasick_route_disabled() = !ac_on;
    (void) re.search(subject); // warm the per-regex automaton cache; never charged to the timing
    std::size_t  seen {0};
    const double ns {bench::nanos(
      [&] {
        std::size_t n {0};
        for (const auto& m : re.find_iter(subject)) {
          (void) m;
          ++n;
        }
        seen = n;
      },
      reps)};
    real::detail::aho_corasick_route_disabled() = false;
    matches                                     = seen;
    return ns;
  }

} // namespace

int main(int argc, char** argv)
{
  const int reps {argc > 1 ? std::atoi(argv[1]) : 9};

  const real::regex re {k_pattern};

  struct regime
  {
    const char* name;
    std::string subject;
  };

  const std::vector<regime> regimes {{"no_match", subject_no_match()},
                                     {"late_match", subject_late_match()},
                                     {"dense", subject_dense()},
                                     {"false_starts", subject_false_starts()}};

  std::printf("AC routing regimes -- %zu branches, %zu-byte subjects, min of %d\n\n",
              static_cast<std::size_t>(24), k_size, reps);
  std::printf("%-14s %8s %12s %12s %10s  %s\n", "regime", "matches", "AC off (us)", "AC on (us)",
              "AC/casc", "verdict");

  int disagreements {0};
  for (const regime& r : regimes) {
    std::size_t  n_off {0};
    std::size_t  n_on {0};
    const double off {drain_ns(re, r.subject, false, reps, n_off) / 1000.0};
    const double on {drain_ns(re, r.subject, true, reps, n_on) / 1000.0};
    if (n_off != n_on) {
      // Equality first: a route that changes the answer is a bug report, not a benchmark row.
      std::printf("%-14s  MISMATCH: cascade found %zu, AC found %zu -- timings suppressed\n", r.name,
                  n_off, n_on);
      ++disagreements;
      continue;
    }
    const double ratio {off > 0.0 ? on / off : 0.0};
    const char*  verdict {ratio > 1.20   ? "AC LOSES"
                          : ratio < 0.80 ? "AC wins"
                                         : "tie"};
    std::printf("%-14s %8zu %12.2f %12.2f %10.2fx  %s\n", r.name, n_off, off, on, ratio, verdict);
  }
  if (disagreements != 0) {
    std::printf("\n%d regime(s) disagreed on the match count -- fix that before reading any ratio.\n",
                disagreements);
    return 1;
  }

  std::printf("\nA routing policy is correct when it takes AC exactly where the ratio is < 1.\n"
              "Branch count is identical across all four rows above, which is the whole argument\n"
              "against gating on it.\n");
  return 0;
}
