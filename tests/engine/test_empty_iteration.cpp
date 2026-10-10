// A `*` or `+` whose body can match the empty string: REAL drops an iteration that comes back, without
// consuming, to an instruction already reached at that position, except a jump back to the loop's head, which
// takes the loop's exit. `re` ends the loop on every empty iteration instead (docs: div_empty_first_branch_loop).
// The answers are pinned here, on a short subject (the bounded backtracker) and on the same subject padded past
// it (the VM and the lazy DFA), which must agree.
#include <sciforge/test/framework.hpp>

#include <real/real.hpp>

#include <cstddef>
#include <string>
#include <string_view>

TEST(an_empty_iteration_after_one_that_consumed_is_dropped)
{
  const struct
  {
    std::string_view pattern;
    std::string_view subject;
    std::size_t      start;
    std::size_t      end;
  } cases[] {
    {.pattern = "(?:a?|b)+", .subject = "ab", .start = 0, .end = 2},            // re: (0,1)
    {.pattern = "(?:a?|b)*", .subject = "ab", .start = 0, .end = 2},            // re: (0,1)
    {.pattern = R"((?:\s*|,)+)", .subject = "  ,  ,x", .start = 0, .end = 6},   // re: (0,2)
    {.pattern = R"((?:\d*|[a-z])+)", .subject = "12ab", .start = 0, .end = 3},  // re: (0,2)
    {.pattern = "(?:a?|b)*", .subject = "b", .start = 0, .end = 0},             // the first iteration: as re
    {.pattern = "(a|)*", .subject = "ab", .start = 0, .end = 1},                // an empty last branch: as re
    {.pattern = "(?:a?|b){1,3}", .subject = "ab", .start = 0, .end = 1},        // bounded, unrolled: as re
    {.pattern = "(?:a|b)*", .subject = "ab", .start = 0, .end = 2},             // never empty: every engine
  };
  for (const auto& c : cases) {
    const real::regex re {c.pattern};
    for (const std::size_t pad : {std::size_t {0}, std::size_t {5000}}) {
      const std::string subject {std::string {c.subject} + std::string(pad, '~')};
      const auto        m       {re.search(subject)};
      EXPECT(m.matched());
      EXPECT_EQ(m.start(), c.start);
      EXPECT_EQ(m.end(), c.end);
    }
  }
}
