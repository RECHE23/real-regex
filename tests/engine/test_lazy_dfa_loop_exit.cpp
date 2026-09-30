//! A loop whose body can match empty ends where an empty iteration reaches it again at one position: the VM takes
//! the loop's exit there, in its priority place, before a branch that would consume. The lazy DFA builds its
//! states by the same closure, and these hold it to the VM on subjects long enough for its route.
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include <sciforge/test/framework.hpp>
#include "real/real.hpp"

namespace {

  using span_list = std::vector<std::pair<std::size_t, std::size_t>>;

  span_list spans_of(const real::regex& re,
                     const std::string& s)
  {
    span_list out;
    for (const auto& m : re.find_iter(s)) {
      out.emplace_back(m.start(), m.end());
    }
    return out;
  }

  //! \brief The spans of \p pattern on \p s from the VM alone.
  span_list vm_spans(const std::string& pattern,
                     const std::string& s)
  {
    real::detail::lazy_dfa_route_disabled()          = true;
    real::detail::bounded_backtrack_route_disabled() = true;
    const real::regex vm  {pattern};
    span_list         out {spans_of(vm, s)};
    real::detail::lazy_dfa_route_disabled()          = false;
    real::detail::bounded_backtrack_route_disabled() = false;
    return out;
  }

  struct generator
  {
    std::uint32_t state {0x2545F491U};

    std::uint32_t next()
    {
      state ^= state << 13U;
      state ^= state >> 17U;
      state ^= state << 5U;
      return state;
    }

    //! \brief A literal, an empty group, or a group of two to four alternatives under a quantifier.
    std::string atom(int depth)
    {
      if (depth > 2 || next() % 10U < 4U) {
        const char* const leaves[] {"a", "b", "c", "ab", "", "(?:)"};
        return leaves[next() % 6U];
      }
      std::string         out {"(?:"};
      const std::uint32_t n   {2U + (next() % 3U)};
      for (std::uint32_t i {0}; i < n; ++i) {
        out += (i == 0U ? "" : "|") + atom(depth + 1);
      }
      const char* const quantifiers[] {"+", "*", "?", "{2,}", "+?", "*?", ""};
      return out + ")" + quantifiers[next() % 7U];
    }
  };
} // namespace

// The shape that showed it: at the `b` of `ccb`, the empty branch reaches the loop's head again and ends the
// loop before the `b` branch could extend it.
TEST(lazy_dfa_loop_exits_on_an_empty_iteration)
{
  std::string s;
  while (s.size() < 800U) {
    s += "ccb ";
  }
  const real::regex re {"(?:c||b)+"};
  const auto        m  {re.search(s)};
  EXPECT_EQ(m.start(), 0U);
  EXPECT_EQ(m.end(), 2U);
  EXPECT(spans_of(re, s) == vm_spans("(?:c||b)+", s));
}

// Random loops over alternations with empty branches, with and without a leading `\b` or a multiline `$` (the
// closure with assertions), over subjects past the lazy DFA's floor: every walk answers as the VM alone.
TEST(lazy_dfa_loops_answer_as_the_vm)
{
  generator   g;
  std::size_t cases {0};
  std::size_t wrong {0};
  for (int round {0}; round < 600; ++round) {
    std::string pattern {g.atom(0) + g.atom(1)};
    if (g.next() % 4U == 0U) {
      pattern.insert(0, R"(\b)");
    }
    if (g.next() % 5U == 0U) {
      pattern.insert(0, "(?m)");
      pattern += "$";
    }
    std::string       s;
    const char* const units[] {"a", "b", "c", " ", "ab", "\n"};
    while (s.size() < 700U) {
      s += units[g.next() % 6U];
    }
    const real::regex re   {pattern};
    const span_list   got  {spans_of(re, s)};
    const span_list   want {vm_spans(pattern, s)};
    if (got != want) {
      if (wrong < 5U) {
        std::printf("/%s/: %zu spans, the VM %zu\n", pattern.c_str(), got.size(), want.size());
      }
      ++wrong;
    }
    ++cases;
  }
  EXPECT_EQ(cases, 600U);
  EXPECT_EQ(wrong, 0U);
}
