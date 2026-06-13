# Build system for REAL. Single source of truth for compilation: warning
# policy, per-compiler/per-platform adjustments, and the sanitizer/coverage
# build variants. The root Makefile is a thin orchestrator over these targets.
#
# Consumers depend on the header-only library via the real::real target
# (add_subdirectory or FetchContent). Tests build only when REAL is the
# top-level project.
cmake_minimum_required(VERSION 3.16)
project(real LANGUAGES CXX)

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

if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    return()
endif()

enable_testing()

file(GLOB REAL_TEST_SOURCES CONFIGURE_DEPENDS tests/*.cpp)
# Target name avoids a clash with CMake's generated RUN_TESTS on
# case-insensitive generators (Visual Studio).
add_executable(real_tests_bin ${REAL_TEST_SOURCES})
target_link_libraries(real_tests_bin PRIVATE real::real)
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
