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

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "real/real.hpp"
#include "real/automata/lazy_dfa.hpp"

namespace {

  //! The 24 branch words, all with DISTINCT heads (a..x). Which prefix of them is used is a runtime
  //! argument, because the crossover this harness exists to find MOVES WITH BRANCH COUNT and the
  //! documented table only ever covered 12..24 -- while the gate applies its threshold from four
  //! branches up. Measured below twelve, that extrapolation does not hold: on a prose corpus the gate
  //! switches to the automaton between 8 and 9 branches and the automaton is 1.9x SLOWER there
  //! (3.56 ns/B against the cascade's 1.87), which is the counter-example this parameterisation exists
  //! to turn into a table.
  //!
  //! Eight is also where the cascade changes scan: 2..8 distinct heads take `small_set`'s SIMD block
  //! loop, past that the `first_bytes` bitmap inside fast_search. A sweep that stops at twelve cannot
  //! see either side of that step.
  const char* const k_words[] {"alpha", "bravo", "charlie", "delta", "echo", "foxtrot", "golf",
                               "hotel", "india", "juliet", "kilo", "lima", "mike", "november",
                               "oscar", "papa", "quebec", "romeo", "sierra", "tango", "uniform",
                               "victor", "whiskey", "xray"};
  constexpr std::size_t k_max_branches {24};

  //! \brief The first \p n words joined by `|`.
  //! \param[in] n How many branches to use (2 .. \ref k_max_branches).
  //! \return The alternation pattern.
  std::string pattern_for(std::size_t n)
  {
    std::string p;
    for (std::size_t i = 0; i < n; ++i) {
      if (i != 0) {
        p += '|';
      }
      p += k_words[i];
    }
    return p;
  }

  //! \brief The heads of the first \p n words — the only bytes that are candidates for that pattern.
  //! \param[in] n How many branches are in play.
  //! \return A string of \p n distinct head bytes.
  std::string heads_for(std::size_t n)
  {
    std::string h;
    for (std::size_t i = 0; i < n; ++i) {
      h += k_words[i][0];
    }
    return h;
  }

  constexpr std::size_t k_size {4000};

  //! Nothing matches and no branch head ever appears: the cascade skips the whole subject.
  std::string subject_no_match()
  {
    return std::string(k_size, '_');
  }

  //! One match, near the end: the cascade skips to it, AC walks everything before it.
  //! \param[in] word The branch to plant — the FIRST word, so the subject is valid at every branch
  //!                  count (`tango` is only a branch from twenty up, and planting it below that
  //!                  silently turned this regime into a second no_match).
  //! \return The subject.
  std::string subject_late_match(const char* word)
  {
    std::string s(k_size, '_');
    const std::size_t len {std::string(word).size()};
    s.replace(k_size - len - 5, len, word);
    return s;
  }

  //! Matches everywhere: both routes answer from the first bytes they touch.
  //! \param[in] word The branch to repeat; see \ref subject_late_match for why it is a parameter.
  //! \return The subject.
  std::string subject_dense(const char* word)
  {
    std::string s;
    while (s.size() < k_size) {
      s += word;
      s += ' ';
    }
    s.resize(k_size);
    return s;
  }

