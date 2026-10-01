//! The RE2 names a drop-in caller types that are not new behaviour: the canned option sets (`RE2 re(p, RE2::Quiet)`),
//! the anchor and error-code constants spelled as RE2 members (`RE2::ANCHOR_BOTH`, `RE2::NoError`), and `LazyRE2`.
#include <string>
#include <thread>
#include <vector>

#include <sciforge/test/framework.hpp>
#include "real/compat/re2/re2.hpp"

using real::compat::re2::LazyRE2;
using real::compat::re2::RE2;

namespace {

  template <typename R>
  concept re2_names = requires(const R& c, const char* cs) {
    R(cs, R::Quiet);
    R(cs, R::DefaultOptions);
    typename R::Options(R::Latin1);
    R::UNANCHORED;
    R::ANCHOR_START;
    R::ANCHOR_BOTH;
    c.error_code() == R::NoError;
    c.error_code() == R::ErrorPatternTooLarge;
    typename R::Set(typename R::Options(), R::ANCHOR_BOTH);
  };
  static_assert(re2_names<RE2>);
} // namespace

TEST(re2_canned_options)
{
  const RE2 quiet {"a+", RE2::Quiet};
  EXPECT(quiet.ok());
  EXPECT(!quiet.options().log_errors());
  const RE2 broken {"a(", RE2::Quiet};
  EXPECT(!broken.ok());
  EXPECT(broken.error_code() != RE2::NoError);
  EXPECT(RE2("a", RE2::DefaultOptions).options().log_errors());
  EXPECT(!RE2("a", RE2::Latin1).ok()); // Latin-1 is outside this layer's scope, rejected at construction
  EXPECT(!RE2("a", RE2::POSIX).ok());
  EXPECT(RE2("a", RE2::Latin1).error_code() == RE2::ErrorUnsupported);
  EXPECT(RE2::Options {RE2::POSIX}.longest_match());
}

TEST(re2_anchor_constants)
{
  RE2::Set both {RE2::Options(), RE2::ANCHOR_BOTH};
  EXPECT_EQ(both.Add("ab", nullptr), 0);
  EXPECT(both.Compile());
  EXPECT(both.Match("ab", nullptr));
  EXPECT(!both.Match("xab", nullptr));
  EXPECT(!both.Match("abx", nullptr));
  RE2::Set start {RE2::Options(), RE2::ANCHOR_START};
  EXPECT_EQ(start.Add("ab", nullptr), 0);
  EXPECT(start.Compile());
  EXPECT(start.Match("abx", nullptr));
  EXPECT(!start.Match("xab", nullptr));
}

// Compiled once, on first use, whichever thread comes first.
TEST(re2_lazy_compiles_once)
{
  static const LazyRE2 lazy  {.pattern_  = "a+b"};
  static const LazyRE2 quiet {.pattern_  = "(", .options_ = RE2::Quiet};
  EXPECT(RE2::FullMatch("aab", *lazy));
  EXPECT(!quiet->ok());
  std::vector<const RE2*>  seen(8, nullptr);
  std::vector<std::thread> threads;
  threads.reserve(seen.size());
  for (std::size_t i {0}; i < seen.size(); ++i) {
    threads.emplace_back([&seen, i] { seen[i] = lazy.get(); });
  }
  for (std::thread& t : threads) {
    t.join();
  }
  for (const RE2* const p : seen) {
    EXPECT(p == lazy.get());
  }
}
