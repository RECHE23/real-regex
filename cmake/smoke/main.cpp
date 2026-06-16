// Downstream consumer smoke test: this file is built by a *separate* CMake
// project that locates REAL only through `find_package(real CONFIG)` against an
// installed copy — no source-tree knowledge. It proves the installed config
// package, include paths, and the real::real target all work end to end.
#include "real/real.hpp"

int main()
{
  const real::regex digits {"[0-9]+"};
  return digits.search("abc123").matched() ? 0 : 1;
}
