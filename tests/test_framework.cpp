// Smoke tests for the test framework itself.
#include <stdexcept>
#include <string>

#include <sciforge/test/framework.hpp>

TEST(framework_expect_and_eq)
{
  EXPECT(1 + 1 == 2);
  EXPECT_EQ(2 + 2, 4);
  EXPECT_EQ(std::string("abc"), "abc");
}

TEST(framework_expect_throws)
{
  EXPECT_THROWS(throw std::runtime_error("boom"), std::runtime_error);
}
