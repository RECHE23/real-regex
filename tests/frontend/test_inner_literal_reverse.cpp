//! IL.1 part 2: the prefix-reverse. From a literal candidate at h, reverse-match the prefix to recover the
//! match start. First the mechanics on known cases; the two-halves differential (oracle = the engine) follows.
#include <sciforge/test/framework.hpp>

#include <real/frontend/inner_literal_reverse.hpp>
#include <real/real.hpp>

#include <utility>
#include <vector>

#include <algorithm>
#include <cstddef>
#include <string_view>

namespace {
  std::size_t rev_start(std::string_view pattern,
                        std::string_view text,
                        std::size_t      h,
                        std::size_t      min_start = 0)
  {
    const real::detail::ast           tree {real::detail::parse(pattern, real::flags::none)};
    const real::detail::inner_literal il   {real::detail::extract_inner_literal(tree)};
    return real::detail::prefix_reverse_start(tree, il.prefix_child_count, real::flags::none, text, h, min_start);
  }
}

TEST(prefix_reverse_mechanics)
{
  // \d{4}-\d{2} on "2026-07": literal "-" at h=4, prefix \d{4} -> the match starts at 0.
  EXPECT(rev_start(R"(\d{4}-\d{2})", "2026-07", 4) == 0);
  // (\w+)@(\w+) on "abc@def": "@" at h=3, prefix (\w+) -> s=0.
  EXPECT(rev_start(R"((\w+)@(\w+))", "abc@def", 3) == 0);
  // Head literal @\w+ on "@abc": "@" at h=0, count 0 -> the reverse is the identity (s = h).
  EXPECT(rev_start(R"(@\w+)", "@abc", 0) == 0);
  // Greedy prefix: \w+@ on "xy abc@" — the "@" is at h=6; \w+ reverses only over "abc" (the space stops it),
  // so s=3, the LEFTMOST start of the enclosing match (rev-kLongest), not the nearest word char.
  EXPECT(rev_start(R"(\w+@)", "xy abc@", 6) == 3);
}

TEST(prefix_reverse_two_halves_differential)
{
  // The oracle is the engine itself. For each real match, its leftmost literal hit must reverse EXACTLY to the
  // match start (half 1). An orphan hit (in no match) must reverse to npos or to a position that is not a
  // match start (half 2) — the reverse never fabricates a match the engine did not find.
  struct testcase { std::string_view pattern; std::string_view text; };
  const testcase cases[] {
    {.pattern = R"(\d{4}-\d{2}-\d{2})", .text = "log 2026-07-04 x 2026-12-25 end"}, // multi-occurrence "-" per match
    {.pattern = R"((\w+)@(\w+))",       .text = "a@b x@y c@d word noat"},
    {.pattern = R"(\w+@)",              .text = "ab@ cd@ @ orphan@ ef@"},           // the lone " @ " is an orphan hit
    {.pattern = R"(key=(\w+))",         .text = "key=val key=x notkey= key=z"},     // "notkey=" -> the key= hit inside is in-match
    {.pattern = R"(@\w+)",              .text = "@a @bc @ @def"},                   // head literal (count 0)
  };
  for (const testcase& tc : cases) {
    const real::detail::ast           tree {real::detail::parse(tc.pattern, real::flags::none)};
    const real::detail::inner_literal il   {real::detail::extract_inner_literal(tree)};
    if (!il.found() || il.prefix_child_count < 0) {
      continue;
    }
    const std::string_view lit {reinterpret_cast<const char*>(il.bytes.data()), il.len};
    const real::regex      re  {tc.pattern};

    std::vector<std::pair<std::size_t, std::size_t>> matches;
    for (const auto& m : re.find_iter(tc.text)) {
      matches.emplace_back(m.start(), m.end());
    }

    auto in_match = [&](std::size_t h) {
                      return std::ranges::any_of(matches,
                                                 [&](const auto& m) { return h >= m.first && h + il.len <= m.second; });
                    };
    auto is_match_start = [&](std::size_t p) {
                            return std::ranges::any_of(matches, [&](const auto& m) { return m.first == p; });
                          };

    // HALF 1: each match's leftmost literal hit reverses to its start.
    for (const auto& [s, e] : matches) {
      const std::size_t h {real::detail::find_literal(tc.text, s, lit)};
      EXPECT(h != real::npos && h + il.len <= e);
      EXPECT(real::detail::prefix_reverse_start(tree, il.prefix_child_count, real::flags::none, tc.text, h, 0) == s);
    }

    // HALF 2: an orphan literal hit reverses to npos or to a non-match-start (no fabricated match).
    for (std::size_t p = 0; (p = real::detail::find_literal(tc.text, p, lit)) != real::npos; ++p) {
      if (in_match(p)) {
        continue;
      }
      const std::size_t rs {real::detail::prefix_reverse_start(tree, il.prefix_child_count, real::flags::none, tc.text, p, 0)};
      EXPECT(rs == real::npos || !is_match_start(rs));
    }
  }
}
