// DFA census of the `general_full` population, BY COUNTING.
//
// THE QUESTION. This engine's cost is decided by the route a pattern takes, and the general Pike VM
// costs 12-25 ns per byte stepped against ~1 for a routed class loop. So: could one machine plus
// prefilters take that population, leaving the VM to lookarounds and ambiguous captures -- or can it
// not, in which case making the VM cheap is the whole game? That dependency runs one way, which is why
// this counts before anything is written.
//
// NO STOPWATCH. Every field read is exact and reproducible: the engine's own byte-program expander, the
// program's `slot_count` and lookaround table, the route counters, the thread histogram. A wall clock
// over randomly composed patterns produces reds indistinguishable from noise -- the lesson
// route_probe.cpp already carries in its own header, and this file inherits it.
//
// ============================================================================================
// READ THIS BEFORE READING A PERCENTAGE. TWO TRAPS, BOTH OF WHICH THIS FILE FELL INTO FIRST.
// ============================================================================================
//
// 1. `byte_program::eligible` IS NOT `lazy_dfa`'s ELIGIBILITY. They are different predicates and
//    conflating them inflates the answer by construction. `build_byte_program` says whether the pattern
//    can be EXPANDED into a byte-only program. `lazy_dfa::compute_eligibility` (lazy_dfa.hpp) decides
//    whether the SCANNING automaton will take it, and it declines `assert_position` on exactly the same
//    footing as a lookaround -- because `consumes()` recognizes byte/klass and nothing else. Handing the
//    scanner a Tier-B program does not help: it re-reads the instruction stream and says no. So a
//    "representable" figure here is an upper bound on what a FUTURE search automaton could reach, never
//    a count of patterns today's DFA would take. The Tier-A column below is the one that answers that,
//    and it reads zero.
//
// 2. THE GENERATOR INJECTS LOOKAROUNDS, AND A LOOKAROUND FORCES THE GENERAL VM. `pattern_gen.hpp`'s
//    `k_look` has four entries, one of them empty, picked uniformly, and appended to free composition
//    only -- half the draws are seeds or one-edit mutants with none. 50 % x 75 % = 37.5 % expected; the
//    run below measures it. Since a lookaround wipes every fast-path hint, those patterns land in this
//    population almost by definition, so "share of general_full carrying a lookaround" is largely
//    tautological. The STRATUM WITHOUT LOOKAROUNDS is the only part that answers the question asked, and
//    it is reported separately rather than divided out afterwards.
//
// The published "4911 of 7406 composed patterns reach general_full" comes from this same generator and
// carries the same weighting. It is a property of the panel, not a share of any caller's workload.
//
// WHAT IT CANNOT SAY, and does not estimate instead: how many STATES a DFA would build. Nothing exposes
// that -- no counter, no profile event. The byte program's size and class count are reported as what
// they are, bounds on the NFA, not a prediction of state explosion. Answering that needs a counter in
// the DFA's cache, and until it exists an eligibility figure is not a routing decision.
//
// BUILD: needs -DREAL_PROFILE for the route and thread counters --
//     c++ -std=c++20 -O2 -DREAL_PROFILE -I include -I benchmarks benchmarks/dfa_census.cpp
// or `make bench-dfa-census`. Informational, never a gate.
#include <real/real.hpp>

#include "pattern_gen.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

namespace prof = real::detail::prof;

namespace {

  volatile std::size_t sink {0};

  std::string long_subject(const std::string& unit)
  {
    std::string s;
    while (s.size() < 200000) {
      s += unit;
    }
    return s;
  }

  //! Highest occupied bucket of the thread histogram = the most live threads the walk carried.
  std::uint64_t peak_threads(const prof::counters& c)
  {
    std::uint64_t worst {0};
    for (std::size_t i = 0; i < 8; ++i) {
      if (c.thread_hist[i] != 0U) {
        worst = i + 1;
      }
    }
    return worst;
  }

}  // namespace

