// Dual compilation: every case here is evaluated at compile time
// (static_assert) AND at runtime (TEST), because constexpr evaluation and
// generated code historically fail in different ways.
#include "framework.hpp"
#include "real/real.hpp"

namespace {

#define CONSTEXPR_EXPECT(cond) \
        if (!(cond))                 \
        return false

  constexpr bool literal_cases()
  {
    CONSTEXPR_EXPECT(real::regex("hello").match("hello world").matched());
    CONSTEXPR_EXPECT(!real::regex("hello").match("say hello"));
    CONSTEXPR_EXPECT(real::regex("ab").search("xxabyy").start() == 2);
    CONSTEXPR_EXPECT(real::regex("ab").search("xxabyy").end() == 4);
    CONSTEXPR_EXPECT(!real::regex("zzz").search("abc"));
    CONSTEXPR_EXPECT(real::regex("hello").fullmatch("hello").matched());
    CONSTEXPR_EXPECT(!real::regex("hello").fullmatch("hello!"));
    CONSTEXPR_EXPECT(real::regex("").match("abc").end() == 0);
    CONSTEXPR_EXPECT(real::regex("a\\.b").match("a.b").matched());
    CONSTEXPR_EXPECT(!real::regex("a\\.b").match("axb"));
    CONSTEXPR_EXPECT(real::regex("abc").group_count() == 0);
    return true;
  }

  constexpr bool class_cases()
  {
    CONSTEXPR_EXPECT(real::regex("[a-z]").fullmatch("q").matched());
    CONSTEXPR_EXPECT(!real::regex("[a-z]").fullmatch("Q"));
    CONSTEXPR_EXPECT(real::regex("[^a]").fullmatch("é").matched());
    CONSTEXPR_EXPECT(!real::regex("[^a]").fullmatch("ab"));
    CONSTEXPR_EXPECT(real::regex("\\d\\d").fullmatch("42").matched());
    CONSTEXPR_EXPECT(real::regex("\\D").fullmatch("é").matched());
    CONSTEXPR_EXPECT(real::regex(".").fullmatch("𝄞").matched());
    CONSTEXPR_EXPECT(!real::regex(".").fullmatch("\n"));
    CONSTEXPR_EXPECT(real::regex("a.c").fullmatch("aéc").matched());
    CONSTEXPR_EXPECT(real::regex("\\x41").fullmatch("A").matched());
    CONSTEXPR_EXPECT(real::regex("[^,]+").search("ab,c").end() == 2); // codepoint-class +
    CONSTEXPR_EXPECT(real::regex(".+").search("a\nb").end() == 1);    // dot stops at \n
    return true;
  }

  constexpr bool quantifier_cases()
  {
    CONSTEXPR_EXPECT(real::regex("a*").fullmatch("aaaa").matched());
    CONSTEXPR_EXPECT(real::regex("a+").match("aaab").end() == 3);
    CONSTEXPR_EXPECT(real::regex("a+?").match("aaa").end() == 1);
    CONSTEXPR_EXPECT(real::regex("a{2,4}").match("aaaaa").end() == 4);
    CONSTEXPR_EXPECT(real::regex("a{2,4}?").match("aaaaa").end() == 2);
    CONSTEXPR_EXPECT(!real::regex("a{3}").fullmatch("aa"));
    CONSTEXPR_EXPECT(real::regex("\\d+").fullmatch("123").matched());
    CONSTEXPR_EXPECT(real::regex("a{").fullmatch("a{").matched());
    CONSTEXPR_EXPECT(real::regex("<.+?>").search("<a><b>").end() == 3);
    CONSTEXPR_EXPECT(real::regex("[0-9a-f]{8}").search("x a3f9c1d8 y").start() == 2);
    CONSTEXPR_EXPECT(!real::regex("[0-9a-f]{8}").fullmatch("deadbeef0"));
    return true;
  }

  constexpr bool group_cases()
  {
    CONSTEXPR_EXPECT(real::regex("cat|dog").fullmatch("dog").matched());
    CONSTEXPR_EXPECT(real::regex("a|ab").search("ab").end() == 1); // leftmost-first
    CONSTEXPR_EXPECT(real::regex("((a|b)|c)d").fullmatch("bd").matched());
    CONSTEXPR_EXPECT(real::regex("(foo|bar)*baz").fullmatch("barfoobaz").matched());
    CONSTEXPR_EXPECT(real::regex("(\\d{4})-(\\d{2})").search("2026-06").start(1) == 0);
    CONSTEXPR_EXPECT(real::regex("(\\d{4})-(\\d{2})").search("2026-06")[2] ==
                     std::string_view("06"));
    CONSTEXPR_EXPECT(real::regex("(a)?b").fullmatch("b").start(1) == real::npos);
    CONSTEXPR_EXPECT(real::regex("(ab)+").fullmatch("abab").start(1) == 2);
    CONSTEXPR_EXPECT(real::regex("(?P<y>\\d+)x").search("42x")["y"] == std::string_view("42"));
    CONSTEXPR_EXPECT(real::regex("(?:a|b)c").fullmatch("bc").matched());
    CONSTEXPR_EXPECT(real::regex("the|fox|dog").search("a dog").start() == 2); // alternation fast path
    CONSTEXPR_EXPECT(real::regex("a|ab").search("ab").end() == 1);             // leftmost-first
    return true;
  }

