# Build system for REAL. Single source of truth for compilation: warning
# policy, per-compiler/per-platform adjustments, and the sanitizer/coverage
# build variants. The root Makefile is a thin orchestrator over these targets.
#
# Consumers depend on the header-only library via the real::real target
# (add_subdirectory or FetchContent). Tests build only when REAL is the
# top-level project.
cmake_minimum_required(VERSION 3.16)

# Single source of truth for the version: pyproject.toml (which `make release`
# bumps and setup.py reads). Deriving it here means the CMake package version can
# never drift from the Python package — the defect that had left this frozen at
# 2026.6.6 while the wheels shipped 2026.6.8.
file(READ "${CMAKE_CURRENT_LIST_DIR}/pyproject.toml" _real_pyproject)
if(NOT _real_pyproject MATCHES "[\r\n]version = \"([0-9]+\\.[0-9]+\\.[0-9]+)\"")
  message(FATAL_ERROR "REAL: could not parse the project version from pyproject.toml")
endif()
project(real VERSION "${CMAKE_MATCH_1}" LANGUAGES CXX)

# Use ccache as a compiler launcher when available (a no-op otherwise): the
# coverage / sanitizer / dual-compiler gates rebuild from scratch, so caching
# object files makes repeat runs near-instant. Override with -DUSE_CCACHE=OFF.
option(USE_CCACHE "Use ccache as the compiler launcher when found" ON)
if(USE_CCACHE)
    find_program(CCACHE_PROGRAM ccache)
    if(CCACHE_PROGRAM)
        set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    endif()
endif()

option(REAL_SANITIZE "Build the tests with AddressSanitizer + UndefinedBehaviorSanitizer" OFF)
option(REAL_COVERAGE "Build the tests with LLVM source-based coverage instrumentation" OFF)

# Header-only library: consumers link real::real to get the include path.
add_library(real INTERFACE)
add_library(real::real ALIAS real)
target_include_directories(real INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_compile_features(real INTERFACE cxx_std_20)

include(GNUInstallDirs)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

# find_package(real CONFIG) support: export the interface target and generate a
# config package, so a downstream CMake project can consume the installed
# library idiomatically, exactly as it consumes any system library:
#   find_package(real CONFIG REQUIRED)
#   target_link_libraries(app PRIVATE real::real)
install(TARGETS real EXPORT realTargets)
install(EXPORT realTargets
    NAMESPACE real::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/real)
include(CMakePackageConfigHelpers)
configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/realConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/realConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/real)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/realConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/realConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/realConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/real)

if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    return()
endif()

enable_testing()

# The test harness (framework.hpp) is owned by SciForge — the ecosystem's shared
# infrastructure repository — and is never carried as a local copy. Resolution
# mirrors how REAL's own consumers find REAL, two modes:
#  * Default (local development): a sibling checkout, used live. The maintainer's
#    layout uses ../sciforge; override with -DSCIFORGE_INCLUDE_DIR=/path.
#  * CI / reproducible builds: -DREAL_FETCH_SCIFORGE=ON fetches SciForge with
#    FetchContent from a configurable repository + tag. Prepared but inert by
#    default; once SciForge publishes a CalVer tag, pin SCIFORGE_TAG to it.
option(REAL_FETCH_SCIFORGE "Fetch SciForge (test harness) with FetchContent" OFF)
set(SCIFORGE_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../sciforge/include"
    CACHE PATH "Path to SciForge's include directory (maintainer's default; override for your layout)")
set(SCIFORGE_REPO "${CMAKE_CURRENT_SOURCE_DIR}/../sciforge"
    CACHE STRING "SciForge git repository (path or URL) when fetching")
set(SCIFORGE_TAG "v2026.6.5" CACHE STRING "SciForge git tag when fetching")

if(REAL_FETCH_SCIFORGE)
    include(FetchContent)
    FetchContent_Declare(sciforge GIT_REPOSITORY "${SCIFORGE_REPO}" GIT_TAG "${SCIFORGE_TAG}")
    FetchContent_GetProperties(sciforge)
    if(NOT sciforge_POPULATED)
        FetchContent_Populate(sciforge) # source only (header-only): no transitive CMake
    endif()
    set(SCIFORGE_INCLUDE_DIR "${sciforge_SOURCE_DIR}/include")
endif()

if(NOT EXISTS "${SCIFORGE_INCLUDE_DIR}/sciforge/test/framework.hpp")
    message(FATAL_ERROR
        "SciForge test harness not found at SCIFORGE_INCLUDE_DIR='${SCIFORGE_INCLUDE_DIR}'. "
        "Use -DREAL_FETCH_SCIFORGE=ON, or set -DSCIFORGE_INCLUDE_DIR=/path/to/sciforge/include.")
endif()

file(GLOB REAL_TEST_SOURCES CONFIGURE_DEPENDS tests/*.cpp)
# Target name avoids a clash with CMake's generated RUN_TESTS on
# case-insensitive generators (Visual Studio).
add_executable(real_tests_bin ${REAL_TEST_SOURCES})
target_link_libraries(real_tests_bin PRIVATE real::real)
# The harness include path comes from SciForge, not from a local tests/ copy.
target_include_directories(real_tests_bin PRIVATE "${SCIFORGE_INCLUDE_DIR}")
set_target_properties(real_tests_bin PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)

# Warnings-as-errors everywhere. IntelLLVM (icpx) and AppleClang accept the
# Clang flag set; only MSVC differs.
if(MSVC)
    # /Zc:__cplusplus makes __cplusplus report 202002L. C4268/C4269 fire on the
    # constexpr static_regex fixtures in tests/test_static.cpp (false positives
    # for the stateless compile-time wrappers); scoped to the test target.
    target_compile_options(real_tests_bin PRIVATE
        /W4 /WX /Zc:__cplusplus /EHsc /utf-8 /wd4268 /wd4269)
else()
    target_compile_options(real_tests_bin PRIVATE -Wall -Wextra -Wpedantic -Werror)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        # Test-only, GCC-only: -Warray-bounds reports a false positive on the
        # small_vec union when its heap state is not visible to the optimizer;
        # -Wmismatched-new-delete fires because the allocation-counting test
        # hook in tests/test_static.cpp pairs malloc/free with operator new.
        target_compile_options(real_tests_bin PRIVATE
            -Wno-array-bounds -Wno-mismatched-new-delete)
    endif()
endif()

if(REAL_SANITIZE)
    if(MSVC)
        message(FATAL_ERROR "REAL_SANITIZE requires a GCC/Clang-family compiler")
    endif()
    target_compile_options(real_tests_bin PRIVATE
        -fsanitize=address,undefined -fno-omit-frame-pointer -g -O1)
    target_link_options(real_tests_bin PRIVATE -fsanitize=address,undefined)
endif()

if(REAL_COVERAGE)
    # LLVM source-based coverage (Clang family). The Makefile coverage target
    # drives report generation with llvm-profdata / llvm-cov.
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "REAL_COVERAGE requires a Clang-family compiler")
    endif()
    target_compile_options(real_tests_bin PRIVATE
        -fprofile-instr-generate -fcoverage-mapping -O0 -g)
    target_link_options(real_tests_bin PRIVATE -fprofile-instr-generate)
endif()

# $<TARGET_FILE:...> resolves the per-config output path on multi-config
# generators (Visual Studio, Xcode).
add_test(NAME run_tests COMMAND $<TARGET_FILE:real_tests_bin>)
