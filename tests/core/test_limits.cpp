// real::limits: each limit of one expression accepts a pattern at it and refuses one past it with error_kind::limit;
// the defaults are this build's ceilings, so the two-argument constructor and limits {} agree; a limit past a ceiling,
// or under 1, is refused; a regex_set holds every member to them. The build's ceilings keep their own kinds: a
// lookaround longer than 255 bytes stays unsupported, a program past REAL_MAX_PROGRAM_SIZE a limit.
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>

#include <sciforge/test/framework.hpp>
#include "real/real.hpp"
#include "real/regex_set.hpp"

namespace {

  //! \brief The kind \p pattern is refused with under \p bound, or none when it compiles.
  std::string refusal(std::string_view    pattern,
                      const real::limits& bound)
  {
    try {
      const real::regex compiled {pattern, real::flags::none, bound};
      return "none";
    }
    catch (const real::regex_error& error) {
      switch (error.kind()) {
        case real::error_kind::limit:       return "limit";
        case real::error_kind::unsupported: return "unsupported";
        case real::error_kind::syntax:      return "syntax";
      }
    }
    return "?";
  }

  real::limits with(std::int32_t real::limits::*field,
                    std::int32_t                value)
  {
    real::limits bound {};
    bound.*field = value;
    return bound;
  }
} // namespace

TEST(each_limit_accepts_a_pattern_at_it_and_refuses_one_past_it)
{
  EXPECT_EQ(refusal("a{5}", with(&real::limits::repeat_count, 5)), "none");
  EXPECT_EQ(refusal("a{6}", with(&real::limits::repeat_count, 5)), "limit");
  EXPECT_EQ(refusal("(a)(b)", with(&real::limits::group_count, 2)), "none");
  EXPECT_EQ(refusal("(a)(b)(c)", with(&real::limits::group_count, 2)), "limit");
  EXPECT_EQ(refusal("(?:(?:(?:a)))", with(&real::limits::nesting_depth, 3)), "none");
  EXPECT_EQ(refusal("(?:(?:(?:(?:a))))", with(&real::limits::nesting_depth, 3)), "limit");
  EXPECT_EQ(refusal("(?=abcd)a", with(&real::limits::lookaround_length, 4)), "none");
  EXPECT_EQ(refusal("(?=abcde)a", with(&real::limits::lookaround_length, 4)), "limit");
  real::limits small {};
  small.program_size = 100;
  EXPECT_EQ(refusal(std::string(20, 'x'), small), "none");
  EXPECT_EQ(refusal(std::string(150, 'x'), small), "limit");
}

TEST(the_defaults_are_the_builds_ceilings_and_keep_their_kinds)
{
  const real::regex plain   {R"((\w+)@(\w+))"};
  const real::regex bounded {R"((\w+)@(\w+))", real::flags::none, real::limits {}};
  EXPECT_EQ(plain.search("mail me@host now").str(2), bounded.search("mail me@host now").str(2));
  EXPECT_EQ(refusal("a{2000}", {}), "limit");                                  // past REAL_MAX_REPEAT_COUNT
  EXPECT_EQ(refusal("(?=" + std::string(300, 'a') + ")a", {}), "unsupported"); // past the 255-byte lookaround
  EXPECT_EQ(refusal("(?<=a+)b", {}), "unsupported");                           // unbounded, at any limit
}

TEST(a_limit_past_a_ceiling_or_under_one_is_refused)
{
  for (std::int32_t real::limits::*field : {&real::limits::repeat_count, &real::limits::group_count,
                                            &real::limits::nesting_depth, &real::limits::lookaround_length}) {
    EXPECT_EQ(refusal("a", with(field, 0)), "limit");
    EXPECT_EQ(refusal("a", with(field, 1 << 30)), "limit");
  }
  real::limits bound {};
  bound.program_size = 0;
  EXPECT_EQ(refusal("a", bound), "limit");
  bound.program_size = std::numeric_limits<std::size_t>::max();
  EXPECT_EQ(refusal("a", bound), "limit");
  bound            = {};
  bound.dfa_memory = 0;
  EXPECT_EQ(refusal("a", bound), "limit");
  bound.dfa_memory = std::numeric_limits<std::size_t>::max(); // the DFA's memory has no ceiling: it may be raised
  EXPECT_EQ(refusal("a", bound), "none");
  const real::regex unbounded {R"(\b\w+\d)", real::flags::none, bound};
  EXPECT_EQ(unbounded.search("x y ab7").str(), "ab7");
}

TEST(a_regex_set_holds_every_member_to_the_limits)
{
  const std::array<std::string_view, 3> patterns {"a{3}", "b", "c{9}"};
  try {
    const real::regex_set set {patterns, real::flags::none, with(&real::limits::repeat_count, 5)};
    EXPECT(false);
  }
  catch (const real::regex_error& error) {
    EXPECT(error.kind() == real::error_kind::limit);
    EXPECT(error.cause().find("in pattern 2 of 3") != std::string::npos);
  }
  const real::regex_set set {std::span<const std::string_view> {patterns.data(), 2}, real::flags::none,
                             with(&real::limits::repeat_count, 5)};
  EXPECT(set.size() == 2);
}
