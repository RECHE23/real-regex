# Standalone consumer project: builds the C++ examples against an INSTALLED REAL,
# located purely through find_package — no source-tree knowledge. The install-smoke
# CI job configures this with -DCMAKE_PREFIX_PATH=<install prefix>, so the examples
# both demonstrate usage and prove the install (and never rot).
#
# The source tree is the denominator: every examples/cpp/*.cpp is a target.
# A handwritten foreach listed three names while ten files existed — install-smoke
# (e) only ran what this file named, and example-check's 10/10 against -Iinclude
# made the hole look closed. A new .cpp must not require editing this list.
cmake_minimum_required(VERSION 3.16)
project(real_examples CXX)

find_package(real CONFIG REQUIRED)

file(GLOB REAL_EXAMPLE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/cpp/*.cpp")
foreach(src ${REAL_EXAMPLE_SOURCES})
  get_filename_component(ex ${src} NAME_WE)
  add_executable(${ex} "${src}")
  target_link_libraries(${ex} PRIVATE real::real)
  set_target_properties(${ex} PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
endforeach()
