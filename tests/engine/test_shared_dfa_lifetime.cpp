//! Shared-DFA map lifetime: slots must not outlive their regex (audit 1.1 leak fix).
//! Proves reclamation (map size), address-reuse correctness, and concurrent create/scan/destroy.
#include <sciforge/test/framework.hpp>

#include <real/real.hpp>
#include <real/automata/onepass.hpp>

#include <atomic>
#include <cstdint>
#include <optional>
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

// Slot ownership is per slot (shared_dfa_slot::owner), not a process-wide epoch: a thread's last-hit
// cache is valid exactly while the cached slot still names the regex being asked about. These two pin
// the cases that ownership -- rather than a global counter -- has to get right, and that the tests above
// (one regex at a time, warm) do not reach: a cache that changes slot on every call, and a destruction
// that must leave an unrelated live regex untouched. Cheap by construction (no thread fan-out): what is
// under test is the cache's identity check, not a race, and tsan_core covers the concurrent side.
TEST(shared_dfa_interleaved_regexes_never_cross_slots)
{
  // Both route through the shared DFAs, on the same subject, with different answers. Interleaving them
  // makes the thread-local slot cache miss and be replaced on every single call — if it ever served one
  // regex's slot for the other, the counts below would move.
  const std::string subject     {"ay@bee 2026-07-04 cee@dee 1999-12-31 eff@gee"};
  real::regex       email       {R"((\w+)@(\w+))"};
  real::regex       date        {R"(\d{4}-\d{2}-\d{2})"};

  const std::size_t email_alone {email.count_matches(subject)};
  const std::size_t date_alone  {date.count_matches(subject)};
  EXPECT_EQ(email_alone, 3U);
  EXPECT_EQ(date_alone, 2U);

  for (int i = 0; i < 50; ++i) {
    EXPECT_EQ(email.count_matches(subject), email_alone);
    EXPECT_EQ(date.count_matches(subject), date_alone);
  }
}

TEST(shared_dfa_destroying_one_regex_leaves_another_warm_one_correct)
{
  const std::string subject  {"ay@bee 2026-07-04 cee@dee 1999-12-31 eff@gee"};
  real::regex       survivor {R"((\w+)@(\w+))"};
  const std::size_t want     {survivor.count_matches(subject)}; // warm: slot built, cached by this thread
  EXPECT_EQ(want, 3U);

  for (int i = 0; i < 50; ++i) {
    {
      // A short-lived regex of its own: its destruction retires ITS slot, and must not disturb the
      // survivor's (which the old process-wide epoch invalidated wholesale — correct, but the reason
      // every thread fell back to the global map mutex).
      real::regex transient {R"(\d{4}-\d{2}-\d{2})"};
      EXPECT_EQ(transient.count_matches(subject), 2U);
    }
    EXPECT_EQ(survivor.count_matches(subject), want);
  }
}

// Address reuse, GUARANTEED. The test above cycles two stack-scoped regexes, which the compiler is free
// to give distinct stack slots — measured, it reuses the immutables address ~never, so it does not in
// fact reach the case its name describes. Nor does going through the heap portably: under ASan the
// quarantine holds freed blocks back, so malloc never recycles and the case silently stops being tested
// (it did — that is how this version came to exist). An `optional` pins it: its storage is a fixed
// buffer, so emplace-after-reset puts the next regex — and its immutables — at the SAME address every
// cycle, on every allocator and under every sanitizer.
//
// That address is what slot ownership has to get right: the same thread asks for it again while its
// last-hit cache still holds the RETIRED slot, so a retired slot whose owner were left set would be
// served for the new pattern.
TEST(shared_dfa_reused_immutables_address_serves_a_fresh_slot)
{
  const std::string subject    {"ay@bee 2026-07-04 cee@dee 1999-12-31 eff@gee"};
  const char* const email_pat  {R"((\w+)@(\w+))"};
  const char* const date_pat   {R"(\d{4}-\d{2}-\d{2})"};
  const std::size_t want_email {real::regex(email_pat).count_matches(subject)};
  const std::size_t want_date  {real::regex(date_pat).count_matches(subject)};
  EXPECT_EQ(want_email, 3U);
  EXPECT_EQ(want_date, 2U);

  const std::size_t          baseline {shared_dfa_map_size_for_test()};
  std::optional<real::regex> slot_reuse;
  slot_reuse.emplace(email_pat);
  EXPECT_EQ(slot_reuse->count_matches(subject), want_email); // warm: this thread now caches that slot
  const void* const fixed {static_cast<const void*>(slot_reuse->raw_program().immut)};

  for (int i = 0; i < 200; ++i) {
    const bool date_turn {(i % 2) == 0};
    slot_reuse.reset();                                       // retires this address's slot
    slot_reuse.emplace(date_turn ? date_pat : email_pat);     // same storage -> same immutables address
    // The premise, asserted rather than hoped for: this really is the reused-address case.
    EXPECT(static_cast<const void*>(slot_reuse->raw_program().immut) == fixed);
    EXPECT_EQ(slot_reuse->count_matches(subject), date_turn ? want_date : want_email);
  }
  slot_reuse.reset();
  EXPECT_EQ(shared_dfa_map_size_for_test(), baseline);
}

