// Time XOR allocations -- never both from one translation unit.
//
// WHY THIS IS A HEADER AND NOT A CONVENTION. Counting allocations means replacing global
// `operator new`, which is not inlinable. On a workload that allocates often that replacement
// dominates the very wall clock it was linked in to explain: the same 16-pattern `regex_set`
// construction measured 7191 us with a counter linked and 2601 us without -- a 2.8x inflation
// read, and published, as a property of the code. The same mistake was made three times in one
// session, twice AFTER the hazard had been written down, which is the argument for a header that
// refuses rather than a note that asks.
//
// USE:
//     #define REAL_BENCH_TIME            // then bench::nanos(f, reps) / bench::micros(f, reps)
//     #include "measure.hpp"
// or:
//     #define REAL_BENCH_ALLOCS          // then bench::allocations(f, reps) / bench::bytes(f, reps)
//     #include "measure.hpp"
//
// Asking for both is a compile error, by design. Two binaries, two runs, two tables -- and say in
// the write-up which number came from which, because a reader cannot tell afterwards.
//
// REAL_BENCH_ALLOCS replaces global operator new/delete, so include it in exactly ONE translation
// unit of the program.

#ifndef REAL_BENCH_MEASURE_HPP
#define REAL_BENCH_MEASURE_HPP

#if defined(REAL_BENCH_TIME) && defined(REAL_BENCH_ALLOCS)
#  error "measure.hpp: REAL_BENCH_TIME and REAL_BENCH_ALLOCS are mutually exclusive. The allocation \
counter replaces global operator new, which is not inlinable and inflates wall clock by ~2.8x on \
allocation-heavy workloads -- timings taken alongside it are not measurements of the code. Build two \
binaries."
#endif

#if !defined(REAL_BENCH_TIME) && !defined(REAL_BENCH_ALLOCS)
#  error "measure.hpp: define exactly one of REAL_BENCH_TIME or REAL_BENCH_ALLOCS before including."
#endif

#include <cstddef>

#if defined(REAL_BENCH_TIME)
#  include <chrono>
#else
#  include <cstdlib>
#  include <new>
#endif

namespace bench {

#if defined(REAL_BENCH_TIME)

  // Minimum over `reps`, not the mean: the minimum is the run least contaminated by interference,
  // and on a quiet machine it is the closest thing to the code's own cost. On a machine that is not
  // quiet -- notably x86 under Docker, where interference arrives in multi-second episodes covering
  // whole cases -- take the minimum ACROSS RUNS as well; see docs/BENCHMARKS.md's methodology.
  template <typename F>
  double nanos(F&& f, int reps)
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

  template <typename F>
  double micros(F&& f, int reps)
  {
    return nanos(static_cast<F&&>(f), reps) / 1000.0;
  }

#else // REAL_BENCH_ALLOCS

  namespace detail {
    inline std::size_t g_count {0};
    inline std::size_t g_bytes {0};
    inline bool        g_on {false};
  } // namespace detail

  // Allocations and bytes requested through global operator new during one call to `f()`, averaged
  // over `reps`. Counts are exact and unaffected by the instrument; the wall clock of the same run
  // is NOT, which is why this header will not hand you both.
  template <typename F>
  double allocations(F&& f, int reps)
  {
    f(); // warm whatever caches the first call fills, so they are not charged to the average
    detail::g_on    = true;
    detail::g_count = 0;
    detail::g_bytes = 0;
    for (int r = 0; r < reps; ++r) {
      f();
    }
    detail::g_on = false;
    return static_cast<double>(detail::g_count) / reps;
  }

  template <typename F>
  double bytes(F&& f, int reps)
  {
    const double n {allocations(static_cast<F&&>(f), reps)};
    (void) n;
    return static_cast<double>(detail::g_bytes) / reps;
  }

#endif

} // namespace bench

#if defined(REAL_BENCH_ALLOCS)

void* operator new(std::size_t n)
{
  if (bench::detail::g_on) {
    ++bench::detail::g_count;
    bench::detail::g_bytes += n;
  }
  void* const p {std::malloc(n)};
  if (p == nullptr) {
    throw std::bad_alloc();
  }
  return p;
}

void operator delete(void* p) noexcept
{
  std::free(p);
}

void operator delete(void* p, std::size_t) noexcept
{
  std::free(p);
}

#endif

#endif // REAL_BENCH_MEASURE_HPP
