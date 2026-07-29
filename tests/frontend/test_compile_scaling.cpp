// Compile cost must scale with the number of DISTINCT constructs, not with how often one is repeated.
//
// REAL's linear-time guarantee covers MATCHING. Nothing covered compile, and twice in consecutive
// releases a compile-cost defect reached CI only because the robustness fuzzer happened to trip its
// ten-second timeout on it: `unicode_casefold` walked the whole fold table per class (v2026.7.59), and
// then, still, folded the same class once per repetition of a bounded repeat. A bounded repeat holds ONE
// class node and emits it k times, so `\w{500}` folded `\w` five hundred times.
//
// A timeout is a blunt net -- it fires at ten seconds and says nothing about a pattern merely fifty times
// out of line. What these probes assert instead is the invariant those defects broke, in a form that
// cancels machine speed: the MARGINAL cost of one more repetition under icase, against the same marginal
// without it. Folding is per distinct class, so once it is cached the marginal costs must be the same
// work; a fold that runs per repetition makes the icase marginal explode while the plain one is untouched.
//
// Measured either side of the fix, best of seven, k = 8 against k = 256:
//
//     construct     ratio before     ratio after
//     \w                 383.25x           0.73x
//     \d                 671.68x           0.79x
//     \s                 196.08x           0.88x
//     \p{Greek}          434.56x           0.88x
//     \p{Han}            384.13x           0.86x
//     [\w\d]              60.99x           0.12x
//
// The bound below sits at 8x: nine times the worst passing ratio, and a seventh of the smallest failing
// one. Wall clock in a test is noisy, and that gap is what makes this safe to gate on.
#include <chrono>
#include <string>
#include <string_view>

#include <sciforge/test/framework.hpp>
#include "real/real.hpp"

namespace {

  using clock_type = std::chrono::steady_clock;

  //! \brief Repetition counts the marginal is taken between. Far enough apart that fixed setup cancels.
  constexpr int k_small {8};
  constexpr int k_large {256};

  //! \brief Compiles per measurement; the best is kept, so a descheduled run is discarded rather than
  //!        averaged in.
  constexpr int reps {7};

  //! \brief How far the icase marginal may exceed the plain one. See the table above for the margin.
  constexpr double ratio_bound {8.0};

  //! \brief Constructs whose EMISSION KIND is the same with and without icase -- every one of these is a
  //!        code-point class either way, so the marginal difference isolates folding.
  //!
  //!        `[a-z]` is deliberately absent: under icase it gains the long s and the Kelvin sign, stops
  //!        being a pure ASCII class and emits as a code-point class instead of a byte class. Its icase
  //!        marginal is legitimately ~25x its plain one, which is emission cost and not a fold running
  //!        per repetition. Including it would force a bound too loose to catch anything.
  constexpr std::string_view constructs[] {
    "\\w",
    "\\d",
    "\\s",
    "\\p{Greek}",
    "\\p{Han}",
    "[\\w\\d]",
  };

  //! \brief Best-of-\ref reps nanoseconds to compile \p pattern with \p f.
  //! \param[in] pattern The pattern to compile.
  //! \param[in] f       Flags to compile with.
  //! \return Best elapsed nanoseconds.
  double best_compile_ns(const std::string& pattern,
                         real::flags        f)
  {
    double best {-1.0};
    for (int r = 0; r < reps; ++r) {
      const auto        t0 {clock_type::now()};
      const real::regex rx(pattern, f);
      const double      ns {std::chrono::duration<double, std::nano>(clock_type::now() - t0).count()};
      if (best < 0.0 || ns < best) {
        best = ns;
      }
      (void) rx;
    }
    return best;
  }

  //! \brief Nanoseconds one further repetition of \p construct costs, under \p f.
  //! \param[in] construct The construct to repeat.
  //! \param[in] f         Flags to compile with.
  //! \return Marginal nanoseconds per repetition.
  double marginal_ns(std::string_view construct,
                     real::flags      f)
  {
    const std::string small {std::string(construct) + "{" + std::to_string(k_small) + "}"};
    const std::string large {std::string(construct) + "{" + std::to_string(k_large) + "}"};
    return (best_compile_ns(large, f) - best_compile_ns(small, f)) / static_cast<double>(k_large - k_small);
  }
} // namespace

// Probe 1 — the invariant itself. A repetition under icase must cost about what it costs without, because
// the fold it needs is a property of the class and is done once.
TEST(compile_scaling_icase_marginal_matches_plain_marginal)
{
  for (const std::string_view c : constructs) {
    const double plain {marginal_ns(c, real::flags::none)};
    const double fold  {marginal_ns(c, real::flags::icase)};

    // A non-positive plain marginal means the two sizes were indistinguishable -- nothing to divide by,
    // and nothing this probe can say. It has not been observed; the guard is so noise cannot fake a pass
    // by making the denominator tiny.
    EXPECT(plain > 0.0);
    if (plain <= 0.0) {
      continue;
    }
    EXPECT(fold / plain < ratio_bound);
  }
}

// Probe 2 — the shape the defects actually took, end to end. `\w{k}` with icase grew linearly in k because
// the fold ran per repetition; it must now grow far slower than the repeat count does.
TEST(compile_scaling_repeat_is_far_cheaper_than_linear)
{
  const double small {best_compile_ns("\\w{" + std::to_string(k_small) + "}", real::flags::icase)};
  const double large {best_compile_ns("\\w{" + std::to_string(k_large) + "}", real::flags::icase)};
  EXPECT(small > 0.0);

  // Linear in the repeat count would be 32x here. Before the fold cache this read 31.92x; it now reads
  // about 3.6x, which is the emitted program growing, not the fold repeating.
  const double size_ratio {static_cast<double>(k_large) / static_cast<double>(k_small)};
  EXPECT(large / small < size_ratio / 2.0);
}

// Probe 3 — a scoped `(?i:...)` folds one class two ways in one pattern, which is why the cache is keyed by
// mode and not by class alone. Both scopes must still be memoized across their own repetitions.
TEST(compile_scaling_scoped_icase_is_memoized_per_mode)
{
  const std::string small {"(?i:\\w{" + std::to_string(k_small) + "})\\w{" + std::to_string(k_small) + "}"};
  const std::string large {"(?i:\\w{" + std::to_string(k_large) + "})\\w{" + std::to_string(k_large) + "}"};
  const double      a     {best_compile_ns(small, real::flags::none)};
  const double      b     {best_compile_ns(large, real::flags::none)};
  EXPECT(a > 0.0);

  const double size_ratio {static_cast<double>(k_large) / static_cast<double>(k_small)};
  EXPECT(b / a < size_ratio / 2.0);
}
