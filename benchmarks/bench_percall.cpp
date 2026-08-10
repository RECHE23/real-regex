// Per-call duel: REAL's std::regex swap-in against std::regex itself, on subjects decided in tens of
// nanoseconds.
//
// A SEPARATE TRANSLATION UNIT ON PURPOSE, and it must stay that way. bench_engines.cpp carries its own
// standing prohibition against being extended -- adding one data row there moved `words` +27.7 % on
// gcc/x86-64 -- and these cases could not go there anyway: that unit measures throughput over 100 KB
// corpora, which divides a fixed per-call cost away until it cannot be seen. This is the regime a
// caller in a validation loop actually pays, where the whole call is 30-300 ns and the fixed part
// dominates.
//
// WHY THE TIMED REGION IS A BATCH. One call here is comparable to the clock's own granularity, so
// timing a single call measures the clock: per-call rows read exactly 0.0 ns under one compiler and
// 42 ns under another purely from where each landed relative to the tick, and a minimum-of-samples
// estimator then reports the zero as speed. Each case therefore grows an inner repeat count until the
// timed region spans 50 us and divides the total. A row reading 0.0 ns is an unmeasured row, never a
// fast one. See docs/MEASUREMENT.md §3.3.
//
// WHY THE VOLATILE SINK. Every loop body here is invariant -- same pattern object, same subject -- so
// without a sink the scan is hoisted out of the timed loop or elided as dead, which reads as infinite
// speed. The sink is not decoration.
//
// The two engines are compared on the SAME surface with the SAME pattern text and subject; both
// compile the pattern once, outside the timing. std::regex is also the control for run-to-run drift:
// this program rebuilds nothing between the two, so if its column moves between two invocations, that
// movement bounds what may be attributed to a change in REAL.
//
//   c++ -std=c++20 -O2 -DNDEBUG -Iinclude benchmarks/bench_percall.cpp -o bench_percall
//   ./bench_percall [samples]

#include <real/compat/std/regex.hpp>

#include "bench_warmup.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <regex>
#include <string>
#include <vector>

namespace {

  //! \brief Sink for every result, so no compiler can hoist an invariant scan out of a timed loop.
  volatile std::size_t sink {0};

  /*!
   * \brief Median nanoseconds per call of \p fn, batched against the clock's granularity.
   * \param[in] fn      The one call under test; its result feeds \ref sink.
   * \param[in] samples How many batches to draw (the median is reported).
   * \return Nanoseconds per single call.
   */
  template <typename Fn>
  [[nodiscard]] double ns_per_call(Fn&& fn, int samples)
  {
    static_cast<void>(fn());
    int inner {1};
    for (; inner < (1 << 22); inner *= 2) {
      const auto probe_start {std::chrono::steady_clock::now()};
      for (int k = 0; k < inner; ++k) {
        sink = fn();
      }
      const auto span {std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - probe_start)};
      if (span.count() >= 50.0) {
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

} // namespace

int main(int argc, char** argv)
{
  bench::ramp_and_pin();
  const int         samples {argc > 1 ? std::atoi(argv[1]) : 15};
  const std::string stamp {"2026-08-10_14:52:47"};
  const std::string embedded {"log 2026-08-10_14:52:47 end"};
  const std::string padded {"   \t indented value here \n  "};

  const char* anchored {R"(^[0-9]{4}-[0-9]{2}-[0-9]{2}_[0-9]{2}:[0-9]{2}:[0-9]{2}$)"};
  const char* bare {R"([0-9]{4}-[0-9]{2}-[0-9]{2}_[0-9]{2}:[0-9]{2}:[0-9]{2})"};
  const char* trim {R"(^[\t \n\r]+|[\t \n\r]+$)"};

  const std::regex          std_anchored {anchored};
  const std::regex          std_bare {bare};
  const std::regex          std_trim {trim};
  const real::compat::regex real_anchored {anchored};
  const real::compat::regex real_bare {bare};
  const real::compat::regex real_trim {trim};

  std::smatch          std_groups;
  real::compat::smatch real_groups;

  std::printf("  %-26s %12s %12s %9s\n", "case", "std::regex", "REAL", "ratio");
  std::printf("  %-26s %12s %12s %9s\n", "--------------------------", "-----------", "-----------", "--------");

  const auto row {[&](const char* name, auto&& std_call, auto&& real_call) {
    const double std_ns {ns_per_call(std_call, samples)};
    const double real_ns {ns_per_call(real_call, samples)};
    std::printf("  %-26s %12.1f %12.1f %8.2fx\n", name, std_ns, real_ns, std_ns / real_ns);
  }};

  row("match hit (anchored)",
      [&] { return std::regex_match(stamp, std_anchored) ? 1U : 0U; },
      [&] { return real::compat::regex_match(stamp, real_anchored) ? 1U : 0U; });
  row("match reject (anchored)",
      [&] { return std::regex_match(embedded, std_anchored) ? 1U : 0U; },
      [&] { return real::compat::regex_match(embedded, real_anchored) ? 1U : 0U; });
  row("search in (unanchored)",
      [&] { return std::regex_search(embedded, std_bare) ? 1U : 0U; },
      [&] { return real::compat::regex_search(embedded, real_bare) ? 1U : 0U; });
  row("search + captures",
      [&] { static_cast<void>(std::regex_search(embedded, std_groups, std_bare)); return std_groups.size(); },
      [&] { static_cast<void>(real::compat::regex_search(embedded, real_groups, real_bare)); return real_groups.size(); });
  row("replace (trim)",
      [&] { return std::regex_replace(padded, std_trim, "").size(); },
      [&] { return real::compat::regex_replace(padded, real_trim, "").size(); });

  std::printf("\n  A ratio above 1.00x is REAL's favour. std::regex is also the drift control: it is\n"
              "  untouched by any change to REAL, so its movement between two runs bounds what may\n"
              "  be attributed to one. Both columns are the median of %d batched draws.\n", samples);
  return 0;
}
