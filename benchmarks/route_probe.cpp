// Route reachability probe: compose patterns from feature fragments and report which of the
// engine's dispatch routes they actually reach.
//
// WHY. The published benchmark suites measure a hand-picked list of patterns, and this engine's
// cost is decided by the ROUTE a pattern takes, not by the pattern's text. So the suites measure
// the routes someone thought of. A route that no composed pattern ever reaches is either dead or
// gated by a condition narrower than its author believed -- and neither shows up as a test
// failure, because correctness is identical on every route by construction (the seam differential
// pins that).
//
// WHAT IT IS NOT. It reports ROUTES, not timings. Route identity is exact and reproducible; a
// wall clock over randomly composed patterns is not, and pinning a figure per random pattern
// would produce reds indistinguishable from real regressions. Allocation counts are the other
// deterministic signal and belong in a sibling probe -- see benchmarks/measure.hpp for why they
// must not share a binary with timings.
//
// BUILD: needs -DREAL_PROFILE (the counters compile out otherwise) --
//     c++ -std=c++20 -O2 -DREAL_PROFILE -I include benchmarks/route_probe.cpp -o build/route_probe
// or `make route-probe`.

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

  using bench_gen::compose;
  using bench_gen::k_subjects;
} // namespace

int main(int argc, char** argv)
{
  const int  iterations {argc > 1 ? std::atoi(argv[1]) : 4000};
  const auto seed       {argc > 2 ? static_cast<std::uint32_t>(std::atoi(argv[2])) : 20260731U};

  std::mt19937 rng {seed};
  constexpr std::size_t n_routes {static_cast<std::size_t>(prof::route::count_)};

  std::vector<std::uint64_t> patterns_reaching(n_routes, 0); // patterns, not dispatches
  std::vector<std::string>   witness(n_routes);
  std::size_t                compiled {0};
  std::size_t                rejected {0};

  for (int it = 0; it < iterations; ++it) {
    const std::string pat {compose(rng)};
    try {
      const real::regex re {pat};
      ++compiled;
      std::vector<bool> seen(n_routes, false);
      for (const std::string& subject : k_subjects) {
        // Route coverage depends on the CALL as much as on the pattern, which the first seeded run
        // made obvious: every `*_window` route was unreachable while this probe only ever called
        // search() over a whole subject. A region search, a full match and an iteration reach
        // dispatches a plain search never does.
        for (int call = 0; call < 5; ++call) {
          prof::reset();
          try {
            switch (call) {
              case 0: (void) re.search(subject); break;
              case 1: (void) re.match(subject); break;
              case 2: (void) re.fullmatch(subject); break;
              case 3: (void) re.search(subject, subject.size() / 4,
                                       subject.size() - (subject.size() / 4)); break;
              default: {
                std::size_t n {0};
                for (const auto& m : re.find_iter(subject)) {
                  (void) m;
                  if (++n > 8) {
                    break;
                  }
                }
                break;
              }
            }
          }
          catch (const real::regex_error&) {
            continue; // a call this pattern does not support: not a route finding
          }
          const prof::counters& c {prof::snapshot()};
          for (std::size_t r = 0; r < n_routes; ++r) {
            if (c.routes[r] != 0) {
              seen[r] = true;
            }
          }
        }
      }
      for (std::size_t r = 0; r < n_routes; ++r) {
        if (seen[r]) {
          if (patterns_reaching[r] == 0) {
            witness[r] = pat;
          }
          ++patterns_reaching[r];
        }
      }
    }
    catch (const real::regex_error&) {
      ++rejected; // an unsupported composition: not a finding, the generator is deliberately loose
    }
  }

  std::printf("route probe: %d compositions, %zu compiled, %zu rejected by the parser (seed %u)\n\n",
              iterations, compiled, rejected, seed);
  std::printf("%-26s %10s  %s\n", "route", "patterns", "first witness");
  std::size_t unreached {0};
  for (std::size_t r = 0; r < n_routes; ++r) {
    const char* const name {prof::route_name(static_cast<prof::route>(r))};
    if (patterns_reaching[r] == 0) {
      ++unreached;
      std::printf("%-26s %10s  --\n", name, "NEVER");
    }
    else {
      std::printf("%-26s %10llu  %s\n", name,
                  static_cast<unsigned long long>(patterns_reaching[r]), witness[r].c_str());
    }
  }
  std::printf("\n%zu of %zu routes unreached by this generator.\n", unreached, n_routes);
  // Never a non-zero exit: an unreached route is a question for a human (is the generator too
  // narrow, or the gate?), not a build failure. Gating on it would pin the generator's blind
  // spots as if they were the engine's contract.
  return 0;
}
