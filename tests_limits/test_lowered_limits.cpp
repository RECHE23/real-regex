// The resource limits a build may lower (core/config.hpp): this binary is compiled with every REAL_MAX_* macro
// below its default (CMakeLists.txt, real_limits_bin) and checks that each lowered limit is the one enforced,
// with a pattern just inside it still accepted. A separate program: every translation unit of one program
// must see the same limits.
#include <sciforge/test/framework.hpp>

#include <real/dfa.hpp>
#include <real/real.hpp>

#include <span>
#include <string>
#include <vector>

namespace {

  bool rejected(const std::string& pattern)
  {
    try {
      static_cast<void>(real::regex {pattern});
      return false;
    }
    catch (const real::regex_error&) {
      return true;
    }
  }

  std::string nested(int depth)
  {
    return std::string(static_cast<std::size_t>(depth), '(') + "a" + std::string(static_cast<std::size_t>(depth), ')');
  }
} // namespace

TEST(the_lowered_limits_are_the_ones_in_force)
{
  EXPECT_EQ(real::detail::max_repeat_count, 10);
  EXPECT_EQ(real::detail::max_group_count, 5);
  EXPECT_EQ(real::detail::max_nesting_depth, 8);
  EXPECT_EQ(real::detail::max_lookaround_length, 4);
  EXPECT_EQ(real::detail::max_program_size, 256U);
  EXPECT_EQ(real::detail::max_dfa_states, 16U);
}

TEST(each_lowered_limit_rejects_just_past_itself)
{
  EXPECT(!rejected("a{10}"));
  EXPECT(rejected("a{11}"));
  EXPECT(!rejected("(a)(b)(c)(d)(e)"));
  EXPECT(rejected("(a)(b)(c)(d)(e)(f)"));
  EXPECT(!rejected(nested(4)));
  EXPECT(rejected(nested(20)));
  EXPECT(!rejected("x(?=abcd)"));
  EXPECT(rejected("x(?=abcde)"));
  EXPECT(!rejected("[a-z]{10}"));
  EXPECT(rejected("(?:[a-z]{10}){10}(?:[0-9]{10}){10}(?:[A-Z]{10}){10}"));
}

// A lowered lookaround bound is named by its macro, not by the default's 255.
TEST(a_lowered_lookaround_bound_names_its_macro)
{
  for (const char* pattern : {"x(?=abcde)", "(?<=a.*)b"}) {
    try {
      static_cast<void>(real::regex {pattern});
      EXPECT(false);
    }
    catch (const real::regex_error& error) {
      EXPECT(std::string {error.what()}.find("REAL_MAX_LOOKAROUND_LENGTH") != std::string::npos);
    }
  }
}

TEST(the_lowered_dfa_state_cap_throws)
{
  const std::vector<real::regex> small {real::regex {"ab|cd"}};
  bool built {false};
  try {
    static_cast<void>(real::dfa {std::span<const real::regex>(small)});
    built = true;
  }
  catch (const real::dfa_error&) {
  }
  EXPECT(built);
  const std::vector<real::regex> wide {real::regex {"(?:a|b)*a(?:a|b)(?:a|b)(?:a|b)(?:a|b)(?:a|b)"}};
  EXPECT_THROWS(static_cast<void>(real::dfa {std::span<const real::regex>(wide)}), real::dfa_error);
}

int main()
{
  return test::run_all();
}
