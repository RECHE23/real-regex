// Where a pattern LOSES its route, and what that costs -- route identity, per-byte cost and MATCH COUNT
// in one table, arranged as ladders that add one construct at a time.
//
// WHY THE THREE TOGETHER. benchmarks/route_probe.cpp reports which routes a pattern reaches but never a
// timing, deliberately. That is right for reachability and wrong for this question: a route label alone
// cannot say whether falling off it is expensive, and a timing alone cannot say what fell. And the match
// count is not decoration -- the first version of this measurement read 91.6 ns/B for
// `(?:ERROR|WARN|FATAL):\s+.*$` and recommended work on the strength of it, when that pattern matches
// NOTHING on the subject (a `.*$` cannot match a subject whose last line is cut mid-text). What was
// measured was a failing scan, not the shape's work. A row without its match count is not a measurement.
//
// WHAT THE LADDERS SHOWED, on an idle x86-64 host with the governor pinned, 4096-byte subjects, 186
// matches on every row of the literal ladders:
//
//     [a-z]+                       1.09 ns/B   class_loop            (control)
//     ERROR:                       1.31        exact_literal
//     ERROR:\s+                    3.52        lazy_dfa + general    <- route lost at the FIRST suffix
//     ERROR:\s+.                   4.96        lazy_dfa + general
//     ERROR:\s+.*                 15.12        lazy_dfa + general    <- the unbounded tail
//     (?:ERROR|WARN|FATAL):        4.33        inner_literal
//     (?:ERROR|WARN|FATAL):\s+     5.34        inner_literal
//     (?:ERROR|WARN|FATAL):\s+.    6.86        inner_literal
//     (?:ERROR|WARN|FATAL):\s+.*  14.20        lazy_dfa + general    <- route lost at `.*`
//
// TWO FINDINGS, neither of which the route table alone would have given:
//
//  1. The alternation-prefix form KEEPS `inner_literal` through two suffixes and loses it at `.*`, while
//     the single-literal form loses `exact_literal` at the first suffix. So the better-routed of the two
//     is the more complex pattern -- and the single literal, the classic "lines starting with X" shape,
//     is the one with no literal prefilter.
//  2. `(?:ERROR|WARN|FATAL):` pays 4.33 ns/B against `ERROR:`'s 1.31 for the same 186 matches. The inner
//     literal it prefilters on is the `:` -- ONE byte, so a weak filter. The set {ERROR, WARN, FATAL} is
//     the filter that shape wants, and nothing offers it: the Aho-Corasick gate has a four-branch floor
//     (this is three) and the alternation route claims whole patterns, not prefixes of them.
//
// NOT A GATE, and it must not become one: these are timings, so a pinned figure per row would produce
// reds indistinguishable from host noise. It is a dev tool for locating a cliff, and every number above
// is a reading on one host, at one moment, printed so the next reader can re-take it rather than trust it.
//
// BUILD: needs -DREAL_PROFILE for the route counters --
//     c++ -std=c++20 -O2 -DREAL_PROFILE -I include -I benchmarks benchmarks/route_cliff.cpp
// or `make bench-route-cliff`.
#include <real/real.hpp>

#include "bench_warmup.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace prof = real::detail::prof;

namespace {

  volatile std::size_t sink {0};

