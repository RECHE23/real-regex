// Public API on the simplest patterns: literals, concatenation, escapes.
#include <string_view>

#include "framework.hpp"
#include "real/real.hpp"

using namespace std::string_view_literals;

TEST(literal_match_is_anchored_prefix)
{
  const real::regex rx("hello");
  EXPECT(rx.match("hello"));
  EXPECT(rx.match("hello world"));
  EXPECT(!rx.match("say hello"));
  EXPECT_EQ(rx.match("hello world")[0], "hello"sv);
  EXPECT_EQ(rx.match("hello world").start(), 0U);
  EXPECT_EQ(rx.match("hello world").end(), 5U);
}

TEST(literal_fullmatch_consumes_everything)
{
  const real::regex rx("hello");
  EXPECT(rx.fullmatch("hello"));
  EXPECT(!rx.fullmatch("hello!"));
  EXPECT(!rx.fullmatch("hell"));
}

TEST(literal_search_finds_leftmost)
{
  const real::regex        rx("ab");
  auto m = rx.search("xxabyyab");
  EXPECT(m);
  EXPECT_EQ(m.start(), 2U);
  EXPECT_EQ(m.end(), 4U);
  EXPECT_EQ(m[0], "ab"sv);
  EXPECT(!rx.search("xyz"));
}

TEST(empty_pattern_matches_empty_string)
{
  const real::regex rx("");
  EXPECT(rx.match("abc"));
  EXPECT_EQ(rx.match("abc").end(), 0U);
  EXPECT(rx.fullmatch(""));
  EXPECT(!rx.fullmatch("a"));
}

TEST(escaped_metacharacters_are_literals)
{
  const real::regex rx("a\\.b");
  EXPECT(rx.match("a.b"));
  EXPECT(!rx.match("axb"));
  EXPECT(real::regex("\\\\").match("\\"));
  EXPECT(real::regex("\\*\\+\\?").match("*+?"));
}

TEST(no_match_result_is_empty)
{
  const real::regex        rx("zzz");
  auto m = rx.search("abc");
  EXPECT(!m);
  EXPECT_EQ(m.start(), real::npos);
  EXPECT_EQ(m.end(), real::npos);
  EXPECT_EQ(m[0], ""sv);
}

TEST(unsupported_syntax_is_rejected)
{
  EXPECT_THROWS(real::regex("(?=a)"), real::regex_error);  // lookarounds: v2
  EXPECT_THROWS(real::regex("(?P=g)"), real::regex_error); // backrefs: v2
  EXPECT_THROWS(real::regex("\\q"), real::regex_error);
  EXPECT_THROWS(real::regex("a\\"), real::regex_error);
}

TEST(regex_error_reports_position)
{
  try {
    real::regex rx("ab\\q");
    EXPECT(false);
  }
  catch (const real::regex_error& e) {
    EXPECT_EQ(e.position(), 3U);
    EXPECT(std::string_view(e.what()).find("escape") != std::string_view::npos);
  }
}

TEST(pattern_and_group_count_accessors)
{
  const real::regex rx("abc");
  EXPECT_EQ(rx.pattern(), "abc"sv);
  EXPECT_EQ(rx.group_count(), 0U);
}

TEST(matching_works_on_embedded_nul_and_binary_text)
{
  const real::regex rx("b");
  EXPECT_EQ(rx.search("a\0b"sv).start(), 2U);
}

TEST(dynamic_large_slots_sbo)
{
  // Exercise small_vec heap path for >32 slots (evidence that SBO helps common small cases
  // while correctly growing for rare large-group patterns). Built from old container inspection.
  std::string pat;
  for (int i = 0; i < 40; ++i) {
    pat += "(x)";
  }
  real::regex rx(pat);
  std::string subject(40, 'x');
  auto m = rx.search(subject);  // owning string -> use view internally; avoids deleted && overload
  EXPECT(m.matched());
  EXPECT(m.size() > 32);  // >32 groups -> slot count >64, forces reserve in small_vec (heap path)
  EXPECT(m[39] == "x");   // last group participates

  // Explicit copy after growth: exercises small_vec heap copy ctor (was a coverage gap
  // for the SBO advancement; secures that grown results can be copied without issue).
  auto m2 = m; // NOLINT(performance-unnecessary-copy-initialization) — the copy is the test
  EXPECT(m2.size() > 32);
  EXPECT(m2[39] == "x");
}

TEST(dynamic_find_all_exercises_result_copies_with_sbo)
{
  // Bonify test for small_vec in dynamic slot_storage: find_all creates
  // std::vector<result_type> and push_back copies the small_vec slots.
  // Exercises inline SBO copy path for common small-group case.
  real::regex rx("(\\w+)");
  auto results = rx.find_all("a1 b22 c333");
  EXPECT_EQ(results.size(), 3U);
  EXPECT_EQ(results[0][1], "a1");
  EXPECT_EQ(results[1][1], "b22");
  EXPECT_EQ(results[2][1], "c333");
}

TEST(small_vec_grown_result_copy_exercises_heap_copy)
{
  // Bonify coverage for small_vec: explicit copy of a grown (>32 slots)
  // result exercises heap copy ctor/assign (missed region in prior coverage).
  // Relevant for robustness of SBO in dynamic results with many groups.
  std::string pat;
  for (int i = 0; i < 40; ++i) {
    pat += "(x)";
  }
  real::regex rx(pat);
  std::string subject(40, 'x');
  auto m = rx.search(subject);
  auto m2 = m; // NOLINT(performance-unnecessary-copy-initialization) — copy after growth is the test
  EXPECT(m2.size() > 32);
  EXPECT(m2[39] == "x");

  // Bonus: move after growth to exercise small_vec move ctor/assign for heap case.
  auto m3 = std::move(m2);
  EXPECT(m3.size() > 32);
  EXPECT(m3[39] == "x");
}

// --- Program size limit (config.hpp + compiler guard, #1+#3) ---------------
// Prevents the validated DoS: 24-char nested bounded quant pattern expanding
// via unroll to ~GB allocation / millions of NFA instrs. We cap at 256Ki
// (allows practical a{1000} etc, rejects the blowup cases).
// The error is raised during emission, so peak mem stays bounded (~few MiB).

TEST(program_size_limit)
{
    // Reasonable large bounded (a{200} emits ~203 instr) must still work.
    // Well below cap; exercises unroll path without hitting limit.
    {
        real::regex r("a{200}");
        EXPECT(r.raw_program().code.size() > 150);
        EXPECT(r.raw_program().code.size() < 300);
        // matching still functions
        std::string subject(200, 'a');
        auto m = r.search(subject);
        EXPECT(m.matched());
        EXPECT_EQ(m[0].size(), 200U);
    }

    // A nested shape that multiplies unrolls beyond cap must raise cleanly.
    // Tune exponents so product of unrolls > 262144 while pattern text small.
    // Rough: 400 * 400 * 2  > cap.
    {
        std::string pat = "((a{400}){400}){2}";
        bool threw = false;
        std::string what;
        try {
            real::regex r(pat);
        } catch (const real::regex_error& e) {
            threw = true;
            what = e.what();
        } catch (...) {
            // other exception bad
        }
        EXPECT(threw);
        // Message contains "program too large" (position 0 as we don't track emit site precisely)
        EXPECT(what.find("program too large") != std::string::npos);
    }
}
