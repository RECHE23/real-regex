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

namespace {

  namespace prof = real::detail::prof;

  //! Feature fragments. Composition is what finds route boundaries: nearly every surprise this
  //! engine has produced came from one edit flipping which route a pattern takes.
  const std::vector<std::string> k_atoms {
    "a",     "abc",   "xyzzy",                       // literals: exact_literal / inner_literal
    "[a-z]", "[0-9]", "[^,]",     "[a-f0-9]",        // byte classes: class_loop
    "\\w",   "\\d",   "\\s",                         // text-mode shorthands: cp_class_loop
    "\\p{L}", "\\p{N}", "\\p{sc=Han}", "[à-ÿ]", // properties / non-ASCII classes
    ".",                                             // dot
  };

  const std::vector<std::string> k_quants {"", "+", "*", "?", "{2,}", "{3}", "++", "*+"};
  const std::vector<std::string> k_wrap {"%s", "(%s)", "(?:%s)", "(?>%s)"};
  const std::vector<std::string> k_anchor_pre {"", "^", "\\b", "\\A"};
  const std::vector<std::string> k_anchor_post {"", "$", "\\b"};
  const std::vector<std::string> k_look {"", "(?=[a-z])", "(?![0-9])", "(?<=x)"};

  std::string wrap(const std::string& form, const std::string& inner)
  {
    const std::size_t at {form.find("%s")};
    if (at == std::string::npos) {
      return inner;
    }
    return form.substr(0, at) + inner + form.substr(at + 2);
  }

  //! Canonical shapes, one or more per route family. Free composition alone does NOT reach most
  //! routes -- the first run of this probe put 3354 of 3407 compositions on `general_full`, because
  //! every fast route wants a WHOLE-PATTERN shape and almost any added fragment breaks it. Seeding
  //! the shapes and mutating around them is what turns this from a toy into an instrument; it is
  //! also the neighbourhood search that found this engine's sharpest boundaries by hand.
  const std::vector<std::string> k_seeds {
    "[a-z]+", "\\w+", "\\p{L}+", "\\d",                    // class / cp-class / bare cp class
    "abc", "\"abc\"", "\\w+@\\w+", "\\d{4}-\\d{2}-\\d{2}",   // literal, delimited, inner-literal, fixed shape
    "[a-z]{3}[0-9]{2}", "(\\w+)@(\\w+)", "(\\w+)-(\\d+)",        // fixed-shape pair, capturing (one-pass)
    "cat|dog|fish", "[a-z]+(?=[0-9])", "a++", "[a-z]++",     // alternation, trailing LA, possessive
    "\\p{L}++", "\"[a-z]++\"", "^[a-z]+$", "(?i)[a-z]+",       // possessive cp / delimited / anchored / folded
  };

  //! One edit on a seed: the mutation that made every boundary in this engine visible when done by
  //! hand (`\p{L}+` against `\p{L}+a`, eleven branches against twelve).
  std::string mutate(const std::string& seed, std::mt19937& rng)
  {
    const auto pick = [&rng](const std::vector<std::string>& v) -> const std::string& {
                        return v[std::uniform_int_distribution<std::size_t>(0, v.size() - 1)(rng)];
                      };
    switch (std::uniform_int_distribution<int>(0, 4)(rng)) {
      case 0:  return seed + "a";                 // one trailing literal byte
      case 1:  return "a" + seed;                 // one leading literal byte
      case 2:  return pick(k_anchor_pre) + seed;  // an anchor
      case 3:  return seed + pick(k_anchor_post);
      default: return "(" + seed + ")";           // a capture around it
    }
  }

  //! One composed pattern. Deliberately not uniformly random: alternations and concatenations are
  //! drawn often enough to reach the multi-branch and fixed-shape routes, which a flat draw over
  //! atoms would starve.
  std::string compose(std::mt19937& rng)
  {
    const auto pick = [&rng](const std::vector<std::string>& v) -> const std::string& {
                        return v[std::uniform_int_distribution<std::size_t>(0, v.size() - 1)(rng)];
                      };
    // Half the draw goes to seeds and their one-edit neighbours; free composition keeps the rest,
    // since it is what finds shapes nobody thought to seed.
    const int bucket {std::uniform_int_distribution<int>(0, 9)(rng)};
    if (bucket <= 2) {
      return pick(k_seeds);
    }
    if (bucket <= 4) {
      return mutate(pick(k_seeds), rng);
    }
    const int shape {std::uniform_int_distribution<int>(0, 9)(rng)};
    std::string body;
    if (shape <= 4) { // concatenation of 1..3 quantified atoms
      const int n {std::uniform_int_distribution<int>(1, 3)(rng)};
      for (int i = 0; i < n; ++i) {
        body += wrap(pick(k_wrap), pick(k_atoms)) + pick(k_quants);
      }
    }
    else if (shape <= 7) { // alternation of 2..24 literal branches (the AC threshold lives here)
      const int n {std::uniform_int_distribution<int>(2, 24)(rng)};
      for (int i = 0; i < n; ++i) {
        if (i != 0) {
          body += '|';
        }
        body += "w" + std::to_string(i) + static_cast<char>('a' + (i % 26));
      }
    }
    else { // literal-delimited: the shape the inner-literal and possessive-delimited routes want
      body = "\"" + wrap(pick(k_wrap), pick(k_atoms)) + pick(k_quants) + "\"";
    }
    return pick(k_anchor_pre) + body + pick(k_look) + pick(k_anchor_post);
  }

  //! Subjects chosen so a route that needs a match can find one, and one that needs a miss can
  //! miss: a route only reached on success is invisible against a corpus that never matches.
  const std::vector<std::string> k_subjects {
    "the quick brown fox abc xyzzy 12345 a,b,c",
    "Ünïcödé tëxt with àccénts and 日本語 too",
    "\"quoted abc\" and w0a w1b w2c trailing",
    "",
    "zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz",
  };

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
