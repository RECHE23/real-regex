// hello.cpp — the smallest useful REAL program: find a number in a string.
// Build against an installed REAL via examples/CMakeLists.txt, or directly:
//   c++ -std=c++20 $(pkg-config --cflags real) cpp/hello.cpp -o hello
#include <real/real.hpp>

#include <iostream>

int main()
{
  const real::regex re("[0-9]+");             // runtime pattern, linear-time engine
  const auto        m = re.search("answer = 42"); // leftmost match anywhere
  if (m.matched()) {
    std::cout << "matched \"" << m[0] << "\" at [" << m.start() << ", " << m.end() << ")\n";
  }
  return m.matched() ? 0 : 1;
}
