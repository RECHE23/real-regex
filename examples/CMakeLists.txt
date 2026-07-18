# Standalone consumer project: builds the C++ examples against an INSTALLED REAL,
# located purely through find_package — no source-tree knowledge. The install-smoke
# CI job configures this with -DCMAKE_PREFIX_PATH=<install prefix>, so the examples
# both demonstrate usage and prove the install (and never rot).
cmake_minimum_required(VERSION 3.16)
project(real_examples CXX)

find_package(real CONFIG REQUIRED)

foreach(ex hello redos_demo quickstart)
  add_executable(${ex} cpp/${ex}.cpp)
  target_link_libraries(${ex} PRIVATE real::real)
  set_target_properties(${ex} PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
endforeach()
