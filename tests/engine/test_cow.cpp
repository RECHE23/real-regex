// Copy-on-write capture blocks (COW): the adversarial scenarios for the detach-on-save-when-shared
// invariant. A split shares one block across its branches; a save in one branch must copy-on-write before
// it writes, or a sibling / parked thread would see the mutation. These patterns produce WRONG captures if
// that detach is missing — so they are the teeth guarding the refcount path (the Σ-invariant assert, live
// in debug/sanitize builds, is the other half: it fires on a leaked or double-freed block).
#include <string_view>

#include <sciforge/test/framework.hpp>
#include "real/real.hpp"

using namespace std::string_view_literals;

// A save in one alternative must not corrupt the shared block a sibling alternative carries: the group-2
// save (in the taken branch) copies-on-write off the block that already holds group 1.
TEST(cow_park_then_save_sibling)
{
  const real::regex rx("(x*)(a|b)");
  const auto        m {rx.search("xxb")};
  EXPECT(m);
  EXPECT_EQ(m[1], "xx"sv); // survives the group-2 branch's save (the detach happened)
  EXPECT_EQ(m[2], "b"sv);
}

// Two adjacent groups: the first "parks" (its thread waits at the whitespace) while the second group's
// saves run in the continuation. The parked thread's captures must be untouched.
TEST(cow_two_groups_parked_then_advanced)
{
  const real::regex rx(R"((\w+)\s+(\w+))");
  const auto        m {rx.search("foo   bar")};
  EXPECT(m);
  EXPECT_EQ(m[1], "foo"sv);
  EXPECT_EQ(m[2], "bar"sv);
}

// Nested groups, each a save, sharing blocks down the split tree, across loop iterations. The last
// iteration's captures win (Python semantics), and every enclosing save must have copied-on-write.
TEST(cow_nested_split_saves_in_a_loop)
{
  const real::regex rx("((a|b)(c|d))+");
  const auto        m {rx.search("acbd")};
  EXPECT(m);
  EXPECT_EQ(m[0], "acbd"sv);
  EXPECT_EQ(m[1], "bd"sv); // group 1 = last iteration
  EXPECT_EQ(m[2], "b"sv);
  EXPECT_EQ(m[3], "d"sv);
}

// Greedy backtracking splits the run between two groups; the lower-priority division is a parked thread
// whose block must survive the higher-priority thread's saves until priority resolves.
TEST(cow_greedy_backtrack_split)
{
  const real::regex rx("(a+)(a+)");
  const auto        m {rx.search("aaaa")};
  EXPECT(m);
  EXPECT_EQ(m[1], "aaa"sv); // greedy first group takes the maximum leaving one for the second
  EXPECT_EQ(m[2], "a"sv);
}

// The empty-alternative branch shares the block with the consuming branch: the empty branch's save must
// not disturb the block the consuming branch keeps.
TEST(cow_empty_alternative_shares_block)
{
  const real::regex rx("(a|)(b)");
  EXPECT_EQ(rx.search("b")[1], ""sv);  // empty first group
  EXPECT_EQ(rx.search("b")[2], "b"sv);
  EXPECT_EQ(rx.search("ab")[1], "a"sv);
  EXPECT_EQ(rx.search("ab")[2], "b"sv);
}

// The copy-on-write pool is the sole capture mechanism, so static_regex (compile-time, zero-heap)
// now shares blocks too — the same captures must hold under constexpr evaluation.
TEST(cow_static_regex_captures_constexpr)
{
  constexpr real::static_regex<"(x*)(a|b)"> rx;
  static_assert(rx.search("xxb"));
  static_assert(rx.search("xxb")[1] == "xx"sv);
  static_assert(rx.search("xxb")[2] == "b"sv);
  constexpr real::static_regex<"(a+)(a+)"> greedy;
  static_assert(greedy.search("aaaa")[1] == "aaa"sv);
  static_assert(greedy.search("aaaa")[2] == "a"sv);
  EXPECT(true);
}
