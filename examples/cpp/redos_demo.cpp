// redos_demo.cpp — the catastrophic pattern (a+)+b stays LINEAR in REAL.
// On "a"×N with no 'b' (no match), a backtracking engine (std::regex, Python re)
// takes exponential time; REAL is a Thompson NFA simulation, so it cannot blow up.
#include <real/real.hpp>

#include <chrono>
#include <iostream>
#include <string>

int main()
{
  const real::regex re("(a+)+b");
  const std::string text(100000, 'a');          // 100k 'a', no 'b' -> no match
  const auto        t0    = std::chrono::steady_clock::now();
  const bool        found = re.search(text).matched();
  const double      ms    = std::chrono::duration<double, std::milli>(
    std::chrono::steady_clock::now() - t0).count();
  std::cout << "(a+)+b over " << text.size() << " 'a': matched=" << std::boolalpha << found
            << " in " << ms << " ms (linear — a backtracker would hang here)\n";
  return 0;
}
