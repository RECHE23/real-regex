// The /reference/ basic_regex page's Example region (the [reference] markers below):
// compiled and run by example-check, so the code the page shows is tested, never
// merely illustrative.
#include <real/real.hpp>

#include <iostream>
#include <string_view>

using namespace std::string_view_literals;

int main()
{
  // [reference]
  // Runtime compilation -- real::regex is basic_regex over dynamic storage.
  const real::regex counter {R"((\w+): (\d+))"};

  if (const auto m = counter.search("retries: 12, errors: 3")) {
    std::cout << m[1] << " = " << m[2] << "\n";  // retries = 12
  }
  for (const auto& m : counter.find_iter("retries: 12, errors: 3")) {
    std::cout << m[0] << "\n";  // each "name: value" hit, left to right
  }

  // Compile-time compilation -- static_regex carries the pattern in its type:
  // an invalid pattern is a compile error, and matching works in constexpr.
  constexpr real::static_regex<R"((\w+)@(\w+))"> email;
  static_assert(email.search("info@example.com")[2] == "example"sv);
  // [/reference]

  return counter.count_matches("retries: 12, errors: 3") == 2 ? 0 : 1;
}
