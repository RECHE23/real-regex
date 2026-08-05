// Three places where a value that no longer fits, or a rule applied to only one side, produced a
// silent wrong answer instead of the error every one of them already knew how to raise.
//
// Each test below fails without its fix -- not by throwing where it should not, but by ANSWERING,
// wrongly. That is what makes them worth pinning: the engine reported success on inputs it had
// already decided were errors.
#include <string>

#include <sciforge/test/framework.hpp>
#include "real/real.hpp"

namespace {

  //! True if compiling \p pattern raises.
  bool rejects(const std::string& pattern)
  {
    try {
      const real::regex re {pattern};
      return false;
    }
    catch (const real::regex_error&) {
      return true;
    }
  }

  //! `pad` empty lookaheads, then one that demands a 'b', then a literal 'a'.
  std::string lookaround_pattern(int pad)
  {
    std::string pattern;
    pattern.reserve((static_cast<std::size_t>(pad) * 4) + 8);
    for (int i = 0; i < pad; ++i) {
      pattern += "(?=)";
    }
    return pattern + "(?=b)a";
  }
} // namespace

// The lookaround index rides in a uint16 instruction field. Nothing checked that it fit, so the
// 65536th assertion pointed at sub-pattern 0 -- and sub-pattern 0, here, is the empty lookahead that
// always holds. `(?=b)a` then matched "a", where the 'b' it demands does not occur at all.
//
// Reachable under `max_program_size` because an EMPTY lookaround costs only three instructions:
// 65536 of them is 196 608, well inside the 262 144 cap. With `(?=a)` at four instructions each the
// cap bites first, which is why the shape that exposes this is the cheap one.
TEST(the_lookaround_index_must_fit_the_field_that_carries_it)
{
  const std::string last_that_fits {lookaround_pattern(65535)};
  const real::regex re             {last_that_fits};      // sub_id 65535: the last valid index
  EXPECT(!re.search("a").matched());                      // no 'b' ahead of the 'a'
  EXPECT(!re.search("ba").matched());                     // the 'b' is not where the assertion looks

  EXPECT(rejects(lookaround_pattern(65536)));             // one past the field: refused, not truncated
}

// `$<digits>` accumulated into a size_t with nothing guarding the multiply. Unsigned overflow wraps,
// and a wrapped value can land back INSIDE the group range, so the bounds check below it passed:
// 2^64 written out in decimal became group 0 and substituted the whole match, and the next integer
// up substituted group 1. Python raises on both.
TEST(a_group_reference_that_overflows_must_not_wrap_into_a_valid_group)
{
  const real::regex re {"(a)(b)"};

  EXPECT_EQ(re.replace("ab", "$0"), std::string {"ab"});       // the ordinary references still work
  EXPECT_EQ(re.replace("ab", "$1"), std::string {"a"});
  EXPECT_EQ(re.replace("ab", "$2"), std::string {"b"});
  EXPECT_EQ(re.replace("ab", "$00000001"), std::string {"a"}); // leading zeros are not overflow

  bool wrapped_to_zero {};
  bool wrapped_to_one  {};
  try {
    wrapped_to_zero = re.replace("ab", "$18446744073709551616") == std::string {"ab"};
  }
  catch (const real::regex_error&) {}
  try {
    wrapped_to_one = re.replace("ab", "$18446744073709551617") == std::string {"a"};
  }
  catch (const real::regex_error&) {}
  EXPECT(!wrapped_to_zero);
  EXPECT(!wrapped_to_one);
}

// A class shorthand cannot be a range endpoint. The check existed for the RIGHT-hand side only:
// `[a-\d]` already raised, while `[\d-z]` fell through to the next loop iteration and the '-' was
// then read as an ordinary member -- so the class quietly became `\d` plus '-' plus 'z', and matched
// "-". Python raises on every one of these, and the engine had already chosen Python's rule by
// rejecting the mirror case; only half of it was implemented.
TEST(a_class_shorthand_is_not_a_range_endpoint_on_either_side)
{
  EXPECT(rejects("[\\d-z]"));
  EXPECT(rejects("[a-\\d]"));
  EXPECT(rejects("[\\w-z]"));
  EXPECT(rejects("[\\d-\\w]"));
  EXPECT(rejects("[\\d-\\d]"));
  EXPECT(rejects("[\\s-z]"));

  // A '-' that is not between two endpoints stays a literal member, exactly as before and exactly
  // as Python: trailing before ']', leading, and escaped.
  const real::regex trailing {"[\\d-]"};
  EXPECT(trailing.search("-").matched());
  EXPECT(trailing.search("5").matched());
  EXPECT(!trailing.search("z").matched());

  const real::regex leading {"[-\\d]"};
  EXPECT(leading.search("-").matched());
  EXPECT(leading.search("5").matched());

  const real::regex escaped {"[\\d\\-z]"};
  EXPECT(escaped.search("-").matched());
  EXPECT(escaped.search("5").matched());
  EXPECT(escaped.search("z").matched());

  // Ordinary ranges and plain shorthands are untouched.
  const real::regex ordinary {"[a-z]"};
  EXPECT(ordinary.search("q").matched());
  EXPECT(!ordinary.search("-").matched());
}