// --- dfa_lease: one thread's DFA set per regex, no lock during a scan ---------------------------------

TEST(dfa_lease_keeps_this_threads_set_between_leases)
{
  real::detail::regex_immutables      immut {};
  const real::detail::shared_dfa_set* first {nullptr};
  {
    const real::detail::dfa_lease lease {&immut};
    first = &*lease;
  }
  const real::detail::dfa_lease again {&immut};
  EXPECT(&*again == first); // the same set: no pool round-trip on the same regex
}

TEST(dfa_lease_nested_gets_its_own_set_and_returns_it)
{
  real::detail::regex_immutables      outer_immut {};
  real::detail::regex_immutables      inner_immut {};
  const real::detail::dfa_lease       outer       {&outer_immut};
  const real::detail::shared_dfa_set* nested_set  {nullptr};
  {
    // A lease taken while this thread's cached set is held -- the same regex or another -- must not
    // hand the outer scan's set away.
    const real::detail::dfa_lease same  {&outer_immut};
    EXPECT(&*same != &*outer);
    const real::detail::dfa_lease other {&inner_immut};
    EXPECT(&*other != &*outer);
    EXPECT(&*other != &*same);
    nested_set = &*same;
  }
  // The nested set went back to the outer regex's pool; the next nested lease on it reuses it.
  const real::detail::dfa_lease reused {&outer_immut};
  EXPECT(&*reused == nested_set);
}

TEST(dfa_lease_a_thread_returns_its_set_when_it_ends)
{
  real::detail::regex_immutables      immut      {};
  const real::detail::shared_dfa_set* thread_set {nullptr};
  std::thread                         user       {[&] {
                                                    const real::detail::dfa_lease lease {&immut};
                                                    thread_set = &*lease;
                                                  }};
  user.join();
  // This thread's cached set belongs to another regex (or none), so it takes from the pool: the set
  // the finished thread gave back.
  real::detail::regex_immutables elsewhere {};
  {
    const real::detail::dfa_lease move_away {&elsewhere};
  }
  const real::detail::dfa_lease lease {&immut};
  EXPECT(&*lease == thread_set);
}

TEST(dfa_lease_clears_a_set_built_for_an_earlier_program)
{
  real::detail::regex_immutables immut  {};
  std::uint64_t                  before {0};
  {
    const real::detail::dfa_lease lease {&immut};
    before = lease->generation;
  }
  real::detail::reset_shared_dfas(&immut); // what a rebuild at the same address does
  const real::detail::dfa_lease lease {&immut};
  EXPECT(lease->generation == before + 1);
  EXPECT(!lease->fwd.has_value());
  EXPECT(!lease->rev.has_value());
  EXPECT(!lease->il_prefix_rev.has_value());
}

// Threads sharing one regex each scan through their own DFAs: the answers stay the single-thread
// ones, and none waits on another's scan.
TEST(shared_regex_scans_in_parallel_with_the_same_answers)
{
  const real::regex  re {"[a-z]+ing|[0-9]+x"};
  std::string        text;
  while (text.size() < 256 * 1024) {
    text += "the quick fox singing 123x and bringing 7x over 42 dogs ";
  }
  const std::size_t        expected {re.count_matches(text)};
  std::atomic<int>         wrong    {0};
  std::vector<std::thread> pool;
  for (int t = 0; t < 8; ++t) {
    pool.emplace_back([&] {
                        for (int round = 0; round < 4; ++round) {
                          if (re.count_matches(text) != expected) {
                            ++wrong;
                          }
                        }
                      });
  }
  for (std::thread& th : pool) {
    th.join();
  }
  EXPECT(wrong.load() == 0);
}
