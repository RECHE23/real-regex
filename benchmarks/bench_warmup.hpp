// Clock ramp and core pinning for the per-call benchmarks. Header-only, called once from main, never
// from a timed path.
//
// WHY THIS EXISTS. A Linux box under the `powersave` governor idles at its minimum multiplier -- 1.2 GHz
// on a part whose base is 3.7 -- and ramps only under sustained load. A per-call benchmark is a few
// hundred microseconds of work per row, so it can complete entirely below the ramp. Measured on one such
// host, five runs of a SINGLE unchanged binary read 163.9, 230.2, 300.1, 301.8 and 315.5 ns on the same
// row, with 83.2 seen earlier the same hour: 3.8x of spread, none of it attributable to any code.
//
// AND WHY IT IS TWO THINGS, NOT ONE. Ramping without pinning does not fix it -- the ramp is per core, so
// the process migrates onto a core that has not ramped and the row reads 2.8x high. With BOTH, the same
// binary reads 77.1-85.2 ns across six runs. Neither half is optional:
//
//   ramp only, unpinned   82.7  83.3  83.9  231.2      <- one run in four is worthless
//   ramp + pinned         77.1  83.5  83.6  83.8  85.1  85.2
//
// A LONGER BATCH DOES NOT SUBSTITUTE. Growing the timed region from 50 us to 5 ms changed nothing once
// ramped and pinned; the batch exists to clear the clock's granularity (docs/MEASUREMENT.md §3.3), which
// is a different problem with a different fix.
//
// STILL TAKE THE MIN ACROSS RUNS on such a host. This brings the spread from 3.8x to 1.1x; it does not
// make a single run authoritative.

#ifndef REAL_BENCH_WARMUP_HPP
#define REAL_BENCH_WARMUP_HPP

#include <chrono>

#if defined(__linux__)
#  include <sched.h>
#endif

namespace bench {

  /*!
   * \brief Pins to the current core and holds it busy until the governor has ramped.
   * \param[in] ms How long to spin. 800 ms is enough for intel_pstate from its minimum multiplier.
   *
   * Pins FIRST, then ramps, so the ramp lands on the core the measurement will run on. Pinning to the
   * core we are already on rather than a chosen index leaves an outer `taskset` in charge when there is
   * one, and picks no fight with another tenant's placement when there is not.
   */
  inline void ramp_and_pin(double ms = 800.0)
  {
#if defined(__linux__)
    const int cpu {sched_getcpu()};
    if (cpu >= 0) {
      cpu_set_t set;
      CPU_ZERO(&set);
      CPU_SET(static_cast<unsigned>(cpu), &set);
      static_cast<void>(sched_setaffinity(0, sizeof(set), &set));
    }
#endif
    volatile double acc {0.0};
    const auto      start {std::chrono::steady_clock::now()};
    while (std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() < ms) {
      for (int k = 0; k < 10000; ++k) {
        acc = acc + 1.0e-9;
      }
    }
    static_cast<void>(acc);
  }

} // namespace bench

#endif // REAL_BENCH_WARMUP_HPP
