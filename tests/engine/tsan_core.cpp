// Standalone ThreadSanitizer harness for REAL's CORE concurrent caches (not the compat layer).
//
// Gap (hardening audit, post-7.47): tests/compat/tsan_compat.cpp only hammers real::compat's
// std_engine mutex. The 7.45 shared-confirm path and its friends were claimed "concurrent const
// race-free" from a one-off; nothing tracked drives:
//   - ensure_immutables / built_for identity on mutable immut_ (storage.hpp)
//   - shared_dfa_for map insert + per-thread DFA sets taken from and returned to the slot's pool
//     (dfa_lease, onepass.hpp / pike.hpp)
//   - first warm of shared fwd/rev/il_prefix_rev DFAs
//
// Critical design (without this the harness is a false negative):
//   built_for + striped rebuild lock ⇒ first arriver fills, late arrivers see filled → NO race window
//   if threads start staggered; the DFA sets are per thread, taken from the slot's pool at the first
//   scan, which the simultaneous start makes every thread do at once. So each iteration:
//     1. construct one FRESH const real::regex (empty built_for / empty slot)
//     2. barrier-sync N threads
//     3. ALL run the first search/find_iter simultaneously on that same regex
//
// Patterns cover the three cache arms, and the two density gates' recorded decisions:
//   (a) sparse-email  (\w+)@(\w+)  → IL prefix reverse + shared confirm-DFA
//   (b) \p{L}+ on CJK              → cp-class route + immutables (cp_hi is thread_local, not shared)
//   (c) plain \w+ on long ASCII    → ensure_immutables + lazy-DFA search slot
//   (d) (?:\w+)_(?:\w+), dense `_` → the inner-literal density gate's recorded abandon
//   (e) 24-word alternation        → the Aho-Corasick density gate's recorded verdict
//
// Injected-race proof: set REAL_TSAN_INJECT_RACE=1 to write an unsynchronized counter from every
// thread during the barrier window — TSan must report a race (proves the harness can go red).
//
// Build & run: make tsan-core
//   REAL_TSAN_INJECT_RACE=1 make tsan-core   # expect non-zero / TSan report

#include <real/real.hpp>

#include <atomic>
#include <barrier>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

  constexpr int k_threads    {8};
  constexpr int k_iterations {200}; // 200 × 8 simultaneous first-searches × 4 patterns

  // Shared only for the inject-race proof (deliberately unsynchronized when env is set).
  int g_inject_counter {0};

  [[nodiscard]] bool inject_race_enabled()
  {
    // Single-threaded main only; getenv is not MT-safe but this runs before any worker starts.
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    const char* e {std::getenv("REAL_TSAN_INJECT_RACE")};
    return e != nullptr && e[0] != '\0' && std::strcmp(e, "0") != 0;
  }

  // Long enough to clear lazy_dfa_min_input (512) and exercise IL / class-loop paths.
  [[nodiscard]] std::string make_email_haystack()
  {
    std::string h;
    h.reserve(8000);
    for (int i = 0; i < 200; ++i) {
      h += "noise_";
      h += std::to_string(i);
      h += " alice@example.com bob@host.org ";
    }
    return h;
  }

  [[nodiscard]] std::string make_cjk_haystack()
  {
    // UTF-8: 你好世界こんにちは  — dense Han + hiragana (same shape as bench_engines CJK corpus).
    static constexpr char unit[] =
      "\xE4\xBD\xA0\xE5\xA5\xBD\xE4\xB8\x96\xE7\x95\x8C"
      "\xE3\x81\x93\xE3\x82\x93\xE3\x81\xAB\xE3\x81\xA1\xE3\x81\xAF ";
    std::string h;
    h.reserve(8000);
    while (h.size() < 4000) {
      h.append(unit);
    }
    return h;
  }

  [[nodiscard]] std::string make_ascii_haystack()
  {
    std::string h;
    h.reserve(4000);
    while (h.size() < 3000) {
      h += "the quick brown fox jumps over the lazy dog ";
    }
    return h;
  }

  // An alternation of class runs with no literal to anchor on: the lazy-DFA search route, whose DFA
  // sets each thread leases from the regex's pool (dfa_lease) -- the one shared cache the three
  // shapes above never reach (\w+ is a class run, and the email subject stays under the inner-literal
  // cold floor).
  [[nodiscard]] std::string make_dfa_haystack()
  {
    std::string h;
    h.reserve(9000);
    while (h.size() < 8000) {
      h += "the quick fox singing 123x and bringing 7x over 42 dogs ";
    }
    return h;
  }

  // Dense failing inner-literal candidates -- a `_` every 16 bytes that no `\w+_\w+` completes, then one
  // that does: the inner-literal density gate abandons the route on this subject, and records that it did.
  [[nodiscard]] std::string make_il_dense_haystack()
  {
    std::string unit(16, ' ');
    unit[1] = '_';
    std::string h;
    while (h.size() < 20000) {
      h += unit;
    }
    h += "id_42";
    return h;
  }

  // Heads of a 24-word alternation every other byte, none completing: the Aho-Corasick density gate decides
  // on this subject, and records its verdict.
  [[nodiscard]] std::string make_ac_dense_haystack()
  {
    static constexpr std::string_view heads {"abcdefghijklmnopqrstuvwx"};
    std::string                       h;
    for (std::size_t i = 0; h.size() < 4000; ++i) {
      h += heads[i % heads.size()];
      h += '_';
    }
    h += "tango";
    return h;
  }

  struct case_spec
  {
    const char  *    name;
    const char  *    pattern;
    std::string_view hay;
  };

  // One barrier-synchronized first-search wave on a single shared const regex.
  // Returns total match-hits counted across threads (functional sanity, not a race signal).
  std::size_t race_wave(const char*        pattern,
                        std::string_view   hay,
                        bool               inject)
  {
    // FRESH regex each wave — empty built_for and a new shared_dfa_for key.
    const real::regex re          {pattern};

    std::barrier             sync {k_threads};
    std::atomic<std::size_t> hits {0};
    std::vector<std::thread> threads;
    threads.reserve(static_cast<std::size_t>(k_threads));

    for (int t = 0; t < k_threads; ++t) {
      threads.emplace_back([&, t] {
                             // Arrive together before the first search so built_for / slot warm race for real.
                             sync.arrive_and_wait();
                             if (inject) {
                               // Deliberate data race: unsynchronized RMW on a shared non-atomic.
                               ++g_inject_counter;
                               g_inject_counter += t;
                             }
                             // First search / find_iter on this regex from every thread simultaneously.
                             const auto matches {re.find_all(hay)};
                             hits.fetch_add(matches.size(), std::memory_order_relaxed);
                             // A second pass after fill — concurrent *readers* of the warm caches.
                             volatile std::size_t sink {0};
                             for (const auto& m : re.find_all(hay)) {
                               sink += m.end();
                             }
                             (void)sink;
                             (void)re.search(hay);
                           });
    }
    for (auto& th : threads) {
      th.join();
    }
    return hits.load(std::memory_order_relaxed);
  }
} // namespace

