//! Shared-DFA map lifetime: slots must not outlive their regex (audit 1.1 leak fix).
//! Proves reclamation (map size), address-reuse correctness, and concurrent create/scan/destroy.
#include <sciforge/test/framework.hpp>

#include <real/real.hpp>
#include <real/automata/onepass.hpp>

#include <atomic>
#include <string>
#include <thread>
#include <vector>

using real::detail::shared_dfa_map_size_for_test;

TEST(shared_dfa_map_reclaims_on_destroy)
{
  const std::size_t baseline {shared_dfa_map_size_for_test()};
  {
    std::vector<real::regex> live;
    live.reserve(32);
    for (int i = 0; i < 32; ++i) {
      // Distinct patterns so each owns a slot (address-keyed, one immutables each).
      live.emplace_back("(?:a{" + std::to_string(i + 1) + "})@\\w+");
      // Force shared-DFA / immutables build.
      (void)live.back().search("xxxx aaaaaaaaaaaaaaaaaaaa@host more");
    }
    EXPECT(shared_dfa_map_size_for_test() >= baseline + 32);
  }
  // All destroyed — map entries must be erased (not retained until process exit).
  EXPECT(shared_dfa_map_size_for_test() == baseline);
}

TEST(shared_dfa_address_reuse_no_contamination)
{
  // Destroy R1, allocate R2 (different pattern) many times; matches must follow R2's pattern only.
  // Multi-thread: each thread warms TLS caches then reuses the same create/destroy cycle.
  std::atomic<int> fails {0};
  auto             worker = [&]() {
                              for (int round = 0; round < 200; ++round) {
                                {
                                  real::regex r1 {R"((\w+)@(\w+))"};
                                  const auto  m1 {r1.search("user@host.com")};
                                  if (!m1.matched() || m1[0] != "user@host") {
                                    ++fails;
                                  }
                                }
                                {
                                  real::regex r2 {R"(\d{4}-\d{2}-\d{2})"};
                                  const auto  m2 {r2.search("on 2026-07-04 end")};
                                  if (!m2.matched() || m2[0] != "2026-07-04") {
                                    ++fails;
                                  }
                                  // Must NOT match the email shape via contaminated DFA.
                                  if (r2.search("user@host.com").matched()) {
                                    ++fails;
                                  }
                                }
                              }
                            };
  std::thread t1 {worker};
  std::thread t2 {worker};
  t1.join();
  t2.join();
  EXPECT(fails.load() == 0);
  // No live regex from this test — map should not retain R1/R2 slots.
  EXPECT(shared_dfa_map_size_for_test() < 4);
}

TEST(shared_dfa_concurrent_create_destroy_waves)
{
  // SECURITY.md multi-tenant shape: many short-lived compiled patterns under concurrency.
  std::atomic<int> fails {0};
  auto             wave = [&](int id) {
                            for (int i = 0; i < 80; ++i) {
                              const std::string pat {"x{" + std::to_string((id % 7) + 1) + "}y"};
                              real::regex       re  {pat};
                              std::string       hay(20, 'x');
                              hay += 'y';
                              hay.append(20, 'x');
                              if (!re.search(hay).matched()) {
                                ++fails;
                              }
                            }
                          };
  std::vector<std::thread> thr;
  thr.reserve(4);
  thr.emplace_back(wave, 0);
  thr.emplace_back(wave, 1);
  thr.emplace_back(wave, 2);
  thr.emplace_back(wave, 3);
  for (auto& t : thr) {
    t.join();
  }
  EXPECT(fails.load() == 0);
}
