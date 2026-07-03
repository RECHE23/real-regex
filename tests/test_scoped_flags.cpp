// Scoped inline flags: `(?x:...)`. Verbose is the one flag whose scope changes tokenization
// (insignificant whitespace and `#` comments), so it is honoured per-scope from the flag-scope stack.
// The other scoped flags (i/a/s/m) are still rejected until they are supported; this pins both.
#include <string>

#include <sciforge/test/framework.hpp>
#include "real/real.hpp"

TEST(scoped_verbose_ignores_internal_whitespace)
{
  // Inside (?x:...), unescaped spaces are insignificant; outside, they are literal.
  EXPECT(real::regex("(?x:a )b").fullmatch("ab"));
  EXPECT(!real::regex("(?x:a )b").fullmatch("a b"));
  EXPECT(real::regex("(?x:a b)c d").fullmatch("abc d"));   // the space before 'd' is outside the scope
  EXPECT(!real::regex("(?x:a b)c d").fullmatch("abcd"));   // ... so it is significant
  EXPECT(real::regex("(?x:a\tb\n\f c)").fullmatch("abc")); // every whitespace kind is insignificant
}

TEST(scoped_verbose_ignores_hash_comments)
{
  // A `#` runs to the end of the line inside a verbose scope.
  EXPECT(real::regex("(?x:a #comment\n b)d").fullmatch("abd"));
  EXPECT(!real::regex("(?x:a #comment\n b)d").fullmatch("a b d"));
}

TEST(nested_minus_x_restores_significance)
{
  // (?-x:...) turns verbose back off for its body, even nested inside (?x:...).
  EXPECT(real::regex("(?x:(?-x:a b))").fullmatch("a b"));
  EXPECT(!real::regex("(?x:(?-x:a b))").fullmatch("ab"));
  // and a scope re-enabling x nested inside -x: the inner spaces are dropped, the outer has none
  EXPECT(real::regex("(?-x:a(?x: b )c)").fullmatch("abc"));
  EXPECT(!real::regex("(?-x:a(?x: b )c)").fullmatch("a bc"));
}

TEST(global_x_with_scoped_minus_x)
{
  // A global (?x) at the start, then a scoped (?-x:...) island of significance.
  EXPECT(real::regex("(?x)a (?-x:b c) d").fullmatch("ab cd"));
  EXPECT(!real::regex("(?x)a (?-x:b c) d").fullmatch("abcd")); // the inner space is significant
}

TEST(scoped_non_verbose_flags_are_still_rejected)
{
  // i / a / s / m scoped groups keep their clean rejection until they are supported.
  EXPECT_THROWS(real::regex("(?i:a)"), real::regex_error);
  EXPECT_THROWS(real::regex("(?s:.)"), real::regex_error);
  EXPECT_THROWS(real::regex("(?m:^)"), real::regex_error);
  EXPECT_THROWS(real::regex("(?a:\\w)"), real::regex_error);
  EXPECT_THROWS(real::regex("(?-i:a)"), real::regex_error);
  EXPECT_THROWS(real::regex("(?x-i:a)"), real::regex_error); // x is fine, but the scoped -i is not
  EXPECT_THROWS(real::regex("(?-:a)"), real::regex_error);   // a '-' with no flag after it
}

TEST(scoped_verbose_in_bytes_mode)
{
  using real::flags;
  // Verbose scoping works the same over raw bytes.
  EXPECT(real::regex("(?x:a )b", flags::bytes).fullmatch("ab"));
  EXPECT(!real::regex("(?x:a )b", flags::bytes).fullmatch("a b"));
}
