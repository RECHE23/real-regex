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
// READ THIS FIRST. THE ANSWER INVERTS THE PREMISE, AND IT TAKES TWO PANELS TO SEE IT.
// ============================================================================================
//
// The premise was that the general VM holds a broad swath of ordinary patterns the route cascade
// misses, so one machine plus prefilters could take them. It does not. Run the same census on a
// composition with three constructs removed -- lookaround, position assertion, possessive quantifier --
// and the general-VM population collapses to 1 pattern in 4000. The cascade takes essentially
// everything else: `[a-z]+[0-9]*` goes to lazy_dfa_anchored, `([a-z])+(\d){2,}` to onepass_window,
// `\w+@\w+` to inner_literal. Verified by route, not inferred from a zero.
//
// So the general VM's population is not "what the routes miss". It is, to a first approximation, THE
// THREE CONSTRUCTS THEMSELVES -- and one of them, the bounded lookaround, is exactly what distinguishes
// this engine from RE2 and rust-regex. A DFA core would not be taking work away from twenty routes; it
// would be competing with them for patterns they already take, while still not carrying the construct
// that put most of this population there.
//
// CAVEAT, STATED SO THE NUMBER IS NOT OVERSOLD: the plain variant builds three shapes (quantified-atom
// concatenation, literal alternation, delimited literal) and skips the seed list. "The cascade takes
// everything" is true of that repertoire. Generalising it needs a real caller corpus, which is now the
// next step for a good reason rather than a bad one.
//
// ============================================================================================
// AND THE ROUTE-COVERAGE PANEL CANNOT ANSWER THE QUESTION AT ALL, WHICH IS WHY BOTH ARE PRINTED.
//
// `compose()` injects three things, and the byte-program expander declines all three: a lookaround, a
// position assertion (via the anchor fragments), and a Tier-1 possessive loop. Measured on 8000 draws:
// NOT ONE pattern in the whole general_full population is free of all three. The stratum with none of
// them is EMPTY. So every "0 %" below is an artifact of the generator, not a verdict on the scanner --
// it was never handed a pattern it could be asked about. Read the strata as what they are: an accounting
// of what blocks, never a share of anything.
//
// TWO OF THE THREE ARE NOT ARTIFACTS THOUGH -- but they are not the same KIND of blocker, and the
// difference decides what a DFA core could ever aim at. Both are declined at both tiers; the reasons
// are not equivalent.
//
//   * A bounded lookaround is an ALGORITHMIC ceiling: no byte automaton over the main pattern carries a
//     sub-match decision at a position. A core cannot absorb it at any encoding, so the VM keeps it.
//   * A Tier-1 possessive loop is an ENCODING refusal, specific to this expander: its `primary_target`
//     holds a capture-slot index rather than a branch target, and the generic pc remap would corrupt it,
//     so `build_byte_program` declines outright. A possessive is SIMPLER than a greedy loop -- there is
//     no backtrack to represent -- and the 250 counted here are the ones that fell to the VM, not the
//     ones `run_possessive_*` already takes. A core could absorb them by changing the opcode, without
//     giving up the DFA.
//
// So do not read "the two constructs that distinguish this engine are both unabsorbable". One is a
// ceiling; the other is a representation this repository chose and could change.
//
// THE TWO TRAPS THIS FILE FELL INTO FIRST, kept because both are easy to repeat:
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

