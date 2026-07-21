// The /reference/syntax page's Example region (the [reference] markers below):
// compiled and run by example-check, so the code the page shows is tested, never
// merely illustrative.
#include <real/real.hpp>

#include <iostream>

int main()
{
  // [reference]
  // Bounded lookarounds match in linear time -- the differentiator.
  const real::regex price {R"((?<=\$)\d+)"};  // lookbehind: digits preceded by '$'
  std::cout << price.search("cost: $42 total")[0] << "\n";  // 42

  // A possessive quantifier never gives back -- and stays linear.
  const real::regex quoted {R"("[^"]*+")"};
  std::cout << quoted.search(R"(say "hi" now)")[0] << "\n";  // "hi"
  // [/reference]

  const bool ok = price.search("cost: $42 total")[0] == std::string_view {"42"} &&
                  quoted.search(R"(say "hi" now)")[0] == std::string_view {"\"hi\""};
  return ok ? 0 : 1;
}
