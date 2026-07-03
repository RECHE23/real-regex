// OPT-B attribution harness: measure the PER-MATCH cost of find_iter on match-dense text, with text
// construction EXCLUDED from timing (the P0 lesson — the earlier harness counted text setup). Reports
// nanoseconds per match, and how that scales with the number of capture groups (the slot-copy / alloc
// size). A steep group slope means advance()'s per-match slots allocation + working->slots copy is the
// cost worth dieting; a flat slope means the per-match cost is the VM itself and the copies are noise.
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#include "real/real.hpp"

namespace {

  double ns_per_match(const real::regex& re, const std::string& text, int iters, std::size_t& matches)
  {
    // Warm up + count matches once (excluded from the timed region below).
    std::size_t count {0};
    for (const auto& m : re.find_iter(text)) {
      (void)m;
      ++count;
    }
    matches = count;

    std::size_t sink {0};
    const auto  start = std::chrono::steady_clock::now();
    for (int i = 0; i < iters; ++i) {
      for (const auto& m : re.find_iter(text)) {
        sink += m.end();
      }
    }
    const auto   end = std::chrono::steady_clock::now();
    const double ns  = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    std::fprintf(stderr, "sink=%zu\n", sink);
    return ns / (static_cast<double>(iters) * static_cast<double>(count == 0 ? 1 : count));
  }

} // namespace

int main()
{
  // Match-dense: every position is a one-char match, so per-match overhead dominates the run.
  const std::string text(1u << 18, 'a'); // 256 Ki matches, pre-built (NOT timed)
  const int         iters {40};

  struct row
  {
    const char* label;
    real::regex re;
  };
  std::vector<row> rows;
  rows.push_back({"g0  a        (2 slots)",  real::regex("a")});
  rows.push_back({"g1  (a)      (4 slots)",  real::regex("(a)")});
  rows.push_back({"g2  ((a))    (6 slots)",  real::regex("((a))")});
  rows.push_back({"g4  ((((a))))(10 slots)", real::regex("((((a))))")});
  rows.push_back({"g8  8-nested (18 slots)", real::regex("((((((((a))))))))")});

  for (auto& r : rows) {
    std::size_t   matches {0};
    const double  per = ns_per_match(r.re, text, iters, matches);
    std::printf("%-24s %7.2f ns/match  (%zu matches)\n", r.label, per, matches);
  }
  return 0;
}