int main(int argc, char** argv)
{
  const int  iterations {argc > 1 ? std::atoi(argv[1]) : 8000};
  const auto seed {argc > 2 ? static_cast<std::uint32_t>(std::atoi(argv[2])) : 20260814U};

  std::mt19937 rng {seed};
  using bench_gen::compose;
  using bench_gen::k_subjects;

  const std::string haystack {long_subject(k_subjects.front())};

  std::size_t compiled {}, rejected {}, general {}, look_all {};
  std::size_t elig {}, elig_caps {}, tier_a {}, inelig_look {}, inelig_other {};
  std::size_t nolook {}, nolook_elig {}, nolook_elig_nocaps {}, nolook_tier_a {}, nolook_single {};
  std::vector<std::size_t> bp_code, bp_classes;

  for (int it = 0; it < iterations; ++it) {
    const std::string pat {compose(rng)};
    try {
      const real::regex re {pat};
      ++compiled;
      const auto prog {re.raw_program()};
      const bool has_look {!prog.lookarounds.empty()};
      if (has_look) {
        ++look_all;
      }

      prof::reset();
      sink += re.search(haystack).matched() ? 1U : 0U;
      const auto& c {prof::snapshot()};
      if (c.routes[static_cast<std::size_t>(prof::route::general_full)] == 0U) {
        continue;
      }
      ++general;
      const bool single {peak_threads(c) <= 1};

      // The engine's own expander, called directly. `immut->byte_prog` is useless here: it is filled
      // only by a DFA route, and by definition none ran on this population -- reading the field asks a
      // cache nobody populated, which is why the first version of this census reported 0 %.
      const auto  a {real::detail::build_byte_program(prog, /*keep_assertions=*/ false)};
      const auto  b {real::detail::build_byte_program(prog, /*keep_assertions=*/ true)};
      const bool  eligible {a.eligible || b.eligible};
      const auto& bp {a.eligible ? a : b};
      const bool  has_caps {prog.slot_count > 2};

      if (eligible) {
        ++elig;
        if (has_caps) {
          ++elig_caps;
        }
        if (a.eligible) {
          ++tier_a;
        }
        bp_code.push_back(bp.code.size());
        bp_classes.push_back(bp.classes.size());
      }
      else if (has_look) {
        ++inelig_look;
      }
      else {
        ++inelig_other;
      }

      if (!has_look) {
        ++nolook;
        if (eligible) {
          ++nolook_elig;
          if (!has_caps) {
            ++nolook_elig_nocaps;
          }
        }
        if (a.eligible) {
          ++nolook_tier_a;
        }
        if (single) {
          ++nolook_single;
        }
      }
    }
    catch (...) {
      ++rejected;
    }
  }

  const auto pct = [](std::size_t x, std::size_t n) {
    return n != 0U ? 100.0 * static_cast<double>(x) / static_cast<double>(n) : 0.0;
  };

  std::printf("# DFA census of general_full -- %d draws, seed %u\n\n", iterations, seed);
  std::printf("  compiled %zu   rejected %zu   general_full %zu (%.1f %%)\n", compiled, rejected, general,
              pct(general, compiled));
  std::printf("  carrying a lookaround: %zu of compiled (%.1f %%) -- injected by the generator, and a\n"
              "  lookaround FORCES the general VM, so its share of the population below is largely\n"
              "  tautological. See this file's header before reading the next block as a workload.\n\n",
              look_all, pct(look_all, compiled));

  std::printf("  whole population (%zu), for reference only:\n", general);
  std::printf("    expander-representable                 %6zu  %5.1f %%\n", elig, pct(elig, general));
  std::printf("      of which blocked by captures alone   %6zu  %5.1f %%\n", elig_caps, pct(elig_caps, general));
  std::printf("    not representable: lookaround          %6zu  %5.1f %%\n", inelig_look,
              pct(inelig_look, general));
  std::printf("    not representable: other               %6zu  %5.1f %%\n", inelig_other,
              pct(inelig_other, general));

  std::printf("\n  THE STRATUM WITHOUT LOOKAROUNDS -- %zu patterns, the part that answers the question:\n",
              nolook);
  std::printf("    expander-representable (Tier-B)        %6zu  %5.1f %%\n", nolook_elig,
              pct(nolook_elig, nolook));
  std::printf("      and capture-free                     %6zu  %5.1f %%\n", nolook_elig_nocaps,
              pct(nolook_elig_nocaps, nolook));
  std::printf("    TAKEN BY TODAY'S SCANNING DFA          %6zu  %5.1f %%  <- Tier-A, what\n"
              "                                                           ensure_immutables builds\n",
              nolook_tier_a, pct(nolook_tier_a, nolook));
  std::printf("    single live thread                     %6zu  %5.1f %%\n", nolook_single,
              pct(nolook_single, nolook));

  if (!bp_code.empty()) {
    std::sort(bp_code.begin(), bp_code.end());
    std::sort(bp_classes.begin(), bp_classes.end());
    const auto q = [](const std::vector<std::size_t>& v, double f) {
      return v[static_cast<std::size_t>(f * static_cast<double>(v.size() - 1))];
    };
    std::printf("\n  expanded byte program, among the representable (a bound on the NFA, NOT a state count):\n");
    std::printf("    instructions  p50 %zu   p90 %zu   max %zu\n", q(bp_code, 0.5), q(bp_code, 0.9),
                bp_code.back());
    std::printf("    classes       p50 %zu   p90 %zu   max %zu\n", q(bp_classes, 0.5), q(bp_classes, 0.9),
                bp_classes.back());
  }
  std::printf("\n  A representable figure is an upper bound on a FUTURE search automaton, never a count of\n"
              "  what routes today. The Tier-A line is the one that says what routes today.\n");
  return 0;
}