  constexpr bool anchor_and_flag_cases()
  {
    CONSTEXPR_EXPECT(real::regex("^ab").search("abab").start() == 0);
    CONSTEXPR_EXPECT(real::regex("ab$").search("abab").start() == 2);
    CONSTEXPR_EXPECT(real::regex("ab$").search("ab\n").matched());
    CONSTEXPR_EXPECT(!real::regex("ab\\Z").search("ab\n"));
    CONSTEXPR_EXPECT(real::regex("\\bcat\\b").search("a cat!").start() == 2);
    CONSTEXPR_EXPECT(!real::regex("\\bcat\\b").search("concat"));
    CONSTEXPR_EXPECT(real::regex("\\<cat\\>").search("a cat!").start() == 2);
    CONSTEXPR_EXPECT(!real::regex("\\<cat\\>").search("category"));
    CONSTEXPR_EXPECT(real::regex("hello", real::flags::icase).fullmatch("HeLLo").matched());
    CONSTEXPR_EXPECT(!real::regex("[^a]", real::flags::icase).fullmatch("A"));
    CONSTEXPR_EXPECT(real::regex("a.b", real::flags::dotall).fullmatch("a\nb").matched());
    CONSTEXPR_EXPECT(real::regex("(?im)^b$").search("a\nB").matched());
    CONSTEXPR_EXPECT(real::regex("^b.d$", real::flags::multiline).search("xx\nbed").start() == 3);
    return true;
  }

  constexpr bool iteration_cases()
  {
    std::size_t       count = 0;
    std::size_t       total = 0;
    const real::regex digits("\\d+");
    for (const auto& m : digits.find_iter("a1 bb22 c333")) {
      ++count;
      total += m.end() - m.start();
    }
    CONSTEXPR_EXPECT(count == 3);
    CONSTEXPR_EXPECT(total == 6);
    const real::regex xs("x*");
    CONSTEXPR_EXPECT(xs.find_all("axb").size() == 4); // Python spans
    CONSTEXPR_EXPECT(real::regex("\\d+").replace("a1b22c", "#") == "a#b#c");
    CONSTEXPR_EXPECT(real::regex("(\\w+)@(\\w+)").replace("bob@host", "$2:$1") == "host:bob");
    CONSTEXPR_EXPECT(real::regex("x*").replace("axb", "-") == "-a--b-");
    CONSTEXPR_EXPECT(real::regex(",").split("a,b,,c").size() == 4);
    CONSTEXPR_EXPECT(real::regex("(,)").split("a,b,c", 1).size() == 3);
    return true;
  }
} // namespace

// The following static_asserts exercise the full constexpr pipeline for every
// major feature group. They are duplicated at runtime via the TEST() macros
// below (see the comment at the top of the file).
//
// Under libstdc++ from GCC < 15 there is a bug in which std::string::~basic_string()
// (and some internal pointer comparisons it performs for SSO) is not considered
// a valid constant expression. This only affects the compile-time evaluation of
// a subset of the cases that happen to construct/return std::string_view from
// operations that trigger temporary strings during ct evaluation. The generated
// runtime code is unaffected, and the runtime TEST() coverage below always runs.
//
// We therefore guard only the compile-time static_asserts for the feature cases
// with a single, clearly documented #if. This is the minimal intrusion into the
// test file while keeping every constexpr function definition and every runtime
// test unconditionally compiled and executed.
#if !defined(__GNUC__) || defined(__clang__) || __GNUC__ >= 15
static_assert(literal_cases());
static_assert(class_cases());
static_assert(quantifier_cases());
static_assert(group_cases());
static_assert(anchor_and_flag_cases());
static_assert(iteration_cases());
#endif

// The small_vec compile-time coverage test below only uses ints (no std::string
// temporaries) and is therefore always safe to static_assert.
static_assert([] {
                real::detail::small_vec<int, 4> v;
                v.assign(2, 42);
                auto v2 = v; // copy in ct
                return v2[0];
              }() == 42);

TEST(constexpr_literal_cases_also_pass_at_runtime)
{
  EXPECT(literal_cases());
}

TEST(constexpr_class_cases_also_pass_at_runtime)
{
  EXPECT(class_cases());
}

TEST(constexpr_quantifier_cases_also_pass_at_runtime)
{
  EXPECT(quantifier_cases());
}

TEST(constexpr_group_cases_also_pass_at_runtime)
{
  EXPECT(group_cases());
}

TEST(constexpr_anchor_and_flag_cases_also_pass_at_runtime)
{
  EXPECT(anchor_and_flag_cases());
}

TEST(constexpr_iteration_cases_also_pass_at_runtime)
{
  EXPECT(iteration_cases());
}
