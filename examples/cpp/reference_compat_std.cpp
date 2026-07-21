// The /reference/ compat-std page's Example region (the [reference] markers below):
// compiled and run by example-check, so the code the page shows is tested, never
// merely illustrative.
#include <real/compat/std/regex.hpp>

#include <iostream>
#include <string>

int main()
{
  // [reference]
  namespace rc = real::compat;

  const rc::regex   date {R"((\d{4})-(\d{2}))"};
  const std::string text {"released 2026-07-21"};

  rc::smatch m;
  if (rc::regex_search(text, m, date)) {
    std::cout << m[1].str() << "/" << m[2].str() << "\n";  // 2026/07
  }
  // [/reference]

  return m.size() == 3 ? 0 : 1;
}
