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
  static_assert(icase_rx.fullmatch("HÉLLO").matched()); // …and the é folds too (Unicode icase)

  // Exact sizing: the storage arrays have exactly the measured sizes, and the
  // type is stateless (all data is static constexpr).
  using date_storage = real::detail::static_storage<"(\\d{4})-(\\d{2})">;
  static_assert(date_storage::slot_count == 6);
  static_assert(date_storage::code.size() == date_storage::code_size);
  static_assert(date_storage::cp_class_count == 1); // one interned \d code-point class (klass_cp)
  static_assert(sizeof(real::static_regex<"(\\d{4})-(\\d{2})">) == 1);

  // Invalid patterns are *compile errors* (uncomment to verify):
  //   constexpr real::static_regex<"(a"> broken;

  // D1: Tier 1 possessive quantifiers / atomic groups under TRUE constant evaluation --
  // emit_tier1_loop / emit_tier1_atom_test / tier1_capture_on_match are all constexpr, but
  // nothing exercised them at compile time until now. A static_assert proves the whole
  // pipeline (parse -> compile -> match) runs at compile time, not just "is marked constexpr".
  constexpr real::static_regex<"(a){2,4}+b"> possessive_rx;
  static_assert(possessive_rx.search("xx aaaab yy").matched());
  static_assert(possessive_rx.search("xx aaaab yy").start() == 3);
  static_assert(possessive_rx.search("xx aaaab yy")[1] == "a"sv); // group(1): the LAST iteration
  // search() retries at later start positions: position 0's possessive attempt (max 4 a's) lands
  // on the 5th 'a', not 'b', and fails outright (no giveback) -- but position 1's INDEPENDENT
  // attempt has only 4 a's ahead of it, consumes all 4, and lands exactly on 'b'. Per-attempt
  // independence (D0-1's own oracle contract), not a possessive/greedy distinction here.
  static_assert(possessive_rx.search("aaaaab").start() == 1);
  static_assert(!possessive_rx.fullmatch("aaaaab")); // fullmatch is anchored at 0 only -- no retry available

  constexpr real::static_regex<"(?>[^\"]*)\""> atomic_rx;
  static_assert(atomic_rx.fullmatch("abc\"").matched());
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

TEST(static_regex_possessive_and_atomic_at_runtime)
{
  // The compile-time static_asserts above prove the pipeline runs under constant evaluation;
  // this proves the SAME compiled program also matches correctly at runtime, on non-constant
  // (heap/hybrid) text -- both storage policies (dynamic_program's constexpr paths AND
  // static_storage's own instantiation) actually execute the Tier 1 opcodes, not just compile.
  const std::string text = "xx aaaab yy";
  const auto        m    = possessive_rx.search(text);
  EXPECT(m.matched());
  EXPECT_EQ(m.start(), 3U);
  EXPECT_EQ(m[1], "a"sv);

  const std::string aaaaab = "aaaaab";
  const auto        m2     = possessive_rx.search(aaaaab);
  EXPECT(m2.matched());
  EXPECT_EQ(m2.start(), 1U);
  EXPECT(!possessive_rx.fullmatch(aaaaab));

  const std::string quoted   = "abc\"";
  const std::string unquoted = "abc";
  EXPECT(atomic_rx.fullmatch(quoted).matched());
  EXPECT(!atomic_rx.fullmatch(unquoted));

  // Same results as the dynamic engine on the same patterns.
  const real::regex dyn_possessive("(a){2,4}+b");
  const real::regex dyn_atomic(R"re((?>[^"]*)")re");
  EXPECT_EQ(dyn_possessive.search(text).start(), m.start());
  EXPECT_EQ(dyn_atomic.fullmatch(quoted).matched(), atomic_rx.fullmatch(quoted).matched());
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
  // Atomic UTF-8 literals and code-point classes are fully constexpr: these patterns
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
