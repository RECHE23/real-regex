// Minimal zero-dependency test framework: TEST() auto-registration,
// EXPECT* macros, one runner. Failures never abort the run.
#ifndef REAL_TESTS_FRAMEWORK_HPP
#define REAL_TESTS_FRAMEWORK_HPP

#include <cstdio>
#include <exception>
#include <sstream>
#include <string>
#include <vector>

namespace test {

struct test_case
{
  const char* name;
  void        (*fn)();
};

inline std::vector<test_case>& registry()
{
  static std::vector<test_case> cases;
  return cases;
}

struct registrar
{
  registrar(const char* name, void (*fn)()) noexcept
  {
    registry().push_back({.name = name, .fn = fn});
  }
};

namespace detail {

inline int         checks_passed  = 0;
inline int         checks_failed  = 0;
inline const char* current_test   = "";
inline bool        current_failed = false;

inline void report_failure(const char* file, int line, const std::string& message)
{
  ++checks_failed;
  current_failed = true;
  std::printf("  FAIL %s:%d [%s] %s\n", file, line, current_test, message.c_str());
}

inline void check(bool ok, const char* file, int line, const char* expr)
{
  if (ok) {
    ++checks_passed;
    return;
  }
  report_failure(file, line, expr);
}

template <typename T>
void print_value(std::ostringstream& oss, const T& value)
{
  if constexpr (requires { oss << value; }) {
    oss << value;
  }
  else {
    oss << "<unprintable>";
  }
}

template <typename L, typename R>
void check_eq(const L& actual, const R& expected, const char* file, int line, const char* expr)
{
  if (actual == expected) {
    ++checks_passed;
    return;
  }
  std::ostringstream oss;
  oss << expr << " — actual: ";
  print_value(oss, actual);
  oss << ", expected: ";
  print_value(oss, expected);
  report_failure(file, line, oss.str());
}

} // namespace detail

inline int run_all()
{
  int tests_failed = 0;
  for (const auto& tc : registry()) {
    detail::current_test   = tc.name;
    detail::current_failed = false;
    try {
      tc.fn();
    }
    catch (const std::exception& e) {
      detail::report_failure(tc.name, 0, std::string("unexpected exception: ") + e.what());
    }
    catch (...) {
      detail::report_failure(tc.name, 0, "unexpected non-standard exception");
    }
    if (detail::current_failed) {
      ++tests_failed;
    }
  }
  std::printf("%zu tests | %d checks passed | %d checks failed\n", registry().size(), detail::checks_passed, detail::checks_failed);
  if (tests_failed > 0) {
    std::printf("FAILED (%d test(s))\n", tests_failed);
    return 1;
  }
  std::printf("OK\n");
  return 0;
}

} // namespace test

#define TEST(name)                                                         \
  static void                    test_fn_##name();                         \
  static const ::test::registrar test_reg_##name {#name, &test_fn_##name}; \
  static void                    test_fn_##name()

#define EXPECT(cond) ::test::detail::check(static_cast<bool>(cond), __FILE__, __LINE__, #cond)

#define EXPECT_EQ(a, b) ::test::detail::check_eq((a), (b), __FILE__, __LINE__, #a " == " #b)

#define EXPECT_THROWS(expr, exception_type)                                                   \
  do {                                                                                        \
    bool caught_ = false;                                                                     \
    try {                                                                                     \
      (void)(expr);                                                                           \
    }                                                                                         \
    catch (const exception_type&) {                                                           \
      caught_ = true;                                                                         \
    }                                                                                         \
    catch (...) {                                                                             \
    }                                                                                         \
    ::test::detail::check(caught_, __FILE__, __LINE__, "throws " #exception_type ": " #expr); \
  } while (0)

#endif // REAL_TESTS_FRAMEWORK_HPP
