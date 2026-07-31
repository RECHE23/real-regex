// Assign-onto-warmed regex must rebuild immutables for the new program (not return 0).
// Regression guard for the spent-once_flag bug: erase/sort/swap/map/re=regex(other) after a
// routed scan all hit regex_immutables::operator= (no-op body + built_for invalidate).
#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include <sciforge/test/framework.hpp>
#include "real/real.hpp"

namespace {

  std::string key_corpus(int n = 4000)
  {
    std::string s;
    s.reserve(static_cast<std::size_t>(n) * 16);
    for (int i = 0; i < n; ++i) {
      s += "xx key=foo yy ";
    }
    return s;
  }

  std::size_t matches(const real::regex&  r,
                      const std::string&  text)
  {
    return r.count_matches(text);
  }
} // namespace

TEST(assign_onto_warmed_rebuilds_immutables)
{
  const std::string corpus {key_corpus()};
  real::regex       r      {R"((\w+)@(\w+))"};
  (void) matches(r, corpus); // warm email route
  r = real::regex {R"(key=(\w+))"};
  EXPECT_EQ(matches(r, corpus), 4000U);
}

TEST(cold_assign_still_ok)
{
  const std::string corpus {key_corpus()};
  real::regex       r      {R"((\w+)@(\w+))"}; // never scanned
  r = real::regex {R"(key=(\w+))"};
  EXPECT_EQ(matches(r, corpus), 4000U);
}

TEST(vector_erase_move_assigns_warmed_tail)
{
  const std::string        corpus {key_corpus()};
  std::vector<real::regex> v;
  v.emplace_back(R"((\w+)@(\w+))");
  v.emplace_back(R"(key=(\w+))");
  (void) matches(v[0], corpus);
  (void) matches(v[1], corpus);
  v.erase(v.begin()); // move-assigns key= into slot 0
  EXPECT_EQ(v.size(), 1U);
  EXPECT_EQ(matches(v[0], corpus), 4000U);
}

TEST(sort_two_warmed_regexes)
{
  const std::string        corpus {key_corpus()};
  std::vector<real::regex> v;
  v.emplace_back(R"(key=(\w+))");
  v.emplace_back(R"((\w+)@(\w+))");
  (void) matches(v[0], corpus);
  (void) matches(v[1], corpus);
  // Order by pattern text so move/assign fire under sort.
  std::ranges::sort(v, [](const real::regex& a, const real::regex& b) {
                      return a.pattern() < b.pattern();
                    });
  // Whichever ends as key= must still count.
  std::size_t key_hits {0};
  for (const auto& r : v) {
    if (r.pattern() == R"(key=(\w+))") {
      key_hits = matches(r, corpus);
    }
  }
  EXPECT_EQ(key_hits, 4000U);
}

TEST(swap_both_directions_after_warm)
{
  const std::string corpus {key_corpus()};
  real::regex       a      {R"((\w+)@(\w+))"};
  real::regex       b      {R"(key=(\w+))"};
  (void) matches(a, corpus);
  (void) matches(b, corpus);
  std::swap(a, b);
  EXPECT_EQ(matches(a, corpus), 4000U); // a is now key=
  EXPECT_EQ(matches(b, corpus), 0U);    // b is email; corpus has none
  std::swap(a, b);
  EXPECT_EQ(matches(b, corpus), 4000U);
  EXPECT_EQ(matches(a, corpus), 0U);
}

TEST(map_overwrite_warmed_entry)
{
  const std::string          corpus {key_corpus()};
  std::map<int, real::regex> m;
  m.insert({1, real::regex {R"((\w+)@(\w+))"}});
  (void) matches(m.at(1), corpus);
  m.insert_or_assign(1, real::regex {R"(key=(\w+))"});
  EXPECT_EQ(matches(m.at(1), corpus), 4000U);
}

TEST(copy_construction_after_warm_is_independent)
{
  const std::string corpus {key_corpus()};
  real::regex       a      {R"(key=(\w+))"};
  (void) matches(a, corpus);
  real::regex b            {a};         // copy-construct: fresh immutables
  EXPECT_EQ(matches(b, corpus), 4000U);
  a = real::regex {R"((\w+)@(\w+))"};
  EXPECT_EQ(matches(b, corpus), 4000U); // b unchanged
}

// The Aho-Corasick automaton is cached per REGEX (regex_immutables), not per state. It lived on
// the state until that was measured: a state is fresh per search(), so crossing the branch
// threshold rebuilt it on every call -- 29.5 us and 584 allocations against 0.14 us for an
// alternation below the gate. These pin the cache's contract, not its speed: same answers across
// repeated searches, across a copy, and after assigning a DIFFERENT alternation onto a warmed one
// (the identity guard, which a shared cache gets wrong by returning the previous program's
// automaton).
namespace {

  std::string alt_pattern(char first,
                          int  n)
  {
    std::string p;
    for (int i = 0; i < n; ++i) {
      if (i != 0) {
        p += '|';
      }
      p += first;
      p += "ord";
      p += static_cast<char>('a' + i);
    }
    return p;
  }

  std::string alt_corpus(char first,
                         int  n = 300)
  {
    std::string s;
    for (int i = 0; i < n; ++i) {
      s += "zz ";
      s += first;
      s += "orda ";
      s += first;
      s += "ordc qq ";
    }
    return s;
  }
} // namespace

TEST(aho_corasick_cache_is_per_regex_not_per_search)
{
  const std::string corpus {alt_corpus('w')};
  const real::regex re     {alt_pattern('w', 24)}; // >= ac_branch_threshold: takes the AC route
  const std::size_t first  {matches(re, corpus)};
  EXPECT(first > 0U);
  // Repeated searches must agree with the first: a per-regex cache is reused, a per-state one was
  // rebuilt, and either way the ANSWER may not move.
  for (int i = 0; i < 5; ++i) {
    EXPECT_EQ(matches(re, corpus), first);
  }
}

TEST(aho_corasick_cache_survives_copy_and_is_independent)
{
  const std::string corpus {alt_corpus('w')};
  real::regex       a      {alt_pattern('w', 24)};
  const std::size_t n      {matches(a, corpus)};
  EXPECT(n > 0U);
  const real::regex b      {a};           // copy after warm: fresh immutables, rebuilds its own automaton
  EXPECT_EQ(matches(b, corpus), n);
  a = real::regex {alt_pattern('v', 24)}; // a now matches nothing in this corpus
  EXPECT_EQ(matches(a, corpus), 0U);
  EXPECT_EQ(matches(b, corpus), n);       // b untouched by a's reassignment
}

TEST(aho_corasick_assign_onto_warmed_rebuilds_for_the_new_alternation)
{
  const std::string w_corpus {alt_corpus('w')};
  const std::string v_corpus {alt_corpus('v')};
  real::regex       re       {alt_pattern('w', 24)};
  const std::size_t w_hits   {matches(re, w_corpus)};
  EXPECT(w_hits > 0U);
  re = real::regex {alt_pattern('v', 24)}; // same shape, different literals: the guard must invalidate
  EXPECT_EQ(matches(re, v_corpus), w_hits);
  EXPECT_EQ(matches(re, w_corpus), 0U);    // a stale automaton would still find the w-words here
}
