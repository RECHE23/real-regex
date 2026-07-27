// static_regex<"pattern">: compile-time compilation into exactly sized
// static arrays, constexpr matching, and zero-allocation runtime matching
// (the hybrid mode: compile-time pattern, runtime text), proven by an
// instrumented operator new.
#include <cstdlib>
#include <new>
#include <string>
#include <string_view>
#include <vector>

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

  // Tier 1 possessive quantifiers / atomic groups under TRUE constant evaluation --
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
  // independence (the oracle contract), not a possessive/greedy distinction here.
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

TEST(static_regex_inner_literal_route_criterion)
{
  // Which patterns carry the literal-sweep route, and why. A required literal at offset >= 1 is necessary
  // but not sufficient: `fixed_shape` means the core already scans the whole pattern by arithmetic width
  // and the sweep adds nothing, while without it the core falls to the general VM, which is what the sweep
  // rescues. Measured over a 64 KiB corpus, this storage with the route compiled in vs kept out:
  //
  //     pattern                     fixed | no match: out -> in | matches: out -> in
  //     [a-z]+@[a-z]+                   0 |  1619.67 -> 1.33 us |  1063.54 -> 1053.92 us
  //     \\w+-\\w+                         0 |  1585.38 -> 1.38 us |  1679.04 -> 1677.92 us
  //     \\d+\\.\\d+                        0 |    29.42 -> 1.33 us |   592.75 ->  586.00 us
  //     \\d{4}-\\d{2}-\\d{2}               0 |    29.50 -> 1.46 us |   629.08 ->  629.08 us
  //     [0-9]{4}-[0-9]{2}-[0-9]{2}      1 |     1.42 -> 1.42 us |    29.50 ->   35.38 us
  using date  = real::detail::static_storage<R"(\d{4}-\d{2}-\d{2})">;
  using bdate = real::detail::static_storage<R"([0-9]{4}-[0-9]{2}-[0-9]{2})">;
  using plain = real::detail::static_storage<R"([a-z]+)">;

  static_assert(date::hints.inner_literal_prefix >= 1);
  static_assert(!date::hints.fixed_shape);    // `\d` is a code-point class, so the core route is the general VM
  static_assert(date::wants_inner_literal);
  static_assert(bdate::hints.inner_literal_prefix >= 1);
  static_assert(bdate::hints.fixed_shape);    // same pattern in byte classes: the core already has it
  static_assert(!bdate::wants_inner_literal);
  static_assert(!plain::wants_inner_literal); // no inner literal at all

  // No prefix sub-program is built for this storage: it never runs the reverse confirm (that needs the
  // per-regex immutables it has none of), and compiling one inside a constant expression is what the
  // step budget cannot afford -- it pushed test_constexpr.cpp's flag_cases() past clang's limit.
  static_assert(date {}.view().prefix_code.empty());
  EXPECT(date::wants_inner_literal);
}

TEST(static_regex_inner_literal_matches_the_dynamic_regex)
{
  // Parity on both sides of the criterion, and on both corpora: a haystack with matches (where this
  // storage hands back to the core on the first candidate) and one without (where the sweep runs to the
  // end). Spans must agree with real::regex, not merely counts.
  const std::string hits = [] {
                             std::string t;
                             for (int i = 0; i < 40; ++i) {
                               t += "on 2026-06-10 root@localhost paid 19.99, then 1999-01-02 x@y again; ";
                             }
                             return t;
                           }();
  const std::string misses = [] {
                               std::string t;
                               for (int i = 0; i < 40; ++i) {
                                 t += "the quick brown fox jumps over lazy dogs and cats meet again; ";
                               }
                               return t;
                             }();

  const auto compare {[](const auto& stat, const char* pattern, const std::string& text, bool expect_any) {
                        const real::regex             dyn {pattern};
                        std::vector<std::string_view> from_static;
                        std::vector<std::string_view> from_dynamic;
                        for (const auto& m : stat.find_iter(text)) {
                          from_static.push_back(std::string_view {text}.substr(m.start(), m.end() - m.start()));
                        }
                        for (const auto& m : dyn.find_iter(text)) {
                          from_dynamic.push_back(std::string_view {text}.substr(m.start(), m.end() - m.start()));
                        }
                        EXPECT_EQ(from_static.size(), from_dynamic.size());
                        EXPECT(from_static == from_dynamic);
                        EXPECT_EQ(!from_static.empty(), expect_any);
                      }};

  for (const auto* text : {&hits, &misses}) {
    const bool any {text == &hits};
    compare(real::static_regex<R"(\d{4}-\d{2}-\d{2})"> {}, R"(\d{4}-\d{2}-\d{2})", *text, any);
    compare(real::static_regex<"[0-9]{4}-[0-9]{2}-[0-9]{2}"> {}, "[0-9]{4}-[0-9]{2}-[0-9]{2}", *text, any);
    compare(real::static_regex<R"((\w+)@(\w+))"> {}, R"((\w+)@(\w+))", *text, any);
    compare(real::static_regex<R"(\d+\.\d+)"> {}, R"(\d+\.\d+)", *text, any);
  }
}

TEST(static_regex_inner_literal_walk_allocates_nothing)
{
  // The zero-allocation guarantee has to survive the inner-literal route, not only the core scans. Both
  // halves of the route are walked here: a haystack with no candidate at all (the memmem-only sweep, which
  // is where this storage wins) and one full of them (hand back to the core on the first).
  const std::string misses = [] {
                               std::string t;
                               for (int i = 0; i < 200; ++i) {
                                 t += "filler text with no date at all in it here, ";
                               }
                               return t;
                             }(); // allocates
  const std::string hits = [] {
                             std::string t;
                             for (int i = 0; i < 200; ++i) {
                               t += "filler 2026-06-10 more filler text here, ";
                             }
                             return t;
                           }();                                                             // allocates

  constexpr real::static_regex<R"(\d{4}-\d{2}-\d{2})"> rx;
  static_assert(real::detail::static_storage<R"(\d{4}-\d{2}-\d{2})">::wants_inner_literal); // this walk does take the route

  const std::size_t before {alloc_count};
  std::size_t       none   {0};
  std::size_t       found  {0};
  for (const auto& m : rx.find_iter(std::string_view {misses})) {
    none += m.end() > m.start() ? 1U : 0U;
  }
  for (const auto& m : rx.find_iter(std::string_view {hits})) {
    found += m.end() > m.start() ? 1U : 0U;
  }
  EXPECT_EQ(alloc_count - before, 0U);
  EXPECT_EQ(none, 0U);
  EXPECT_EQ(found, 200U);
}
