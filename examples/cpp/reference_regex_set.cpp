// The /reference/ regex_set page's Example region (the [reference] markers below):
// compiled and run by example-check, so the code the page shows is tested, never
// merely illustrative.
#include <real/regex_set.hpp>

#include <cstddef>
#include <iostream>
#include <vector>

int main()
{
  // [reference]
  // Compile the patterns together; ask which of them match a subject.
  const real::regex_set routes {"^/api/", R"(\.json$)", "^/static/"};

  std::cout << routes.size() << "\n";                 // 3
  std::cout << routes.is_match("/api/users") << "\n"; // 1 -- any-match, stops at the first hit

  for (const std::size_t i : routes.which("/static/app.json")) {
    std::cout << i << "\n";                           // 1 then 2 -- construction order
  }
  // [/reference]

  const bool ok = routes.which("/static/app.json") == std::vector<std::size_t> {1, 2};
  return ok ? 0 : 1;
}
