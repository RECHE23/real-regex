// The /reference/support page's Example region (the [reference] markers below):
// compiled and run by example-check, so the code the page shows is tested, never
// merely illustrative.
#include <real/real.hpp>

#include <iostream>

int main()
{
  // [reference]
  // flags combine bitwise at construction (the (?imsxa) inline letters work too).
  const real::regex ci {"real", real::flags::icase};
  std::cout << ci.search("the REAL engine").matched() << "\n";  // 1

  // Every rejection raises real::regex_error -- never a silent divergence.
  bool rejected = false;
  try {
    const real::regex bad {R"((a+)\1)"};  // backreference: rejected up front
  } catch (const real::regex_error&) {
    rejected = true;
  }
  std::cout << rejected << "\n";  // 1
  // [/reference]

  return (ci.search("the REAL engine").matched() && rejected) ? 0 : 1;
}
