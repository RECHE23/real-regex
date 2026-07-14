// Standalone ThreadSanitizer smoke for real::compat (run by `make tsan`).
//
// Makes the "concurrent const operations on one shared regex are race-free" claim reproducible from
// the repository: several threads share two real-backed regex objects and hammer the operations that
// lazily build the cached std engine (a constraining match flag routes search to std; a nullable
// replace routes to std). std_engine() builds lazy_std_ under a static build mutex and returns the
// reference under the same lock, so there is no data race on the mutable member.
//
// This is a standalone TU on purpose: it does not link the test framework or test_static.cpp (whose
// non-atomic global operator-new counter, for the zero-allocation tests, would itself race under any
// concurrent allocation — a harness artifact, not a library race). Built with -fsanitize=thread.
//
// Build & run: clang++ -std=c++20 -O1 -g -Iinclude -fsanitize=thread tests/tsan_compat.cpp -o tsan && ./tsan

#include <real/compat/std/regex.hpp>

#include <atomic>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

namespace rc = real::compat;

int main()
{
  const rc::regex  nonnull(R"(\w+)"); // non-nullable real-backed: a constraining flag routes to std
  const rc::regex  nullab(R"(\w*)");  // nullable real-backed: replace routes to std
  std::atomic<int> matches              {0};
  std::atomic<int> replaced             {0};

  constexpr int            thread_count {8};
  constexpr int            iterations   {500};
  std::vector<std::thread> threads;
  threads.reserve(thread_count);
  for (int t = 0; t < thread_count; ++t) {
    threads.emplace_back([&] {
                           for (int i = 0; i < iterations; ++i) {
                             const std::string subject {"hello world"};
                             rc::smatch        m;
                             if (rc::regex_search(subject, m, nonnull, rc::regex_constants::match_not_bol)) {
                               matches.fetch_add(1);
                             }
                             if (!rc::regex_replace(subject, nullab, std::string("x")).empty()) {
                               replaced.fetch_add(1);
                             }
                           }
                         });
  }
  for (std::thread& th : threads) {
    th.join();
  }

  const int expected {thread_count * iterations};
  if (matches.load() != expected || replaced.load() != expected) {
    std::printf("tsan_compat: FAIL matches=%d replaced=%d expected=%d\n", matches.load(),
                replaced.load(), expected);
    return 1;
  }
  std::printf("tsan_compat: OK (%d threads x %d iters, matches=%d replaced=%d)\n", thread_count,
              iterations, matches.load(), replaced.load());
  return 0;
}
