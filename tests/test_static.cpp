// static_regex<"pattern">: compile-time compilation into exactly sized
// static arrays, constexpr matching, and zero-allocation runtime matching
// (the hybrid mode: compile-time pattern, runtime text), proven by an
// instrumented operator new.
#include <cstdlib>
#include <new>
#include <string>
#include <string_view>

#include <sciforge/test/framework.hpp>
#include "real/real.hpp"

using namespace std::string_view_literals;

// --- allocation counting (whole binary; tests measure deltas) -------------

namespace {

  std::size_t alloc_count = 0;
}

// NOLINTBEGIN — this *is* the allocator: malloc and odd names are the point.
void* operator new(std::size_t count)
{
  ++alloc_count;
  if (void* ptr = std::malloc(count)) {
    return ptr;
  }
  throw std::bad_alloc();
}

void* operator new[](std::size_t count)
{
  return ::operator new(count);
}

void operator delete(void* ptr) noexcept
{
  std::free(ptr);
}

void operator delete[](void* ptr) noexcept
{
  std::free(ptr);
}

void operator delete(void* ptr,
                     std::size_t) noexcept
{
  std::free(ptr);
}

void operator delete[](void* ptr,
                       std::size_t) noexcept
{
  std::free(ptr);
}

// NOLINTEND

// --- compile-time compilation and matching --------------------------------

namespace {

  constexpr real::static_regex<"(\\d{4})-(\\d{2})"> date_rx;

  static_assert(date_rx.search("on 2026-06-10").start() == 3);
  static_assert(date_rx.search("on 2026-06-10")[1] == "2026"sv);
  static_assert(!date_rx.search("nothing"));
  static_assert(date_rx.group_count() == 2);

  constexpr real::static_regex<"(?i)héllo"> icase_rx;
  static_assert(icase_rx.fullmatch("HéLLO").matched()); // ASCII letters fold…
  static_assert(icase_rx.fullmatch("HÉLLO").matched()); // …and the é folds too (Unicode icase, CF2)

  // Exact sizing: the storage arrays have exactly the measured sizes, and the
  // type is stateless (all data is static constexpr).
  using date_storage = real::detail::static_storage<"(\\d{4})-(\\d{2})">;
  static_assert(date_storage::slot_count == 6);
  static_assert(date_storage::code.size() == date_storage::code_size);
  static_assert(date_storage::class_count == 1); // one interned digit class
  static_assert(sizeof(real::static_regex<"(\\d{4})-(\\d{2})">) == 1);

  // Invalid patterns are *compile errors* (uncomment to verify):
  //   constexpr real::static_regex<"(a"> broken;
} // namespace

TEST(static_regex_matches_at_runtime_like_dynamic)
{
  const std::string text     = "meeting on 2026-06-10, room 4";
  const auto        match    = date_rx.search(text);
  EXPECT(match.matched());
  EXPECT_EQ(match[0], "2026-06"sv);
  EXPECT_EQ(match[1], "2026"sv);
  EXPECT_EQ(match[2], "06"sv);
  // Same results as the dynamic engine on the same pattern.
  const real::regex dyn("(\\d{4})-(\\d{2})");
  EXPECT_EQ(dyn.search(text).start(), match.start());
}

TEST(static_regex_matching_allocates_nothing)
{
  const std::string      text       = "meeting on 2026-06-10, room 4"; // allocates
  const std::string_view view       = text;
  const std::size_t      before     = alloc_count;
  const auto             match      = date_rx.search(view);
  const auto             f          = date_rx.fullmatch(view);
  const bool             ok         = match.matched() && !f.matched();
  EXPECT_EQ(alloc_count - before, 0U); // hybrid mode: zero allocations
  EXPECT(ok);
  EXPECT_EQ(match.start(), 11U);
}

TEST(static_regex_named_groups_and_flags)
{
  constexpr real::static_regex<"(?P<h>\\d{2}):(?P<m>\\d{2})">     clock;
  const auto                                                      match = clock.search("at 09:45!");
  EXPECT_EQ(match["h"], "09"sv);
  EXPECT_EQ(match["m"], "45"sv);
  constexpr real::static_regex<"^b.d$", real::flags::multiline | real::flags::dotall> ml;
  EXPECT(ml.search("xx\nb\nd"));
  EXPECT(has_flag(ml.compile_flags(), real::flags::multiline));
  EXPECT_EQ(clock.pattern(), "(?P<h>\\d{2}):(?P<m>\\d{2})"sv);
}

TEST(static_regex_iteration_and_replace)
{
  constexpr real::static_regex<"\\d+"> digits;
  std::size_t                          count = 0;
  for (const auto& match : digits.find_iter("a1 bb22 c333")) {
    count += match.end() - match.start();
  }
  EXPECT_EQ(count, 6U);
  EXPECT_EQ(digits.replace("a1b22", "#"), std::string("a#b#"));
  EXPECT_EQ(digits.split("a1b").size(), 2U);
}

TEST(static_vec_overflow_throws)
{
  real::detail::static_vec<int, 2> v;
  v.push_back(1);
  v.push_back(2);
  EXPECT_THROWS(v.push_back(3), std::length_error);
  EXPECT_THROWS(v.assign(3, 0), std::length_error);
  v.assign(1, 9);
  EXPECT_EQ(v.size(), 1U);
  EXPECT_EQ(v[0], 9);
}

TEST(static_regex_constexpr_iteration)
{
  constexpr std::size_t n = [] {
                              constexpr real::static_regex<"[ab]+"> rx;
                              std::size_t                           total = 0;
                              for (const auto& match : rx.find_iter("ab cd abba")) {
                                total += match.end() - match.start();
                              }
                              return total;
                            }();
  static_assert(n == 6);
  EXPECT_EQ(n, 6U);
}

TEST(static_regex_utf8_literals_and_classes_are_constexpr)
{
  // The UTF-8 arc (U1 atomic literals, U2 code-point classes) is fully constexpr: these patterns
  // compile at build time and match at compile time. Pinned so a later change cannot regress it.
  static_assert(real::static_regex<"é+">().search("ééé").matched());
  static_assert(real::static_regex<"[é]">().search("é").matched());
  static_assert(!real::static_regex<"[é]">().search("à").matched());
  static_assert(real::static_regex<"[à-ÿ]">().search("ê").matched());
  static_assert(real::static_regex<"[^é]">().search("à").matched());
  static_assert(!real::static_regex<"[^é]">().search("é").matched());
  EXPECT(real::static_regex<"é+">().search("ééé").matched());
  EXPECT(real::static_regex<"[^é]">().fullmatch("€").matched());
}
