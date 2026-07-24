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
  std::sort(v.begin(), v.end(), [](const real::regex& a, const real::regex& b) {
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
