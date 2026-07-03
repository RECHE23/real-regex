// Micro-benchmark for the small-first-byte-set memchr-cascade (OPT-A). Times the prefilter scan on
// small-set patterns in the two regimes the veto gate cares about: SPARSE (the set bytes are rare, so
// the scan runs far — the cascade should win big) and DENSE (the set bytes are frequent, so candidates
// are close — where k memchr per candidate could lose to the one-test-per-byte bitmap loop). Also a
// single-byte and a literal-prefix pattern (H/B) that must be untouched. Run the same binary before and
// after the change (git stash) to get the before/after ratio.
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#include "real/real.hpp"

namespace {

  double time_search(const real::regex& re, const std::string& text, int iters)
  {
    const auto start = std::chrono::steady_clock::now();
    std::size_t sink {0};
    for (int i = 0; i < iters; ++i) {
      for (const auto& m : re.find_iter(text)) {
        sink += m.end();
      }
    }
    const auto end = std::chrono::steady_clock::now();
    const double ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    std::fprintf(stderr, "sink=%zu\n", sink); // defeat dead-code elimination
    return ns / iters;
  }

  std::string filled(std::size_t n, char c) { return std::string(n, c); }

  // Text of length n over 'a', with the given set bytes sprinkled every `period` positions.
  std::string sprinkled(std::size_t n, const std::string& set, std::size_t period)
  {
    std::string s(n, 'a');
    for (std::size_t i = 0, k = 0; i < n; i += period, ++k) {
      s[i] = set[k % set.size()];
    }
    return s;
  }

} // namespace

int main()
{
  constexpr std::size_t N {1u << 20}; // 1 MiB
  const int             iters {200};

  struct row
  {
    const char* label;
    real::regex re;
    std::string text;
  };
  std::vector<row> rows;
  rows.push_back({"sparse-2  [xz] on 'a'*1M",     real::regex("[xz]"),   filled(N, 'a')});
  rows.push_back({"sparse-4  [wxyz] on 'a'*1M",   real::regex("[wxyz]"), filled(N, 'a')});
  rows.push_back({"dense-2   [xz] every 8",       real::regex("[xz]"),   sprinkled(N, "xz", 8)});
  rows.push_back({"dense-4   [wxyz] every 8",     real::regex("[wxyz]"), sprinkled(N, "wxyz", 8)});
  rows.push_back({"dense2-4  [xz] every 4",       real::regex("[xz]"),   sprinkled(N, "xz", 4)});
  rows.push_back({"H single  x on 'a'*1M",        real::regex("x"),      filled(N, 'a')});
  rows.push_back({"B prefix  abcd on 'a'*1M",     real::regex("abcd"),   filled(N, 'a')});

  for (auto& r : rows) {
    const double per = time_search(r.re, r.text, iters);
    std::printf("%-28s %10.1f us/pass\n", r.label, per / 1000.0);
  }
  return 0;
}