  //! Batched clock: one call here is well under the clock's granularity (MEASUREMENT.md §3.3).
  template <typename Fn>
  double ns_per_call(Fn&& fn, int samples)
  {
    int inner {1};
    for (; inner < (1 << 22); inner *= 2) {
      const auto t0 {std::chrono::steady_clock::now()};
      for (int k = 0; k < inner; ++k) {
        sink = fn();
      }
      if (std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() >= 50.0) {
        break;
      }
    }
    std::vector<double> draws;
    draws.reserve(static_cast<std::size_t>(samples));
    for (int s = 0; s < samples; ++s) {
      const auto t0 {std::chrono::steady_clock::now()};
      for (int k = 0; k < inner; ++k) {
        sink = fn();
      }
      const auto t1 {std::chrono::steady_clock::now()};
      draws.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count() / inner);
    }
    std::sort(draws.begin(), draws.end());
    return draws[draws.size() / 2];
  }

  /*!
   * \brief Grow \p unit to \p n bytes, then trim trailing blanks.
   *
   * The trim is load-bearing: a `.*$` pattern cannot match a subject whose final line is cut mid-text, and
   * such a row reports a FAILING scan while looking like a slow one.
   */
  std::string grown(const char* unit, std::size_t n)
  {
    std::string s;
    while (s.size() < n) {
      s += unit;
    }
    s.resize(n);
    while (!s.empty() && (s.back() == '\n' || s.back() == ' ')) {
      s.pop_back();
    }
    return s;
  }

  //! The routes one call dispatched, most-used first, as a compact string.
  std::string routes_of(const real::regex& re, const std::string& s)
  {
    prof::reset();
    sink = re.count_matches(s);
    const auto& c {prof::snapshot()};
    std::vector<std::pair<std::uint64_t, const char*>> hit;
    for (std::size_t i = 0; i < static_cast<std::size_t>(prof::route::count_); ++i) {
      if (c.routes[i] != 0U) {
        hit.emplace_back(c.routes[i], prof::route_name(static_cast<prof::route>(i)));
      }
    }
    std::sort(hit.begin(), hit.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    std::string out;
    for (const auto& [n, name] : hit) {
      if (!out.empty()) {
        out += " + ";
      }
      out += name;
    }
    return out.empty() ? std::string {"(none)"} : out;
  }

}  // namespace

int main(int argc, char** argv)
{
  const int             samples {argc > 1 ? std::atoi(argv[1]) : 15};
  constexpr std::size_t k_size {4096};
  bench::ramp_and_pin();

  struct rung
  {
    const char* pattern;
    const char* unit;
  };
  const rung rungs[] {
    {R"([a-z]+)", "log ERROR:  disk full\n"},
    {R"(ERROR:)", "log ERROR:  disk full\n"},
    {R"(ERROR:\s+)", "log ERROR:  disk full\n"},
    {R"(ERROR:\s+.)", "log ERROR:  disk full\n"},
    {R"(ERROR:\s+.*)", "log ERROR:  disk full\n"},
    {R"((?:ERROR|WARN|FATAL):)", "log ERROR:  disk full\n"},
    {R"((?:ERROR|WARN|FATAL):\s+)", "log ERROR:  disk full\n"},
    {R"((?:ERROR|WARN|FATAL):\s+.)", "log ERROR:  disk full\n"},
    {R"((?:ERROR|WARN|FATAL):\s+.*)", "log ERROR:  disk full\n"},
    {R"(^[\t \n\r]+|[\t \n\r]+$)", "   \t indented value here \n  "},
    {R"([\t \n\r]+)", "   \t indented value here \n  "},
  };

  std::printf("# route cliff -- %zu-byte subjects, median of %d batched draws\n", k_size, samples);
  std::printf("# a row with 0 matches measures a FAILING scan; read it as such or not at all\n\n");
  std::printf("  %-30s %9s %9s %8s  %s\n", "pattern", "ns/call", "ns/byte", "matches", "route(s)");
  for (const rung& r : rungs) {
    const real::regex      re {r.pattern};
    const std::string      s {grown(r.unit, k_size)};
    const std::string_view v {s};
    const std::string      routes {routes_of(re, s)};
    const std::size_t      hits {re.count_matches(v)};
    const double           ns {ns_per_call([&] { return re.count_matches(v); }, samples)};
    std::printf("  %-30s %9.0f %9.2f %8zu  %s\n", r.pattern, ns, ns / static_cast<double>(k_size), hits,
                routes.c_str());
  }
  return 0;
}