void census(const char* label, std::string (*make)(std::mt19937&), int iterations, std::uint32_t seed)
{
  std::mt19937 rng {seed};
  using bench_gen::k_subjects;

  const std::string haystack {long_subject(k_subjects.front())};

  std::size_t compiled {}, rejected {}, general {}, look_all {};
  std::size_t elig {}, elig_caps {}, tier_a {}, inelig_look {}, inelig_other {};
  std::size_t nolook {}, nolook_elig {}, nolook_elig_nocaps {}, nolook_tier_a {}, nolook_single {};
  // The doubly-clean stratum: no lookaround AND no `assert_position`. It is the only one on which the
  // scanning DFA's own predicate can be read, because those are the two things it declines.
  std::size_t assert_general {}, clean {}, clean_tier_a {}, clean_elig {}, clean_single {};
  std::size_t poss_general {}, clean_poss {}, bare {}, bare_tier_a {};
  std::vector<std::size_t> bp_code, bp_classes;

  for (int it = 0; it < iterations; ++it) {
    const std::string pat {make(rng)};
    try {
      const real::regex re {pat};
      ++compiled;
      const auto prog {re.raw_program()};
      const bool has_look {!prog.lookarounds.empty()};
      // READ FROM THE PROGRAM, NEVER FROM THE COMPOSED TEXT. What Tier-A declines is the OPCODE, and the
      // two do not correspond: a `\b` can be peeled away at compile time and leave none, while a `^` can
      // survive as one. Counting `k_anchor_pre` hits would be re-reading the generator -- the exact
      // mistake this file's header warns about, one level down. The lookaround stratum above already
      // reads the program's own table; this does the same for position assertions.
      const bool has_assert {std::any_of(prog.code.begin(), prog.code.end(), [](const auto& in) {
        return in.op == real::detail::opcode::assert_position;
      })};
      // The THIRD blocker: a Tier-1 possessive loop is declined by BOTH tiers, with the reason in
      // build_byte_program -- its `primary_target` carries a capture-slot index rather than a branch
      // target, so the generic pc remap would corrupt it. An encoding refusal, not a ceiling; see the
      // header for why that distinction decides what a core could aim at.
      const bool has_poss {std::any_of(prog.code.begin(), prog.code.end(), [](const auto& in) {
        return in.op == real::detail::opcode::byte_loop_possessive
               || in.op == real::detail::opcode::klass_loop_possessive
               || in.op == real::detail::opcode::klass_cp_loop_possessive;
      })};
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

      if (has_assert) {
        ++assert_general;
      }
      if (has_poss) {
        ++poss_general;
      }
      if (!has_look && !has_assert && has_poss) {
        ++clean_poss;
      }
      if (!has_look && !has_assert && !has_poss) {
        ++bare;
        if (a.eligible) {
          ++bare_tier_a;
        }
      }
      if (!has_look && !has_assert) {
        ++clean;
        if (a.eligible) {
          ++clean_tier_a;
        }
        if (eligible) {
          ++clean_elig;
        }
        if (single) {
          ++clean_single;
        }
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

  std::printf("\n================ %s ================\n", label);
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
  std::printf("    carrying an assert_position OPCODE     %6zu  %5.1f %%  <- what the line above still\n"
              "                                                           mixes in\n",
              assert_general, pct(assert_general, general));

  std::printf("\n  THE DOUBLY-CLEAN STRATUM -- no lookaround, no assert_position: %zu patterns.\n", clean);
  std::printf("  This is the only stratum on which the SCANNER's predicate can be read: those two ops are\n"
              "  exactly what it declines, so anything still ineligible here is blocked by something else.\n");
  std::printf("    taken by today's scanning DFA (Tier-A) %6zu  %5.1f %%\n", clean_tier_a,
              pct(clean_tier_a, clean));
  std::printf("    expander-representable (either tier)   %6zu  %5.1f %%\n", clean_elig,
              pct(clean_elig, clean));
  std::printf("    single live thread                     %6zu  %5.1f %%\n", clean_single,
              pct(clean_single, clean));
  std::printf("    of which carry a POSSESSIVE loop       %6zu  %5.1f %%  <- declined by BOTH tiers\n",
              clean_poss, pct(clean_poss, clean));
  std::printf("\n  AND WITH POSSESSIVES REMOVED TOO -- %zu patterns, nothing left that either tier declines:\n",
              bare);
  std::printf("    taken by today's scanning DFA (Tier-A) %6zu  %5.1f %%\n", bare_tier_a,
              pct(bare_tier_a, bare));
  std::printf("  (possessive loops in the whole population: %zu, %.1f %%)\n", poss_general,
              pct(poss_general, general));

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
}

int main(int argc, char** argv)
{
  const int  iterations {argc > 1 ? std::atoi(argv[1]) : 8000};
  const auto seed {argc > 2 ? static_cast<std::uint32_t>(std::atoi(argv[2])) : 20260814U};

  // BOTH PANELS, always, because either one alone misleads. The route-coverage generator is what the
  // published "4911 of 7406" comes from and it cannot answer the DFA question -- its stratum is empty.
  // The plain variant can answer it but reaches fewer routes. Printing one without the other is how the
  // first four readings of this census went wrong.
  census("ROUTE-COVERAGE GENERATOR (compose) -- cannot answer the DFA question", bench_gen::compose,
         iterations, seed);
  census("PLAIN VARIANT (compose_plain) -- no lookaround, no anchor, no possessive quantifier",
         bench_gen::compose_plain, iterations, seed);
  return 0;
}