  //! Branch HEADS everywhere, whole branches almost never: every candidate is verified and
  //! rejected. This is the regime the cascade degrades in and the one AC exists for.
  //! \param[in] heads The head bytes of the branches in play; only those are candidates, so a fixed
  //!                   24-head string plants non-candidates below 24 branches and understates the
  //!                   regime this function exists to create.
  //! \return The subject.
  std::string subject_false_starts(const std::string& heads)
  {
    std::string s;
    s.reserve(k_size + 8);
    std::size_t i {0};
    while (s.size() < k_size) {
      s += heads[i % heads.size()];
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
    // Force the arm, both halves of it: the route flag alone only declines to FORBID the
    // automaton -- the density gate would then re-decide, and this column would measure the gate
    // rather than the automaton it claims to measure.
    real::detail::aho_corasick_route_disabled() = !ac_on;
    real::detail::ac_density_gate_disabled()    = true;
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
    real::detail::ac_density_gate_disabled()    = false;
    matches                                     = seen;
    return ns;
  }

} // namespace

int main(int argc, char** argv)
{
  const int reps {argc > 1 ? std::atoi(argv[1]) : 9};

  const std::size_t branches {argc > 2 ? std::min<std::size_t>(std::max<std::size_t>(
                                          static_cast<std::size_t>(std::atoi(argv[2])), 2U),
                                        k_max_branches)
                                      : k_max_branches};
  const std::string pattern {pattern_for(branches)};
  const std::string heads {heads_for(branches)};
  const real::regex re {pattern};

  struct regime
  {
    const char* name;
    std::string subject;
  };

  const std::vector<regime> regimes {{"no_match", subject_no_match()},
                                     {"late_match", subject_late_match(k_words[0])},
                                     {"dense", subject_dense(k_words[0])},
                                     {"false_starts", subject_false_starts(heads)}};

  std::printf("AC routing regimes -- %zu branches, %zu-byte subjects, min of %d\n\n",
              branches, k_size, reps);
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

  // --- crossover sweep ----------------------------------------------------------------------
  //
  // The four regimes above bracket the answer but do not locate it: their candidate densities are
  // 0, ~0.25, 166 and 500 per 1000 bytes, so the crossover is somewhere inside a 600x gap. A
  // routing threshold picked from those four rows would be a guess dressed as a measurement. This
  // sweep plants FALSE-START heads at a controlled rate -- candidate first bytes that never
  // complete a branch, which is the axis the cascade degrades along -- and reports where AC starts
  // winning.
  std::printf("\n\ncandidate-density sweep (false starts only, no matches)\n\n");
  std::printf("%10s %10s %12s %12s %10s  %s\n", "1 head/N", "cand/1000B", "AC off (us)",
              "AC on (us)", "AC/casc", "");
  for (const std::size_t stride : {2000U, 400U, 100U, 60U, 50U, 45U, 40U, 36U, 32U, 28U, 25U, 20U, 12U, 6U, 2U}) {
    std::string s(k_size, '_');
    std::size_t planted {0};
    for (std::size_t i = stride; i < k_size; i += stride) {
      s[i] = heads[planted % heads.size()]; // a head, but the bytes after it never complete the branch
      ++planted;
    }
    const double milli {static_cast<double>(planted) * 1000.0 / static_cast<double>(k_size)};
    std::size_t  n_off {0};
    std::size_t  n_on {0};
    const double off {drain_ns(re, s, false, reps, n_off) / 1000.0};
    const double on {drain_ns(re, s, true, reps, n_on) / 1000.0};
    if (n_off != n_on || n_off != 0) {
      std::printf("%10zu  MISMATCH or unexpected match (%zu/%zu) -- sweep row invalid\n", stride,
                  n_off, n_on);
      continue;
    }
    const double ratio {off > 0.0 ? on / off : 0.0};
    std::printf("%10zu %10.1f %12.2f %12.2f %10.2fx  %s\n", stride, milli, off, on, ratio,
                ratio < 1.0 ? "<-- AC ahead" : "");
  }
  std::printf("\nThe threshold belongs where the ratio crosses 1, read off this table on BOTH\n"
              "platforms -- not interpolated from the four regimes above.\n");

  // MATCHING SUBJECTS, which is the gap the sweep above leaves. Every row of it plants heads that
  // never complete a branch, so it measures the cascade at its WORST: verify, reject, repeat. The
  // gate, though, serves subjects that match -- and there the cascade finds its answer and stops
  // while the automaton keeps walking, so the cascade is better than the match-free table says and
  // the true crossover sits higher than that table reports.
  //
  // The counter-example that made this worth measuring: a 9-branch alternation over ordinary prose
  // has a candidate product of ~1521, just past the 1400 the low region applies, so the gate takes
  // the automaton -- which then reads 3.56 ns/B against the cascade's 1.87, a 1.9x LOSS on the side
  // of the threshold that is supposed to be a win.
  //
  // This sweep plants COMPLETE branches at a controlled stride, so its axis is match density rather
  // than false-start density, and the two tables together bracket what a real subject does.
  std::printf("\n\nmatch-density sweep (real matches, no false starts)\n\n");
  std::printf("%10s %10s %12s %12s %10s  %s\n", "1 match/N", "match/1000B", "AC off (us)",
              "AC on (us)", "AC/casc", "");
  const std::string word {k_words[0]};
  for (const std::size_t stride : {2000U, 400U, 200U, 100U, 60U, 40U, 30U, 20U, 14U, 10U, 8U}) {
    if (stride <= word.size()) {
      continue;
    }
    std::string s(k_size, '_');
    std::size_t planted {0};
    for (std::size_t i = stride; i + word.size() < k_size; i += stride) {
      s.replace(i, word.size(), word);
      ++planted;
    }
    const double milli {static_cast<double>(planted) * 1000.0 / static_cast<double>(k_size)};
    std::size_t  n_off {0};
    std::size_t  n_on {0};
    const double off {drain_ns(re, s, false, reps, n_off) / 1000.0};
    const double on {drain_ns(re, s, true, reps, n_on) / 1000.0};
    if (n_off != n_on || n_off != planted) {
      std::printf("%10zu  MISMATCH (%zu/%zu, planted %zu) -- sweep row invalid\n", stride, n_off,
                  n_on, planted);
      continue;
    }
    const double ratio {off > 0.0 ? on / off : 0.0};
    std::printf("%10zu %10.1f %12.2f %12.2f %10.2fx  %s\n", stride, milli, off, on, ratio,
                ratio < 1.0 ? "<-- AC ahead" : "");
  }
  std::printf("\nA subject that MATCHES is the one the gate actually serves. Where this table's\n"
              "crossover sits above the false-start table's, the calibration taken from that one\n"
              "switches to the automaton too early by exactly that margin.\n");

  // THE EXPERIMENT THAT DECIDES WHETHER ONE NUMBER CAN ARBITRATE. The gate measures CANDIDATE
  // density -- positions where a branch head occurs -- and cannot tell a false start from a match.
  // Those two pull in OPPOSITE directions: a false start punishes the cascade (verify, reject,
  // resume) and leaves the automaton indifferent, while a match rewards the cascade (it stops) and
  // leaves the automaton indifferent. This sweep holds the candidate density FIXED and varies only
  // the fraction of candidates that complete. Along each row the gate sees one unchanging number.
  // If the winner flips along a row, that number cannot be the deciding quantity -- which is the
  // same argument the branch COUNT lost, applied to what replaced it.
  std::printf("\n\ncandidate density held FIXED, match fraction varied\n\n");
  std::printf("%12s %10s %10s %12s %12s %10s  %s\n", "1 cand/N", "cand/1000B", "matched %",
              "AC off (us)", "AC on (us)", "AC/casc", "");
  for (const std::size_t stride : {40U, 20U, 10U}) {
    for (const unsigned pct : {0U, 25U, 50U, 75U, 100U}) {
      std::string s(k_size, '_');
      std::size_t planted {0};
      std::size_t matches {0};
      std::size_t idx {0};
      for (std::size_t i = stride; i + word.size() + 1 < k_size; i += stride) {
        // Deterministic interleave: of every 100 candidate events, `pct` complete a branch.
        const bool complete {(idx % 100U) < pct};
        if (complete) {
          s.replace(i, word.size(), word);
          ++matches;
        }
        else {
          s[i] = heads[idx % heads.size()]; // a head that never completes
        }
        ++planted;
        ++idx;
      }
      const double dens {static_cast<double>(planted) * 1000.0 / static_cast<double>(k_size)};
      std::size_t  n_off {0};
      std::size_t  n_on {0};
      const double off {drain_ns(re, s, false, reps, n_off) / 1000.0};
      const double on {drain_ns(re, s, true, reps, n_on) / 1000.0};
      if (n_off != n_on) {
        std::printf("%12zu  MISMATCH (%zu/%zu) -- row invalid\n", stride, n_off, n_on);
        continue;
      }
      const double ratio {off > 0.0 ? on / off : 0.0};
      std::printf("%12zu %10.1f %10.0f %12.2f %12.2f %10.2fx  %s\n", stride, dens,
                  planted ? 100.0 * static_cast<double>(matches) / static_cast<double>(planted) : 0.0,
                  off, on, ratio, ratio < 1.0 ? "<-- AC ahead" : "");
    }
  }
  std::printf("\nIf the verdict flips inside a row-group -- same candidate density, different match\n"
              "fraction -- then candidate density alone cannot decide the route, and the gate needs\n"
              "a second quantity rather than a retuned constant.\n");
  return 0;
}
