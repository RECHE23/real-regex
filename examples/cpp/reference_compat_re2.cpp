// The /reference/ compat-re2 page's Example region (the [reference] markers below):
// compiled and run by example-check, so the code the page shows is tested, never
// merely illustrative.
#include <real/compat/re2/re2.hpp>

#include <iostream>
#include <string>

int main()
{
  // [reference]
  using real::compat::re2::RE2;

  // The RE2 statics, with typed Arg extraction -- one output per capture group.
  std::string user;
  std::string host;
  const bool  hit = RE2::PartialMatch("info@example.com", R"((\w+)@(\w+))", &user, &host);
  std::cout << hit << ": " << user << " at " << host << "\n";  // 1: info at example

  // The instance surface: ok() is the no-exception contract.
  const RE2 number {R"(\d+)"};
  std::cout << number.ok() << "\n";             // 1
  // [/reference]

  return (hit && user == "info" && host == "example" && number.ok()) ? 0 : 1;
}
