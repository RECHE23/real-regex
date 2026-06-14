/**\file framework.hpp
 * \brief Minimal zero-dependency test framework used by the C++ test suite.
 *
 * Provides TEST() auto-registration, EXPECT* assertion macros, and a single
 * runner. Failures are reported but never abort the run, so one failing test
 * does not hide others.
 */
#ifndef REAL_TESTS_FRAMEWORK_HPP
#define REAL_TESTS_FRAMEWORK_HPP

#include <cstdio>
#include <exception>
#include <sstream>
#include <string>
#include <vector>

namespace test {

/** \brief One registered test case. */
  struct test_case
  {
    const char* name;    //!< Human-readable test name.
    void        (*fn)(); //!< Test function to invoke.
  };

/*!
 * \brief Returns the global test-case registry.
 * \return A reference to the vector of registered tests.
 */
  inline std::vector<test_case>& registry()
  {
    static std::vector<test_case> cases;
    return cases;
  }

/*!
 * \brief Auto-registration helper for TEST() macros.
 *
 * Constructing a global registrar pushes the named test function into
 * \ref registry at program start-up.
 */
  struct registrar
  {
    /*!\brief Registers a test case.
     * \param[in] name Test name.
     * \param[in] fn   Test function.
     */
    registrar(const char* name,
              void (*fn)()) noexcept
    {
      registry().push_back({.name = name, .fn = fn});
    }
  };

  namespace detail {

    inline int         checks_passed  = 0;     //!< Number of assertions that passed.
    inline int         checks_failed  = 0;     //!< Number of assertions that failed.
    inline const char* current_test   = "";    //!< Name of the currently running test.
    inline bool        current_failed = false; //!< Whether the current test has failed.

    /*!\brief Reports a failed assertion.
     * \param[in] file    Source file where the failure occurred.
     * \param[in] line    Line number where the failure occurred.
     * \param[in] message Human-readable failure message.
     */
    inline void report_failure(const char       * file,
                               int                line,
                               const std::string& message)
    {
      ++checks_failed;
      current_failed = true;
      std::printf("  FAIL %s:%d [%s] %s\n", file, line, current_test, message.c_str());
    }

    /*!\brief Checks a boolean condition.
     * \param[in] ok   The condition result.
     * \param[in] file Source file of the check.
     * \param[in] line Line number of the check.
     * \param[in] expr String representation of the condition.
     */
    inline void check(bool        ok,
                      const char* file,
                      int         line,
                      const char* expr)
    {
      if (ok) {
        ++checks_passed;
        return;
      }
      report_failure(file, line, expr);
    }

    /*!\brief Streams a value into an ostringstream when possible.
     * \tparam T     Value type.
     * \param[in,out] oss   Output stream.
     * \param[in]     value Value to stream.
     */
    template <typename T>
    void print_value(std::ostringstream& oss,
                     const T&            value)
    {
      if constexpr (requires { oss << value; }) {
        oss << value;
      }
      else {
        oss << "<unprintable>";
      }
    }

    /*!\brief Checks that two values are equal.
     * \tparam L     Type of the actual value.
     * \tparam R     Type of the expected value.
     * \param[in] actual   The value produced by the test.
     * \param[in] expected The expected value.
     * \param[in] file     Source file of the check.
     * \param[in] line     Line number of the check.
     * \param[in] expr     String representation of the comparison.
     */
    template <typename L, typename R>
    void check_eq(const L&    actual,
                  const R&    expected,
                  const char* file,
                  int         line,
                  const char* expr)
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

/*!
 * \brief Runs all registered tests.
 * \return 0 if all tests passed, 1 otherwise.
 */
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

/*! \brief Defines and auto-registers a test case.
 *  \param name Unique test name.
 *
 *  Usage: \code TEST(my_feature) { EXPECT_EQ(1, 1); } \endcode
 */
#define TEST(name)                                                         \
        static void                    test_fn_##name();                         \
        static const ::test::registrar test_reg_##name {#name, &test_fn_##name}; \
        static void                    test_fn_##name()

/*! \brief Checks that a condition is true.
 *  \param cond The condition to evaluate.
 */
#define EXPECT(cond) ::test::detail::check(static_cast<bool>(cond), __FILE__, __LINE__, #cond)

/*! \brief Checks that two values are equal.
 *  \param a The actual value.
 *  \param b The expected value.
 */
#define EXPECT_EQ(a, b) ::test::detail::check_eq((a), (b), __FILE__, __LINE__, #a " == " #b)

/*! \brief Checks that an expression throws a specific exception type.
 *  \param expr     Expression to evaluate.
 *  \param exception_type Exception type expected to be thrown.
 */
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
