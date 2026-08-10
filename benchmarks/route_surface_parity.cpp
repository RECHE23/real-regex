// Route/SURFACE parity: every API that enumerates all matches must reach the SAME dispatch route.
//
// WHY THIS EXISTS. `find_iter` ran the general Pike VM on `[a-z]+(?=[a-z])` while `count_matches` and
// `find_all` took the dedicated trailing-lookaround route -- 53.84 ns/B against 4.41, a 12x gap for the
// same pattern and the same matches. The cause was structural rather than an oversight: those two branch
// internally onto `basic_match_range<Storage, TrailingLA = true>`, and `find_iter` could not, because its
// RETURN TYPE names the specialization and a runtime hint cannot pick a type.
//
// Nothing caught it. Correctness could not: both routes answer identically, which is the seam
// differential's whole premise. The benchmarks could not: every row in `bench_minimal.cpp` measured
// `count_matches`, so the published `lookahead` figure described the fast path while the iterator API ran
// 12x slower. It was found by accident, profiling that row for an unrelated reason.
//
// So this asks the question directly, and deterministically -- route counters, no timing, indifferent to
// machine load. A divergence here is not a wrong answer; it is one API paying for a route another one
// gets, which is exactly the defect class that hid for a whole release train.
//
// WARM THE REGEX FIRST, and the reason is a false positive this file produced before it did: the first
// surface measured pays `ensure_immutables` and the lazy-DFA build, so it bills routes the others then
// reuse. Unwarmed, `(\w+)_(\w+)` read `inner_literal+lazy_dfa_anchored+general_window` on whichever
// surface went first and `inner_literal` on the rest -- a cold path, not an asymmetry.
#include <cstdio>
#include <string>
#include <vector>

#include "real/real.hpp"

namespace {

  //! Every non-zero route counter, joined -- the fingerprint a surface leaves behind.
  std::string routes_of(const real::detail::prof::counters& c)
  {
    std::string s;
    for (std::size_t i = 0; i < static_cast<std::size_t>(real::detail::prof::route::count_); ++i) {
      if (c.routes[i] == 0) {
        continue;
      }
      if (!s.empty()) {
        s += "+";
      }
      s += real::detail::prof::route_name(static_cast<real::detail::prof::route>(i));
    }
    return s.empty() ? "-" : s;
  }

  struct probe
  {
    const char* pattern;
    const char* unit; //!< repeated to build a subject past every route's minimum-input gate
  };

  //! One representative per route family the engine dispatches for a search.
  const std::vector<probe>& probes()
  {
    static const std::vector<probe> p {
      {"[a-z]+", "abc def ghi "},
      {"[a-z]", "abc def "},
      {"[a-z]{2,}", "abc de f "},
      {"[a-z]++", "abc def "},
      {"\\w+", "abc_1 def "},
      {"\\w{2,}", "ab_1 cd "},
      {"\\p{L}+", "abc caf\xC3\xA9 "},
      {"[^,]+", "aa,bb,cc,"},
      {".", "abc "},
      {"dog", "the dog and dog "},
      {"the|fox|dog", "the fox and dog "},
      {"[0-9]{4}-[0-9]{2}-[0-9]{2}", "x 2026-08-10 y "},
      {"(\\w+)_(\\w+)", "aa_bb cc_dd "},
      {"[a-z]+(?=[a-z])", "abc def "},
      {"[0-9]+(?![0-9])", "12 345 "},
      {"\\b\\w+\\b", "aa bb cc "},
      {"a++", "aaa b aa "},
      {"\"[a-z]*+\"", "\"ab\" \"cd\" "},
      {"[ab][cd]", "ac bd ad "},
      {"(?i)cafe", "cafe CAFE Cafe "},
      {"\\d+\\.\\d+", "1.5 22.75 "},
      {"[0-9a-f]{8}", "deadbeef cafe1234 "},
    };
    return p;
  }

  const char* const surface_name[] {"count_matches", "find_iter", "find_all", "replace", "split"};

  //! Drives surface \p s over \p text and returns the routes it billed.
  std::string bill(const real::regex& re,
                   const std::string& text,
                   int                s)
  {
    real::detail::prof::reset();
    switch (s) {
      case 0:
        (void) re.count_matches(text);
        break;
      case 1:
        for (const auto& m : re.find_iter(text)) {
          (void) m;
        }
        break;
      case 2:
        (void) re.find_all(text);
        break;
      case 3:
        (void) re.replace(text, "X");
        break;
      default:
        (void) re.split(text);
        break;
    }
    return routes_of(real::detail::prof::snapshot());
  }
} // namespace

int main()
{
  std::printf("route/surface parity — every enumerating API must bill the same route\n\n");
  int diverging {0};
  for (const probe& p : probes()) {
    std::string text;
    while (text.size() < 40000) {
      text += p.unit;
    }
    const real::regex re {p.pattern};
    // See the file header: the cold path bills routes the warm one does not.
    (void) re.count_matches(text);
    (void) re.find_all(text);

    std::string billed[5];
    for (int s = 0; s < 5; ++s) {
      billed[s] = bill(re, text, s);
    }
    bool same {true};
    for (int s = 1; s < 5; ++s) {
      if (billed[s] != billed[0]) {
        same = false;
      }
    }
    if (same) {
      std::printf("  ok        %-28s %s\n", p.pattern, billed[0].c_str());
    }
    else {
      ++diverging;
      std::printf("  DIVERGES  %-28s\n", p.pattern);
      for (int s = 0; s < 5; ++s) {
        std::printf("              %-14s %s\n", surface_name[s], billed[s].c_str());
      }
    }
  }
  std::printf("\n%s: %d of %zu pattern(s) diverge across enumerating surfaces\n",
              diverging == 0 ? "route-surface-parity: PASS" : "route-surface-parity: FAIL",
              diverging, probes().size());
  return diverging == 0 ? 0 : 1;
}
