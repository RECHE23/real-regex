// Allocation budget per dispatch route.
//
// THE INVARIANT. How many heap allocations a search performs should be a property of the ROUTE it
// takes, not of the pattern that took it. A route whose patterns all allocate zero, except a few
// that allocate thirteen, is not a route with a wide budget -- it is a route with a defect, or a
// dispatch that is quietly sending unlike work down one name. That spread is what this probe
// reports, and it is deterministic: allocation counts are exact, identical run to run, and
// unaffected by machine load. Unlike a timing bench, it cannot produce a red that means "the
// runner was busy".
//
// WHY IT IS WORTH HAVING. The most expensive defect this engine has produced -- an Aho-Corasick
// automaton rebuilt on every search, 29.5 us and 584 allocations where the route should cost
// neither -- was invisible to every existing gate and was found by hand, years later, while
// digging into something else. This probe would have named it on the day it was written: a route
// whose siblings allocate zero, allocating 584.
//
// PAIRING. It composes patterns with the same generator as route_probe (benchmarks/pattern_gen.hpp)
// so the two tables describe the same population. It reports allocations only -- never timings --
// because the counter that makes the counts exact makes the clock lie; benchmarks/measure.hpp
// refuses to hand you both, and this file takes the half that is trustworthy.
//
// BUILD: c++ -std=c++20 -O2 -DREAL_PROFILE -I include -I benchmarks benchmarks/alloc_probe.cpp
// or `make alloc-probe`.

#define REAL_BENCH_ALLOCS
#include "measure.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

#include "real/real.hpp"
#include "pattern_gen.hpp"

namespace {

  namespace prof = real::detail::prof;

  //! Allocation counts seen for one route, one entry per pattern attributed to it.
  struct budget
  {
    std::vector<double> samples;
    std::string         worst_pattern;
    double              worst {-1.0};
  };

} // namespace

int main(int argc, char** argv)
{
  const int  iterations {argc > 1 ? std::atoi(argv[1]) : 3000};
  const auto seed       {argc > 2 ? static_cast<std::uint32_t>(std::atoi(argv[2])) : 20260731U};

  std::mt19937          rng {seed};
  constexpr std::size_t n_routes {static_cast<std::size_t>(prof::route::count_)};
  std::vector<budget>   per_route(n_routes);
  std::size_t           attributed {0};
  std::size_t           ambiguous {0};

  for (int it = 0; it < iterations; ++it) {
    const std::string pat {bench_gen::compose(rng)};
    real::regex       re {""};
    try {
      re = real::regex {pat};
    }
    catch (const real::regex_error&) {
      continue;
    }
    for (const std::string& subject : bench_gen::k_subjects) {
      // Which route? One search with the counter OFF -- prof's counters are thread-local scalars
      // and allocate nothing, so profiling and allocation counting can share a binary where
      // profiling and TIMING could not.
      prof::reset();
      (void) re.search(subject);
      const prof::counters& c {prof::snapshot()};
      std::size_t           hit {n_routes};
      std::size_t           n_hit {0};
      for (std::size_t r = 0; r < n_routes; ++r) {
        if (c.routes[r] != 0) {
          hit = r;
          ++n_hit;
        }
      }
      if (n_hit != 1) {
        // Attribute only when exactly one route fired: a search that crossed two routes cannot
        // charge its allocations to either without inventing a split.
        ambiguous += (n_hit > 1) ? 1 : 0;
        continue;
      }
      const double allocs {bench::allocations([&] { (void) re.search(subject); }, 200)};
      budget&      b {per_route[hit]};
      b.samples.push_back(allocs);
      if (allocs > b.worst) {
        b.worst         = allocs;
        b.worst_pattern = pat;
      }
      ++attributed;
    }
  }

  std::printf("alloc probe: %d compositions, %zu single-route searches attributed, "
              "%zu multi-route skipped (seed %u)\n\n",
              iterations, attributed, ambiguous, seed);
  std::printf("%-26s %8s %8s %8s %8s  %s\n", "route", "n", "min", "median", "max", "worst pattern");
  std::size_t suspect {0};
  for (std::size_t r = 0; r < n_routes; ++r) {
    budget& b {per_route[r]};
    if (b.samples.empty()) {
      continue;
    }
    std::sort(b.samples.begin(), b.samples.end());
    const double lo  {b.samples.front()};
    const double mid {b.samples[b.samples.size() / 2]};
    const double hi  {b.samples.back()};
    // THE invariant: a fast route exists to avoid the general VM's machinery, and its heap scratch
    // is part of that machinery -- so every route but general_* must allocate nothing per search.
    // Spread within a route is a second, weaker signal (unlike work down one name).
    //
    // The first cut of this check flagged SPREAD only, and would have missed the very defect it was
    // written for: the Aho-Corasick route rebuilt its automaton on every search, 584 allocations,
    // CONSISTENTLY -- min equal to max, no spread, silently past a spread test. Levels against a
    // per-family expectation, not variance, is what catches a route that is uniformly wrong.
    const bool is_general {r == static_cast<std::size_t>(prof::route::general_full)
                           || r == static_cast<std::size_t>(prof::route::general_window)};
    const bool flag {(!is_general && hi > 0.0) || (lo == 0.0 && hi > 0.0)};
    if (flag) {
      ++suspect;
    }
    std::printf("%-26s %8zu %8.2f %8.2f %8.2f  %s%s\n",
                prof::route_name(static_cast<prof::route>(r)), b.samples.size(), lo, mid, hi,
                flag ? "<-- OFF BUDGET: " : "", flag ? b.worst_pattern.c_str() : "");
  }
  std::printf("\n%zu route(s) off budget: a non-general route allocating at all, or any route that\n"
              "  allocates for some patterns and not others.\n", suspect);
  // Never a non-zero exit, for route_probe's reason: a spread is a question for a human, and
  // gating on it would pin today's dispatch shape as if it were the contract.
  return 0;
}