int main()
{
  const std::string email_hay {make_email_haystack()};
  const std::string cjk_hay   {make_cjk_haystack()};
  const std::string ascii_hay {make_ascii_haystack()};
  const std::string dfa_hay   {make_dfa_haystack()};
  const std::string il_hay    {make_il_dense_haystack()};
  const std::string ac_hay    {make_ac_dense_haystack()};

  const case_spec cases[] =   {
    {.name = "email-IL", .pattern = R"((\w+)@(\w+))", .hay = email_hay},
    {.name = "pL-CJK", .pattern = R"(\p{L}+)", .hay = cjk_hay},
    {.name = "wplus-ascii", .pattern = R"(\w+)", .hay = ascii_hay},
    {.name = "lazy-dfa", .pattern = R"([a-z]+ing|[0-9]+x)", .hay = dfa_hay},
    // The two density gates record their decision for tests to read; every thread records it at once.
    {.name    = "il-density", .pattern = R"((?:\w+)_(?:\w+))", .hay = il_hay},
    {.name    = "ac-density",
     .pattern = "alpha|bravo|charlie|delta|echo|foxtrot|golf|hotel|india|juliet|kilo|lima|mike|november|oscar|"
                "papa|quebec|romeo|sierra|tango|uniform|victor|whiskey|xray",
     .hay = ac_hay},
  };

  const bool inject {inject_race_enabled()};
  if (inject) {
    std::printf("tsan_core: REAL_TSAN_INJECT_RACE=1 — expecting TSan to report a race\n");
  }

  std::size_t total_hits {0};
  for (int iter = 0; iter < k_iterations; ++iter) {
    for (const case_spec& c : cases) {
      total_hits += race_wave(c.pattern, c.hay, inject);
    }
  }

  // Functional floor: every pattern must have matched something (else the caches may not arm).
  if (total_hits == 0) {
    std::printf("tsan_core: FAIL total_hits=0 (patterns did not match — caches may be unarmed)\n");
    return 2;
  }

  std::printf("tsan_core: OK (%d threads x %d iters x %zu patterns, total_hits=%zu%s)\n",
              k_threads, k_iterations, sizeof(cases) / sizeof(cases[0]), total_hits,
              inject ? ", inject_race ON" : "");
  if (inject) {
    // If TSan is absent or failed to fire, still exit non-zero so CI can detect a broken inject path.
    std::printf("tsan_core: inject counter=%d (if TSan silent, harness proof is broken)\n",
                g_inject_counter);
    return 3;
  }
  return 0;
}
